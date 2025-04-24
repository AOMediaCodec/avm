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

#if !CONFIG_AIMC
static const aom_cdf_prob
    default_kf_y_mode_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(
        INTRA_MODES)] = {
      { { AOM_CDF13(15588, 17027, 19338, 20218, 20682, 21110, 21825, 23244,
                    24189, 28165, 29093, 30466) },
        { AOM_CDF13(12016, 18066, 19516, 20303, 20719, 21444, 21888, 23032,
                    24434, 28658, 30172, 31409) },
        { AOM_CDF13(10052, 10771, 22296, 22788, 23055, 23239, 24133, 25620,
                    26160, 29336, 29929, 31567) },
        { AOM_CDF13(14091, 15406, 16442, 18808, 19136, 19546, 19998, 22096,
                    24746, 29585, 30958, 32462) },
        { AOM_CDF13(12122, 13265, 15603, 16501, 18609, 20033, 22391, 25583,
                    26437, 30261, 31073, 32475) } },
      { { AOM_CDF13(10023, 19585, 20848, 21440, 21832, 22760, 23089, 24023,
                    25381, 29014, 30482, 31436) },
        { AOM_CDF13(5983, 24099, 24560, 24886, 25066, 25795, 25913, 26423,
                    27610, 29905, 31276, 31794) },
        { AOM_CDF13(7444, 12781, 20177, 20728, 21077, 21607, 22170, 23405,
                    24469, 27915, 29090, 30492) },
        { AOM_CDF13(8537, 14689, 15432, 17087, 17408, 18172, 18408, 19825,
                    24649, 29153, 31096, 32210) },
        { AOM_CDF13(7543, 14231, 15496, 16195, 17905, 20717, 21984, 24516,
                    26001, 29675, 30981, 31994) } },
      { { AOM_CDF13(12613, 13591, 21383, 22004, 22312, 22577, 23401, 25055,
                    25729, 29538, 30305, 32077) },
        { AOM_CDF13(9687, 13470, 18506, 19230, 19604, 20147, 20695, 22062,
                    23219, 27743, 29211, 30907) },
        { AOM_CDF13(6183, 6505, 26024, 26252, 26366, 26434, 27082, 28354, 28555,
                    30467, 30794, 32086) },
        { AOM_CDF13(10718, 11734, 14954, 17224, 17565, 17924, 18561, 21523,
                    23878, 28975, 30287, 32252) },
        { AOM_CDF13(9194, 9858, 16501, 17263, 18424, 19171, 21563, 25961, 26561,
                    30072, 30737, 32463) } },
      { { AOM_CDF13(12602, 14399, 15488, 18381, 18778, 19315, 19724, 21419,
                    25060, 29696, 30917, 32409) },
        { AOM_CDF13(8203, 13821, 14524, 17105, 17439, 18131, 18404, 19468,
                    25225, 29485, 31158, 32342) },
        { AOM_CDF13(8451, 9731, 15004, 17643, 18012, 18425, 19070, 21538, 24605,
                    29118, 30078, 32018) },
        { AOM_CDF13(7714, 9048, 9516, 16667, 16817, 16994, 17153, 18767, 26743,
                    30389, 31536, 32528) },
        { AOM_CDF13(8843, 10280, 11496, 15317, 16652, 17943, 19108, 22718,
                    25769, 29953, 30983, 32485) } },
      { { AOM_CDF13(12578, 13671, 15979, 16834, 19075, 20913, 22989, 25449,
                    26219, 30214, 31150, 32477) },
        { AOM_CDF13(9563, 13626, 15080, 15892, 17756, 20863, 22207, 24236,
                    25380, 29653, 31143, 32277) },
        { AOM_CDF13(8356, 8901, 17616, 18256, 19350, 20106, 22598, 25947, 26466,
                    29900, 30523, 32261) },
        { AOM_CDF13(10835, 11815, 13124, 16042, 17018, 18039, 18947, 22753,
                    24615, 29489, 30883, 32482) },
        { AOM_CDF13(7618, 8288, 9859, 10509, 15386, 18657, 22903, 28776, 29180,
                    31355, 31802, 32593) } }
    };
#endif

#if CONFIG_IMPROVED_INTRA_DIR_PRED
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_mrl_index_cdf[MRL_INDEX_CONTEXTS][CDF_SIZE(MRL_LINE_NUMBER)] = {
      { AOM_CDF4(28081, 30613, 31659), 78 },
      { AOM_CDF4(22175, 28045, 30623), 75 },
      { AOM_CDF4(17175, 25921, 29682), 1 },
    };
#else
static const aom_cdf_prob
    default_mrl_index_cdf[MRL_INDEX_CONTEXTS][CDF_SIZE(MRL_LINE_NUMBER)] = {
      { AOM_CDF4(27852, 29491, 31129) },
      { AOM_CDF4(23920, 27852, 30474) },
      { AOM_CDF4(20316, 26542, 29818) },
    };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob default_mrl_index_cdf[CDF_SIZE(MRL_LINE_NUMBER)] = {
  AOM_CDF4(24756, 29049, 31092)
};
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED

#if CONFIG_MRLS_IMPROVE
static const aom_cdf_prob default_multi_line_mrl_cdf[MRL_INDEX_CONTEXTS]
                                                    [CDF_SIZE(2)] = {
                                                      { AOM_CDF2(28081), 0 },
                                                      { AOM_CDF2(22175), 0 },
                                                      { AOM_CDF2(16384), 0 },
                                                    };
#endif

#if CONFIG_LOSSLESS_DPCM
static const aom_cdf_prob default_dpcm_cdf[CDF_SIZE(2)] = { AOM_CDF2(16384) };
static const aom_cdf_prob default_dpcm_vert_horz_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16384) };
static const aom_cdf_prob default_dpcm_uv_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16384) };
static const aom_cdf_prob default_dpcm_uv_vert_horz_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16384)
};
#endif  // CONFIG_LOSSLESS_DPCM

#if CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENTROPY_PARA
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
#else
static const aom_cdf_prob
    default_fsc_mode_cdf[FSC_MODE_CONTEXTS][FSC_BSIZE_CONTEXTS]
                        [CDF_SIZE(FSC_MODES)] = { { { AOM_CDF2(29360) },
                                                    { AOM_CDF2(31501) },
                                                    { AOM_CDF2(32278) },
                                                    { AOM_CDF2(32371) },
                                                    { AOM_CDF2(32560) },
                                                    { AOM_CDF2(32531) } },
                                                  { { AOM_CDF2(24973) },
                                                    { AOM_CDF2(24385) },
                                                    { AOM_CDF2(24145) },
                                                    { AOM_CDF2(26258) },
                                                    { AOM_CDF2(21038) },
                                                    { AOM_CDF2(15313) } },
                                                  { { AOM_CDF2(20868) },
                                                    { AOM_CDF2(16117) },
                                                    { AOM_CDF2(12254) },
                                                    { AOM_CDF2(14424) },
                                                    { AOM_CDF2(5350) },
                                                    { AOM_CDF2(2348) } },
                                                  { { AOM_CDF2(31265) },
                                                    { AOM_CDF2(31284) },
                                                    { AOM_CDF2(32247) },
                                                    { AOM_CDF2(32253) },
                                                    { AOM_CDF2(32560) },
                                                    { AOM_CDF2(32533) } } };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob
    default_fsc_mode_cdf[FSC_MODE_CONTEXTS][FSC_BSIZE_CONTEXTS]
                        [CDF_SIZE(FSC_MODES)] = { { { AOM_CDF2(29656) },
                                                    { AOM_CDF2(31950) },
                                                    { AOM_CDF2(32056) },
                                                    { AOM_CDF2(32483) },
                                                    { AOM_CDF2(32320) } },
                                                  { { AOM_CDF2(24381) },
                                                    { AOM_CDF2(28062) },
                                                    { AOM_CDF2(21473) },
                                                    { AOM_CDF2(28418) },
                                                    { AOM_CDF2(14016) } },
                                                  { { AOM_CDF2(19188) },
                                                    { AOM_CDF2(22942) },
                                                    { AOM_CDF2(8388) },
                                                    { AOM_CDF2(20964) },
                                                    { AOM_CDF2(1235) } },
                                                  { { AOM_CDF2(29238) },
                                                    { AOM_CDF2(30676) },
                                                    { AOM_CDF2(31947) },
                                                    { AOM_CDF2(32203) },
                                                    { AOM_CDF2(32283) } } };
#endif  // CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENABLE_MHCCP

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_cfl_index_cdf[CDF_SIZE(
    CFL_TYPE_COUNT - 1)] = { AOM_CDF3(4124, 16615), 75 };
#else
static const aom_cdf_prob default_cfl_index_cdf[CDF_SIZE(CFL_TYPE_COUNT)] = {
  AOM_CDF4(18000, 24000, 29000)
};
#endif  // CONFIG_ENTROPY_PARA

#else
static const aom_cdf_prob default_cfl_index_cdf[CDF_SIZE(CFL_TYPE_COUNT)] = {
  AOM_CDF2(18000), 0
};
#endif  // CONFIG_ENABLE_MHCCP
#if CONFIG_ENABLE_MHCCP
static const aom_cdf_prob default_filter_dir_cdf[MHCCP_CONTEXT_GROUP_SIZE]
                                                [CDF_SIZE(MHCCP_MODE_NUM)] = {
                                                  { AOM_CDF2(13909), 1 },
                                                  { AOM_CDF2(8925), 76 },
                                                  { AOM_CDF2(4205), 1 },
                                                  { AOM_CDF2(5225), 6 },
                                                  { AOM_CDF2(6694), 31 },
                                                  { AOM_CDF2(9557), 32 },
                                                  { AOM_CDF2(16384), 32 },
                                                };
#endif  // CONFIG_ENABLE_MHCCP
#if CONFIG_AIMC
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_y_mode_set_cdf[CDF_SIZE(INTRA_MODE_SETS)] = {
  AOM_CDF4(28618, 30909, 31555), 118
};

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

static const aom_cdf_prob
    default_uv_mode_cdf[UV_MODE_CONTEXTS][CDF_SIZE(UV_INTRA_MODES - 1)] = {
      { AOM_CDF13(13848, 18930, 20641, 22133, 23986, 25450, 28075, 28950, 29740,
                  30647, 31182, 31880),
        0 },
      { AOM_CDF13(19268, 22648, 25651, 26449, 27288, 28840, 29451, 30120, 30622,
                  31606, 31844, 32144),
        0 },
    };
static const aom_cdf_prob default_cfl_cdf[CFL_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(18484), 6 },
  { AOM_CDF2(8591), 6 },
  { AOM_CDF2(2151), 0 },
};
#else
static const aom_cdf_prob default_y_mode_set_cdf[CDF_SIZE(INTRA_MODE_SETS)] = {
  AOM_CDF4(28000, 30600, 31400)
};
static const aom_cdf_prob
    default_y_first_mode_cdf[Y_MODE_CONTEXTS][CDF_SIZE(FIRST_MODE_COUNT)] = {
      { AOM_CDF13(13000, 18000, 20000, 22000, 24000, 25000, 26000, 27000, 28000,
                  29000, 30000, 31000) },
      { AOM_CDF13(10000, 15000, 17000, 19000, 20000, 25000, 26000, 27000, 28000,
                  29000, 30000, 31000) },
      { AOM_CDF13(7000, 12000, 14000, 16000, 17000, 22000, 26000, 27000, 28000,
                  29000, 30000, 31000) }
    };
static const aom_cdf_prob
    default_y_second_mode_cdf[Y_MODE_CONTEXTS][CDF_SIZE(SECOND_MODE_COUNT)] = {
      { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384, 18432,
                  20480, 22528, 24576, 26624, 28672, 30720) },
      { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384, 18432,
                  20480, 22528, 24576, 26624, 28672, 30720) },
      { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384, 18432,
                  20480, 22528, 24576, 26624, 28672, 30720) }
    };
static const aom_cdf_prob
    default_uv_mode_cdf[UV_MODE_CONTEXTS][CDF_SIZE(UV_INTRA_MODES - 1)] = {
      { AOM_CDF13(20545, 22597, 24087, 24753, 24995, 25621, 26273, 27089, 28510,
                  29888, 31389, 32041) },
      { AOM_CDF13(5917, 11538, 16087, 17200, 18154, 19802, 21631, 23074, 24491,
                  29013, 29694, 30641) }
    };
static const aom_cdf_prob default_cfl_cdf[CFL_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(16384) },
  { AOM_CDF2(12384) },
  { AOM_CDF2(6384) },
};
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob default_angle_delta_cdf
    [PARTITION_STRUCTURE_NUM][DIRECTIONAL_MODES]
    [CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)] = {
      { { AOM_CDF7(2180, 5032, 7567, 22776, 26989, 30217) },
        { AOM_CDF7(2301, 5608, 8801, 23487, 26974, 30330) },
        { AOM_CDF7(3780, 11018, 13699, 19354, 23083, 31286) },
        { AOM_CDF7(4581, 11226, 15147, 17138, 21834, 28397) },
        { AOM_CDF7(1737, 10927, 14509, 19588, 22745, 28823) },
        { AOM_CDF7(2664, 10176, 12485, 17650, 21600, 30495) },
        { AOM_CDF7(2240, 11096, 15453, 20341, 22561, 28917) },
        { AOM_CDF7(3605, 10428, 12459, 17676, 21244, 30655) } },
      { { AOM_CDF7(2180, 5032, 7567, 22776, 26989, 30217) },
        { AOM_CDF7(2301, 5608, 8801, 23487, 26974, 30330) },
        { AOM_CDF7(3780, 11018, 13699, 19354, 23083, 31286) },
        { AOM_CDF7(4581, 11226, 15147, 17138, 21834, 28397) },
        { AOM_CDF7(1737, 10927, 14509, 19588, 22745, 28823) },
        { AOM_CDF7(2664, 10176, 12485, 17650, 21600, 30495) },
        { AOM_CDF7(2240, 11096, 15453, 20341, 22561, 28917) },
        { AOM_CDF7(3605, 10428, 12459, 17676, 21244, 30655) } }
    };

static const aom_cdf_prob default_if_y_mode_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(
    INTRA_MODES)] = { { AOM_CDF13(22801, 23489, 24293, 24756, 25601, 26123,
                                  26606, 27418, 27945, 29228, 29685, 30349) },
                      { AOM_CDF13(18673, 19845, 22631, 23318, 23950, 24649,
                                  25527, 27364, 28152, 29701, 29984, 30852) },
                      { AOM_CDF13(19770, 20979, 23396, 23939, 24241, 24654,
                                  25136, 27073, 27830, 29360, 29730, 30659) },
                      { AOM_CDF13(20155, 21301, 22838, 23178, 23261, 23533,
                                  23703, 24804, 25352, 26575, 27016, 28049) } };

static const aom_cdf_prob
    default_uv_mode_cdf[CFL_ALLOWED_TYPES][INTRA_MODES][CDF_SIZE(
        UV_INTRA_MODES)] = {
      { { AOM_CDF13(22631, 24152, 25378, 25661, 25986, 26520, 27055, 27923,
                    28244, 30059, 30941, 31961) },
        { AOM_CDF13(9513, 26881, 26973, 27046, 27118, 27664, 27739, 27824,
                    28359, 29505, 29800, 31796) },
        { AOM_CDF13(9845, 9915, 28663, 28704, 28757, 28780, 29198, 29822, 29854,
                    30764, 31777, 32029) },
        { AOM_CDF13(13639, 13897, 14171, 25331, 25606, 25727, 25953, 27148,
                    28577, 30612, 31355, 32493) },
        { AOM_CDF13(9764, 9835, 9930, 9954, 25386, 27053, 27958, 28148, 28243,
                    31101, 31744, 32363) },
        { AOM_CDF13(11825, 13589, 13677, 13720, 15048, 29213, 29301, 29458,
                    29711, 31161, 31441, 32550) },
        { AOM_CDF13(14175, 14399, 16608, 16821, 17718, 17775, 28551, 30200,
                    30245, 31837, 32342, 32667) },
        { AOM_CDF13(12885, 13038, 14978, 15590, 15673, 15748, 16176, 29128,
                    29267, 30643, 31961, 32461) },
        { AOM_CDF13(12026, 13661, 13874, 15305, 15490, 15726, 15995, 16273,
                    28443, 30388, 30767, 32416) },
        { AOM_CDF13(19052, 19840, 20579, 20916, 21150, 21467, 21885, 22719,
                    23174, 28861, 30379, 32175) },
        { AOM_CDF13(18627, 19649, 20974, 21219, 21492, 21816, 22199, 23119,
                    23527, 27053, 31397, 32148) },
        { AOM_CDF13(17026, 19004, 19997, 20339, 20586, 21103, 21349, 21907,
                    22482, 25896, 26541, 31819) },
        { AOM_CDF13(12124, 13759, 14959, 14992, 15007, 15051, 15078, 15166,
                    15255, 15753, 16039, 16606) } },
      { { AOM_CDF14(10407, 11208, 12900, 13181, 13823, 14175, 14899, 15656,
                    15986, 20086, 20995, 22455, 24212) },
        { AOM_CDF14(4532, 19780, 20057, 20215, 20428, 21071, 21199, 21451,
                    22099, 24228, 24693, 27032, 29472) },
        { AOM_CDF14(5273, 5379, 20177, 20270, 20385, 20439, 20949, 21695, 21774,
                    23138, 24256, 24703, 26679) },
        { AOM_CDF14(6740, 7167, 7662, 14152, 14536, 14785, 15034, 16741, 18371,
                    21520, 22206, 23389, 24182) },
        { AOM_CDF14(4987, 5368, 5928, 6068, 19114, 20315, 21857, 22253, 22411,
                    24911, 25380, 26027, 26376) },
        { AOM_CDF14(5370, 6889, 7247, 7393, 9498, 21114, 21402, 21753, 21981,
                    24780, 25386, 26517, 27176) },
        { AOM_CDF14(4816, 4961, 7204, 7326, 8765, 8930, 20169, 20682, 20803,
                    23188, 23763, 24455, 24940) },
        { AOM_CDF14(6608, 6740, 8529, 9049, 9257, 9356, 9735, 18827, 19059,
                    22336, 23204, 23964, 24793) },
        { AOM_CDF14(5998, 7419, 7781, 8933, 9255, 9549, 9753, 10417, 18898,
                    22494, 23139, 24764, 25989) },
        { AOM_CDF14(10660, 11298, 12550, 12957, 13322, 13624, 14040, 15004,
                    15534, 20714, 21789, 23443, 24861) },
        { AOM_CDF14(10522, 11530, 12552, 12963, 13378, 13779, 14245, 15235,
                    15902, 20102, 22696, 23774, 25838) },
        { AOM_CDF14(10099, 10691, 12639, 13049, 13386, 13665, 14125, 15163,
                    15636, 19676, 20474, 23519, 25208) },
        { AOM_CDF14(3144, 5087, 7382, 7504, 7593, 7690, 7801, 8064, 8232, 9248,
                    9875, 10521, 29048) } }
    };
#endif  // CONFIG_AIMC

#if CONFIG_EXTENDED_SDP
static aom_cdf_prob default_region_type_cdf[INTER_SDP_BSIZE_GROUP]
                                           [CDF_SIZE(REGION_TYPES)] = {
#if CONFIG_EXTENDED_SDP_64x64
                                             // w * h <= 128
                                             { AOM_CDF2(8192), 0 },
                                             // w * h <= 512
                                             { AOM_CDF2(8192), 0 },
                                             // w * h <= 1024
                                             { AOM_CDF2(8192), 0 },
                                             // w * h <= 4096
                                             { AOM_CDF2(8192), 0 },
#else
                                             // w * h <= 64
                                             { AOM_CDF2(16384), 0 },
                                             // w * h <= 128
                                             { AOM_CDF2(16384), 0 },
                                             // w * h <= 256
                                             { AOM_CDF2(16384), 0 },
                                             // w * h <= 512
                                             { AOM_CDF2(16384), 0 },
                                             // w * h <= 1024
                                             { AOM_CDF2(16384), 0 }
#endif  // CONFIG_EXTENDED_SDP_64x64
                                           };
#endif  // CONFIG_EXTENDED_SDP
#if CONFIG_EXT_RECUR_PARTITIONS
// clang-format off
#if CONFIG_PARTITION_CONTEXT_REDUCE
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

#if CONFIG_RECT_CTX
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
#else
static aom_cdf_prob default_rect_type_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
  {
    { AOM_CDF2(16587),  75 },
    { AOM_CDF2(15076),  75 },
    { AOM_CDF2(16875),  90 },
    { AOM_CDF2(15489),  93 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16454),  45 },
    { AOM_CDF2( 8282),  25 },
    { AOM_CDF2(24832),  47 },
    { AOM_CDF2(16451),  90 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16764),   7 },
    { AOM_CDF2(10266),   1 },
    { AOM_CDF2(23603),  34 },
    { AOM_CDF2(15865),   3 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(18831),  30 },
    { AOM_CDF2(13663),  45 },
    { AOM_CDF2(24819),  49 },
    { AOM_CDF2(21622),  35 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(17066),   3 },
    { AOM_CDF2(10496),   1 },
    { AOM_CDF2(20321),   2 },
    { AOM_CDF2(11621),   1 },
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
    { AOM_CDF2(16384),  25 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(29778),   6 },
    { AOM_CDF2(21380),   1 },
    { AOM_CDF2(31867),   0 },
    { AOM_CDF2(29040),  76 },
    { AOM_CDF2( 1883),   1 },
    { AOM_CDF2(  679),  90 },
    { AOM_CDF2( 7054),   6 },
    { AOM_CDF2( 1518),  75 },
    { AOM_CDF2(23684),   7 },
    { AOM_CDF2(18105),   6 },
    { AOM_CDF2(30326),  76 },
    { AOM_CDF2(28709),   7 },
    { AOM_CDF2( 6363),   7 },
    { AOM_CDF2(  709),  90 },
    { AOM_CDF2( 9326),  37 },
    { AOM_CDF2(  588),  76 },
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
    { AOM_CDF2(16147),  90 },
    { AOM_CDF2(12502),  90 },
    { AOM_CDF2(20013),  76 },
    { AOM_CDF2(16275),  75 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16444),  34 },
    { AOM_CDF2(10647),  32 },
    { AOM_CDF2(22549),  50 },
    { AOM_CDF2(16140),  95 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16892),   4 },
    { AOM_CDF2( 8890),  26 },
    { AOM_CDF2(25109),  55 },
    { AOM_CDF2(15109),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(16384),   0 },
    { AOM_CDF2(21210),  31 },
    { AOM_CDF2(12880),   0 },
    { AOM_CDF2(29030),   2 },
    { AOM_CDF2(23562),  32 },
    { AOM_CDF2( 7522),   7 },
    { AOM_CDF2( 2159),  76 },
    { AOM_CDF2(12307),  31 },
    { AOM_CDF2( 3030),   6 },
  },
};
#endif

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
#else
static aom_cdf_prob
    default_do_split_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
      // Luma
      {
        // BLOCK_4X4
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_4X8
        { AOM_CDF2(28194),   0 },
        { AOM_CDF2(26594),  75 },
        { AOM_CDF2(24734),   0 },
        { AOM_CDF2(22454),   0 },
        // BLOCK_8X4
        { AOM_CDF2(27954),   0 },
        { AOM_CDF2(23649),   0 },
        { AOM_CDF2(26002),  75 },
        { AOM_CDF2(20643),   0 },
        // BLOCK_8X8
        { AOM_CDF2(24288),   1 },
        { AOM_CDF2(13221),   0 },
        { AOM_CDF2(12754),   1 },
        { AOM_CDF2( 7261),   0 },
        // BLOCK_8x16
        { AOM_CDF2(24132),   1 },
        { AOM_CDF2(15340),   1 },
        { AOM_CDF2(14348),   1 },
        { AOM_CDF2( 6511),   1 },
        // BLOCK_16X8
        { AOM_CDF2(20945),   1 },
        { AOM_CDF2( 9504),   1 },
        { AOM_CDF2(11479),   1 },
        { AOM_CDF2( 3562),   0 },
        // BLOCK_16x16
        { AOM_CDF2(19951),   1 },
        { AOM_CDF2( 7293),   1 },
        { AOM_CDF2( 7619),   1 },
        { AOM_CDF2( 2157),  75 },
        // BLOCK_16X32
        { AOM_CDF2(21508),  75 },
        { AOM_CDF2( 9317),  76 },
        { AOM_CDF2(10683),   1 },
        { AOM_CDF2( 2709),   0 },
        // BLOCK_32X16
        { AOM_CDF2(18209),   1 },
        { AOM_CDF2( 5879),   1 },
        { AOM_CDF2( 6560),  76 },
        { AOM_CDF2( 1174),  90 },
        // BLOCK_32X32
        { AOM_CDF2(21089),   1 },
        { AOM_CDF2( 6150),   1 },
        { AOM_CDF2( 6826),   1 },
        { AOM_CDF2( 1202),  75 },
        // BLOCK_32X64
        { AOM_CDF2(21366),  76 },
        { AOM_CDF2( 8824),  76 },
        { AOM_CDF2(12030),   6 },
        { AOM_CDF2( 2371),   1 },
        // BLOCK_64X32
        { AOM_CDF2(17649),  76 },
        { AOM_CDF2( 4196),   1 },
        { AOM_CDF2( 4840),  76 },
        { AOM_CDF2(  531),  93 },
        // BLOCK_64X64
        { AOM_CDF2(17354),  76 },
        { AOM_CDF2( 4849),   1 },
        { AOM_CDF2( 5162),   1 },
        { AOM_CDF2(  639), 115 },
        // BLOCK_64X128
        { AOM_CDF2(25198),  80 },
        { AOM_CDF2(10238),  20 },
        { AOM_CDF2(22876),  98 },
        { AOM_CDF2( 8560),  22 },
        // BLOCK_128X64
        { AOM_CDF2(26996), 107 },
        { AOM_CDF2(21770),  12 },
        { AOM_CDF2(11453),  85 },
        { AOM_CDF2( 6203),  60 },
        // BLOCK_128X128
        { AOM_CDF2(17248),   2 },
        { AOM_CDF2( 5306),  37 },
        { AOM_CDF2( 3947),   2 },
        { AOM_CDF2(  506),  78 },
        // BLOCK_128X256
        { AOM_CDF2(25796), 120 },
        { AOM_CDF2(11229),   0 },
        { AOM_CDF2(19151), 120 },
        { AOM_CDF2( 4994),  75 },
        // BLOCK_256X128
        { AOM_CDF2(23826), 109 },
        { AOM_CDF2(20220), 120 },
        { AOM_CDF2( 5605),   0 },
        { AOM_CDF2( 2749), 100 },
        // BLOCK_256X256
        { AOM_CDF2(19297),   0 },
        { AOM_CDF2( 3302),  25 },
        { AOM_CDF2( 3314),   0 },
        { AOM_CDF2(  356), 110 },
#if CONFIG_CB1TO4_SPLIT
        // BLOCK_4X16
        { AOM_CDF2(27089),  75 },
        { AOM_CDF2(24922),  75 },
        { AOM_CDF2(21953),  75 },
        { AOM_CDF2(18174),  75 },
        // BLOCK_16X4
        { AOM_CDF2(25865),  75 },
        { AOM_CDF2(20645),  75 },
        { AOM_CDF2(23818),  75 },
        { AOM_CDF2(16997),   0 },
        // BLOCK_8X32
        { AOM_CDF2(23817),  75 },
        { AOM_CDF2(13474),   0 },
        { AOM_CDF2(11862),   1 },
        { AOM_CDF2( 3226),   0 },
        // BLOCK_32X8
        { AOM_CDF2(22226),   1 },
        { AOM_CDF2( 7562),   1 },
        { AOM_CDF2( 9657),   1 },
        { AOM_CDF2( 1002),  75 },
        // BLOCK_16X64
        { AOM_CDF2(22117),  75 },
        { AOM_CDF2(10947),   1 },
        { AOM_CDF2( 8769),   1 },
        { AOM_CDF2( 2299),   0 },
        // BLOCK_64X16
        { AOM_CDF2(22152),   6 },
        { AOM_CDF2( 3618),   1 },
        { AOM_CDF2( 9667),   1 },
        { AOM_CDF2(  425),  90 },
#endif  // CONFIG_CB1TO4_SPLIT
      },
      // Chroma
      {
        // BLOCK_4X4
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_4X8
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X4
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X8
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8x16
        { AOM_CDF2(26672),   0 },
        { AOM_CDF2(16384),   0 },
        { AOM_CDF2(20710),   0 },
        { AOM_CDF2(16384),   0 },
        // BLOCK_16X8
        { AOM_CDF2(26587),   0 },
        { AOM_CDF2(22028),   0 },
        { AOM_CDF2(16384),   0 },
        { AOM_CDF2(16384),   0 },
        // BLOCK_16x16
        { AOM_CDF2(22192),   0 },
        { AOM_CDF2(11412),   0 },
        { AOM_CDF2(11721),  75 },
        { AOM_CDF2( 6263),  90 },
        // BLOCK_16X32
        { AOM_CDF2(23832),   1 },
        { AOM_CDF2(11924),  90 },
        { AOM_CDF2(14591),   6 },
        { AOM_CDF2( 6202),   0 },
        // BLOCK_32X16
        { AOM_CDF2(23116),  31 },
        { AOM_CDF2(12716),  31 },
        { AOM_CDF2(10065),   0 },
        { AOM_CDF2( 4723),  90 },
        // BLOCK_32X32
        { AOM_CDF2(29561),  31 },
        { AOM_CDF2(11499),   1 },
        { AOM_CDF2(10640),   6 },
        { AOM_CDF2( 2921),   0 },
        // BLOCK_32X64
        { AOM_CDF2(15621),   1 },
        { AOM_CDF2( 7058),   8 },
        { AOM_CDF2( 5046),   6 },
        { AOM_CDF2( 1287),   0 },
        // BLOCK_64X32
        { AOM_CDF2( 7706),  32 },
        { AOM_CDF2( 1091),   0 },
        { AOM_CDF2( 1637),  76 },
        { AOM_CDF2(  325),  99 },
        // BLOCK_64X64
        { AOM_CDF2(20097),  32 },
        { AOM_CDF2( 6851),   7 },
        { AOM_CDF2( 5020),   1 },
        { AOM_CDF2(  909),  75 },
        // BLOCK_64X128
        { AOM_CDF2(19587), 107 },
        { AOM_CDF2( 9322), 110 },
        { AOM_CDF2(19769),   5 },
        { AOM_CDF2( 9100),   8 },
        // BLOCK_128X64
        { AOM_CDF2(24402),  37 },
        { AOM_CDF2(19136),  35 },
        { AOM_CDF2( 8316), 110 },
        { AOM_CDF2( 4291),  60 },
        // BLOCK_128X128
        { AOM_CDF2(27951),   1 },
        { AOM_CDF2(15351),  35 },
        { AOM_CDF2(10542),  33 },
        { AOM_CDF2( 2947),   7 },
        // BLOCK_128X256
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_256X128
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_256X256
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
        // BLOCK_4X16,
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_16X4,
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X32,
        { AOM_CDF2(26812),   0 },
        { AOM_CDF2(16384),   0 },
        { AOM_CDF2(17291),   1 },
        { AOM_CDF2(16384),   0 },
        // BLOCK_32X8,
        { AOM_CDF2(25836),   0 },
        { AOM_CDF2(17434),   1 },
        { AOM_CDF2(16384),   0 },
        { AOM_CDF2(16384),   0 },
        // BLOCK_16X64,
        { AOM_CDF2(23234),   1 },
        { AOM_CDF2(12187),   3 },
        { AOM_CDF2(13025),  32 },
        { AOM_CDF2( 4418),   0 },
        // BLOCK_64X16,
        { AOM_CDF2(21041),  31 },
        { AOM_CDF2( 7352),  31 },
        { AOM_CDF2( 9962),   6 },
        { AOM_CDF2( 1922),   0 },
#endif  // CONFIG_CB1TO4_SPLIT
      }
    };

static aom_cdf_prob
    default_rect_type_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][CDF_SIZE(2)] = {
      // Luma
      {
        // BLOCK_4X4
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_4X8
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X4
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X8
        { AOM_CDF2(17405),  75 },
        { AOM_CDF2(13772),  75 },
        { AOM_CDF2(21881), 115 },
        { AOM_CDF2(19500), 115 },
        // BLOCK_8x16
        { AOM_CDF2(22464),   1 },
        { AOM_CDF2(17171),   1 },
        { AOM_CDF2(28223),   0 },
        { AOM_CDF2(26546),   1 },
        // BLOCK_16X8
        { AOM_CDF2( 6820),  76 },
        { AOM_CDF2( 2633),  75 },
        { AOM_CDF2(10735),  77 },
        { AOM_CDF2( 3775),  90 },
        // BLOCK_16x16
        { AOM_CDF2(18040),   1 },
        { AOM_CDF2(10648),   1 },
        { AOM_CDF2(25993),  75 },
        { AOM_CDF2(22265),  76 },
        // BLOCK_16X32
        { AOM_CDF2(21076),   1 },
        { AOM_CDF2(17097),   1 },
        { AOM_CDF2(29285),   1 },
        { AOM_CDF2(28199),   1 },
        // BLOCK_32X16
        { AOM_CDF2( 6588),  76 },
        { AOM_CDF2( 1588),  90 },
        { AOM_CDF2( 9365),   7 },
        { AOM_CDF2( 1930),  75 },
        // BLOCK_32X32
        { AOM_CDF2(19224),  76 },
        { AOM_CDF2(13869),   1 },
        { AOM_CDF2(26476), 115 },
        { AOM_CDF2(24567),  75 },
        // BLOCK_32X64
        { AOM_CDF2(20266),   7 },
        { AOM_CDF2(18322),   6 },
        { AOM_CDF2(28636),   1 },
        { AOM_CDF2(29270),   1 },
        // BLOCK_64X32
        { AOM_CDF2( 6131),   6 },
        { AOM_CDF2( 1064), 115 },
        { AOM_CDF2( 9671),  37 },
        { AOM_CDF2( 1050),   0 },
        // BLOCK_64X64
        { AOM_CDF2(20007),   7 },
        { AOM_CDF2(17698),   1 },
        { AOM_CDF2(26666),  76 },
        { AOM_CDF2(26592),  75 },
        // BLOCK_64X128
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_128X64
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_128X128
        { AOM_CDF2(18109),  90 },
        { AOM_CDF2(10515),   0 },
        { AOM_CDF2(22879),  15 },
        { AOM_CDF2(14385),  12 },
        // BLOCK_128X256
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_256X128
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_256X256
        { AOM_CDF2(18966),  75 },
        { AOM_CDF2(14351),   0 },
        { AOM_CDF2(23553),  10 },
        { AOM_CDF2(19681),  25 },
#if CONFIG_CB1TO4_SPLIT
        // BLOCK_4X16,
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_16X4,
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X32,
        { AOM_CDF2(29885),   6 },
        { AOM_CDF2(22637),   1 },
        { AOM_CDF2(31974),  75 },
        { AOM_CDF2(30175),   1 },
        // BLOCK_32X8,
        { AOM_CDF2( 1593),  76 },
        { AOM_CDF2(  569),  90 },
        { AOM_CDF2( 5434),   1 },
        { AOM_CDF2( 1025),  75 },
        // BLOCK_16X64,
        { AOM_CDF2(24340),   7 },
        { AOM_CDF2(19892),  31 },
        { AOM_CDF2(30807),  76 },
        { AOM_CDF2(29855),   6 },
        // BLOCK_64X16,
        { AOM_CDF2( 5948),   7 },
        { AOM_CDF2(  643),  90 },
        { AOM_CDF2( 7712),  37 },
        { AOM_CDF2(  438),  75 },
#endif  // CONFIG_CB1TO4_SPLIT
      },
      // Chroma
      {
        // BLOCK_4X4
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_4X8
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X4
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X8
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8x16
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_16X8
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_16x16
        { AOM_CDF2(17466),  75 },
        { AOM_CDF2(11461),  75 },
        { AOM_CDF2(23069),  90 },
        { AOM_CDF2(17575),  90 },
        // BLOCK_16X32
        { AOM_CDF2(21500),   1 },
        { AOM_CDF2(16941),   1 },
        { AOM_CDF2(28190),  75 },
        { AOM_CDF2(25590),   1 },
        // BLOCK_32X16
        { AOM_CDF2(12489),   2 },
        { AOM_CDF2( 4568),  76 },
        { AOM_CDF2(17792),   7 },
        { AOM_CDF2( 7495),  76 },
        // BLOCK_32X32
        { AOM_CDF2(19912),  76 },
        { AOM_CDF2(13861),  76 },
        { AOM_CDF2(26203),  76 },
        { AOM_CDF2(21835),  76 },
        // BLOCK_32X64
        { AOM_CDF2(26349),   4 },
        { AOM_CDF2(18909),  45 },
        { AOM_CDF2(31145),   1 },
        { AOM_CDF2(29434),   6 },
        // BLOCK_64X32
        { AOM_CDF2( 1359),   1 },
        { AOM_CDF2(  346), 118 },
        { AOM_CDF2(11224),  37 },
        { AOM_CDF2( 1533),  76 },
        // BLOCK_64X64
        { AOM_CDF2(22373),   5 },
        { AOM_CDF2(22200),   6 },
        { AOM_CDF2(27751),   1 },
        { AOM_CDF2(26876),   0 },
        // BLOCK_64X128
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_128X64
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_128X128
        { AOM_CDF2(13754),  25 },
        { AOM_CDF2( 8131),   0 },
        { AOM_CDF2(19409),  45 },
        { AOM_CDF2( 8887),  35 },
        // BLOCK_128X256
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_256X128
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_256X256
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
        // BLOCK_4X16,
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_16X4,
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_8X32,
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_32X8,
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        { AOM_CDF2(16384),   0 },  // unused entry
        // BLOCK_16X64,
        { AOM_CDF2(20421),  32 },
        { AOM_CDF2(13087),  32 },
        { AOM_CDF2(28719),   2 },
        { AOM_CDF2(24110),  32 },
        // BLOCK_64X16,
        { AOM_CDF2( 7423),   7 },
        { AOM_CDF2( 1984),   0 },
        { AOM_CDF2(11758),  32 },
        { AOM_CDF2( 2535),   6 },
#endif  // CONFIG_CB1TO4_SPLIT
      }
    };

// Note: For the partition CDFs below, most entries are unused. An optimized
// implementation could create smaller arrays with only used values + some
// mapping tables.
static aom_cdf_prob default_do_ext_partition_cdf
    [PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS][PARTITION_CONTEXTS]
    [CDF_SIZE(2)] = {
      // Luma
      {
        // HORZ
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(28126),   0 },
          { AOM_CDF2(24398),  75 },
          { AOM_CDF2(23529),   1 },
          { AOM_CDF2(18352),  90 },
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(30732), 115 },
          { AOM_CDF2(30747), 118 },
          { AOM_CDF2(30203), 118 },
          { AOM_CDF2(30752), 118 },
          // BLOCK_16X32
          { AOM_CDF2(27439),  75 },
          { AOM_CDF2(26381),  75 },
          { AOM_CDF2(20472),  75 },
          { AOM_CDF2(19396),  90 },
          // BLOCK_32X16
          { AOM_CDF2(29051),  76 },
          { AOM_CDF2(26472),  75 },
          { AOM_CDF2(26906),   6 },
          { AOM_CDF2(22036),   7 },
          // BLOCK_32X32
          { AOM_CDF2(30744),  90 },
          { AOM_CDF2(31624), 115 },
          { AOM_CDF2(29903),  90 },
          { AOM_CDF2(31718), 115 },
          // BLOCK_32X64
          { AOM_CDF2(26575),  15 },
          { AOM_CDF2(25376),  92 },
          { AOM_CDF2(19834),  76 },
          { AOM_CDF2(17616),  93 },
          // BLOCK_64X32
          { AOM_CDF2(27828),  13 },
          { AOM_CDF2(24853),  30 },
          { AOM_CDF2(27145),  37 },
          { AOM_CDF2(20656),  37 },
          // BLOCK_64X64
          { AOM_CDF2(31215), 119 },
          { AOM_CDF2(31941), 117 },
          { AOM_CDF2(30679),  90 },
          { AOM_CDF2(32244), 115 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(30336),   6 },
          { AOM_CDF2(22571),   6 },
          { AOM_CDF2(27893),   6 },
          { AOM_CDF2(15963),   1 },
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(25754),   1 },
          { AOM_CDF2(21089),  21 },
          { AOM_CDF2(21108),   1 },
          { AOM_CDF2(15907),  75 },
          // BLOCK_64X16,
          { AOM_CDF2(31083),  31 },
          { AOM_CDF2(30314),  30 },
          { AOM_CDF2(30676),  31 },
          { AOM_CDF2(28363),  37 },
#endif  // CONFIG_CB1TO4_SPLIT
        },
        // VERT
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(30150),  90 },
          { AOM_CDF2(28639),  75 },
          { AOM_CDF2(27384),  90 },
          { AOM_CDF2(25595),  93 },
          // BLOCK_16x16
          { AOM_CDF2(31199), 123 },
          { AOM_CDF2(30494), 118 },
          { AOM_CDF2(29694), 123 },
          { AOM_CDF2(29315), 123 },
          // BLOCK_16X32
          { AOM_CDF2(30436),   0 },
          { AOM_CDF2(27853),   0 },
          { AOM_CDF2(28515),   6 },
          { AOM_CDF2(24402),   0 },
          // BLOCK_32X16
          { AOM_CDF2(29006),  90 },
          { AOM_CDF2(25918), 118 },
          { AOM_CDF2(27758), 118 },
          { AOM_CDF2(26233), 118 },
          // BLOCK_32X32
          { AOM_CDF2(30119),  90 },
          { AOM_CDF2(27654),  75 },
          { AOM_CDF2(29837),  90 },
          { AOM_CDF2(29583),  90 },
          // BLOCK_32X64
          { AOM_CDF2(28069),  44 },
          { AOM_CDF2(24273),  31 },
          { AOM_CDF2(27312),  20 },
          { AOM_CDF2(22205),  25 },
          // BLOCK_64X32
          { AOM_CDF2(29637), 115 },
          { AOM_CDF2(27709), 115 },
          { AOM_CDF2(29419), 115 },
          { AOM_CDF2(28775), 118 },
          // BLOCK_64X64
          { AOM_CDF2(28966),  90 },
          { AOM_CDF2(27455),  91 },
          { AOM_CDF2(29656), 104 },
          { AOM_CDF2(30158),  76 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(30555),  76 },
          { AOM_CDF2(26165),   0 },
          { AOM_CDF2(24481),  75 },
          { AOM_CDF2(20123),  90 },
          // BLOCK_16X64,
          { AOM_CDF2(30695),  25 },
          { AOM_CDF2(29311),  27 },
          { AOM_CDF2(30088),  35 },
          { AOM_CDF2(29580),  30 },
          // BLOCK_64X16,
          { AOM_CDF2(27840),  76 },
          { AOM_CDF2(22490),  90 },
          { AOM_CDF2(25067),  91 },
          { AOM_CDF2(23393),  90 },
#endif  // CONFIG_CB1TO4_SPLIT
        }
      },
      // Chroma
      {
        // HORZ
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(23344),  75 },
          { AOM_CDF2(21307), 124 },
          { AOM_CDF2(20958),  75 },
          { AOM_CDF2(18495), 123 },
          // BLOCK_32X16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X32
          { AOM_CDF2(29125), 115 },
          { AOM_CDF2(29708), 115 },
          { AOM_CDF2(29515), 118 },
          { AOM_CDF2(30210), 115 },
          // BLOCK_32X64
          { AOM_CDF2(24821),   4 },
          { AOM_CDF2(19830), 110 },
          { AOM_CDF2(14978),  75 },
          { AOM_CDF2(13689), 118 },
          // BLOCK_64X32
          { AOM_CDF2(20582),  30 },
          { AOM_CDF2(18547),  10 },
          { AOM_CDF2(28584),  37 },
          { AOM_CDF2(19540),  35 },
          // BLOCK_64X64
          { AOM_CDF2(29777), 109 },
          { AOM_CDF2(31484),  75 },
          { AOM_CDF2(30589),  75 },
          { AOM_CDF2(32081),  90 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
         // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(25761),   6 },
          { AOM_CDF2(20655), 109 },
          { AOM_CDF2(24773),   1 },
          { AOM_CDF2(19256),  78 },
          // BLOCK_64X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#endif  // CONFIG_CB1TO4_SPLIT
        },
        // VERT
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X16
          { AOM_CDF2(27168),  75 },
          { AOM_CDF2(26315),  75 },
          { AOM_CDF2(25707), 115 },
          { AOM_CDF2(24315), 115 },
          // BLOCK_32X32
          { AOM_CDF2(30010), 123 },
          { AOM_CDF2(29350), 123 },
          { AOM_CDF2(29797), 123 },
          { AOM_CDF2(29474), 123 },
          // BLOCK_32X64
          { AOM_CDF2(24177),  20 },
          { AOM_CDF2(22570),  35 },
          { AOM_CDF2(20114),  30 },
          { AOM_CDF2(15773),  30 },
          // BLOCK_64X32
          { AOM_CDF2(30934),   0 },
          { AOM_CDF2(29660),   0 },
          { AOM_CDF2(28397),  96 },
          { AOM_CDF2(26230), 123 },
          // BLOCK_64X64
          { AOM_CDF2(25575),  95 },
          { AOM_CDF2(25411),  94 },
          { AOM_CDF2(28377),  76 },
          { AOM_CDF2(30292),   6 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X16,
          { AOM_CDF2(27118),   1 },
          { AOM_CDF2(22793),   0 },
          { AOM_CDF2(24896),  76 },
          { AOM_CDF2(21952),  75 },
#endif  // CONFIG_CB1TO4_SPLIT
        }
      }
    };

static aom_cdf_prob default_do_uneven_4way_partition_cdf
    [PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS][PARTITION_CONTEXTS]
    [CDF_SIZE(2)] = {
      // Luma
      {
        // HORZ
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(20896),  91 },
          { AOM_CDF2(25423),  87 },
          { AOM_CDF2(18934),  76 },
          { AOM_CDF2(24233),  76 },
          // BLOCK_32X16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X32
          { AOM_CDF2(18886),   5 },
          { AOM_CDF2(19925),  75 },
          { AOM_CDF2(14534),   2 },
          { AOM_CDF2(15937),  12 },
          // BLOCK_32X64
          { AOM_CDF2(22179),   0 },
          { AOM_CDF2(25007),   0 },
          { AOM_CDF2(23004),   2 },
          { AOM_CDF2(28583),   1 },
          // BLOCK_64X32
          { AOM_CDF2(28837),   0 },
          { AOM_CDF2(27058),   0 },
          { AOM_CDF2(26110),   4 },
          { AOM_CDF2(25514),  35 },
          // BLOCK_64X64
          { AOM_CDF2(15474),  25 },
          { AOM_CDF2(17027),   0 },
          { AOM_CDF2(11547),  18 },
          { AOM_CDF2(13274),  10 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(18162),   2 },
          { AOM_CDF2(18940),  20 },
          { AOM_CDF2(13346),   1 },
          { AOM_CDF2(14703),  76 },
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(15035),  79 },
          { AOM_CDF2(17974),  14 },
          { AOM_CDF2(10818),   1 },
          { AOM_CDF2(12521),  75 },
          // BLOCK_64X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#endif  // CONFIG_CB1TO4_SPLIT
        },
        // VERT
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X16
          { AOM_CDF2(18404),  94 },
          { AOM_CDF2(13828),   1 },
          { AOM_CDF2(21848),  92 },
          { AOM_CDF2(21300),   1 },
          // BLOCK_32X32
          { AOM_CDF2(17164),   0 },
          { AOM_CDF2(12467),   7 },
          { AOM_CDF2(19002),   0 },
          { AOM_CDF2(15835),   7 },
          // BLOCK_32X64
          { AOM_CDF2(18611), 100 },
          { AOM_CDF2(22116),  99 },
          { AOM_CDF2(24382),  25 },
          { AOM_CDF2(23971),  20 },
          // BLOCK_64X32
          { AOM_CDF2(15558),   0 },
          { AOM_CDF2(13948),   7 },
          { AOM_CDF2(23894),   0 },
          { AOM_CDF2(25244),   1 },
          // BLOCK_64X64
          { AOM_CDF2(16931),  50 },
          { AOM_CDF2(15037),  31 },
          { AOM_CDF2(20386),  25 },
          { AOM_CDF2(18422),  35 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(13636),   6 },
          { AOM_CDF2( 9713),  75 },
          { AOM_CDF2(14907),   6 },
          { AOM_CDF2(10869),  75 },
          // BLOCK_16X64,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X16,
          { AOM_CDF2(12756),   2 },
          { AOM_CDF2( 7866),  75 },
          { AOM_CDF2(14524),  23 },
          { AOM_CDF2(10401),  75 },
#endif  // CONFIG_CB1TO4_SPLIT
        }
      },
      // Chroma
      {
        // HORZ
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X64
          { AOM_CDF2(23490), 120 },
          { AOM_CDF2(24611),   0 },
          { AOM_CDF2(21709),  76 },
          { AOM_CDF2(24109),  77 },
          // BLOCK_64X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X64
          { AOM_CDF2(26228),  75 },
          { AOM_CDF2(23938), 100 },
          { AOM_CDF2(21021),  20 },
          { AOM_CDF2(20071),   8 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(21664),  20 },
          { AOM_CDF2(22456), 100 },
          { AOM_CDF2(14064),  77 },
          { AOM_CDF2(15873),  80 },
          // BLOCK_64X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#endif  // CONFIG_CB1TO4_SPLIT
        },
        // VERT
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X32
          { AOM_CDF2(21146),  75 },
          { AOM_CDF2(18757),   2 },
          { AOM_CDF2(25409),  95 },
          { AOM_CDF2(22994),  76 },
          // BLOCK_64X64
          { AOM_CDF2(16091),  75 },
          { AOM_CDF2(14886),  20 },
          { AOM_CDF2(17656),  75 },
          { AOM_CDF2(15202),  15 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X16,
          { AOM_CDF2(16685),   8 },
          { AOM_CDF2(10836),  75 },
          { AOM_CDF2(17532),  98 },
          { AOM_CDF2(10826),  75 },
#endif  // CONFIG_CB1TO4_SPLIT
        }
      },
    };

static aom_cdf_prob default_uneven_4way_partition_type_cdf
    [PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS][PARTITION_CONTEXTS]
    [CDF_SIZE(NUM_UNEVEN_4WAY_PARTS)] = {
      // Luma
      {
        // HORZ
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(21740),  80 },
          { AOM_CDF2(21317), 100 },
          { AOM_CDF2(24285), 115 },
          { AOM_CDF2(24124), 124 },
          // BLOCK_32X16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X32
          { AOM_CDF2(19671),  75 },
          { AOM_CDF2(17030),  75 },
          { AOM_CDF2(20432),   7 },
          { AOM_CDF2(17357),  17 },
          // BLOCK_32X64
          { AOM_CDF2(21678), 100 },
          { AOM_CDF2(22846), 100 },
          { AOM_CDF2(23642),  80 },
          { AOM_CDF2(23686), 110 },
          // BLOCK_64X32
          { AOM_CDF2(20055),   0 },
          { AOM_CDF2(24000), 100 },
          { AOM_CDF2(17744),   0 },
          { AOM_CDF2(18144),  25 },
          // BLOCK_64X64
          { AOM_CDF2(19248),  25 },
          { AOM_CDF2(13999),  75 },
          { AOM_CDF2(19510),   5 },
          { AOM_CDF2(12879),  75 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(21110),  79 },
          { AOM_CDF2(21499),  75 },
          { AOM_CDF2(23865),  75 },
          { AOM_CDF2(22557),  79 },
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(22737),  95 },
          { AOM_CDF2(21673),   0 },
          { AOM_CDF2(25141),  77 },
          { AOM_CDF2(23727),  75 },
          // BLOCK_64X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#endif  // CONFIG_CB1TO4_SPLIT
        },
        // VERT
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X16
          { AOM_CDF2(21198), 100 },
          { AOM_CDF2(25264),  76 },
          { AOM_CDF2(21343),  95 },
          { AOM_CDF2(22641), 116 },
          // BLOCK_32X32
          { AOM_CDF2(19777),  75 },
          { AOM_CDF2(20825), 115 },
          { AOM_CDF2(17008),  75 },
          { AOM_CDF2(18355),  82 },
          // BLOCK_32X64
          { AOM_CDF2(22149),  75 },
          { AOM_CDF2(14263),  20 },
          { AOM_CDF2(23013),   0 },
          { AOM_CDF2(17554),  25 },
          // BLOCK_64X32
          { AOM_CDF2(22601), 100 },
          { AOM_CDF2(23769),  91 },
          { AOM_CDF2(19886),   0 },
          { AOM_CDF2(21578),  84 },
          // BLOCK_64X64
          { AOM_CDF2(20325),  75 },
          { AOM_CDF2(20781), 105 },
          { AOM_CDF2(17086),  75 },
          { AOM_CDF2(16419),   0 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(22586),  79 },
          { AOM_CDF2(26160),  90 },
          { AOM_CDF2(22454),  99 },
          { AOM_CDF2(25288),  90 },
          // BLOCK_16X64,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X16,
          { AOM_CDF2(23477),  95 },
          { AOM_CDF2(27490),  90 },
          { AOM_CDF2(23633),  95 },
          { AOM_CDF2(26356),  90 },
#endif  // CONFIG_CB1TO4_SPLIT
        }
      },
      // Chroma
      {
        // HORZ
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X64
          { AOM_CDF2(23000), 100 },
          { AOM_CDF2(22945), 100 },
          { AOM_CDF2(21130),  16 },
          { AOM_CDF2(21467), 115 },
          // BLOCK_64X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X64
          { AOM_CDF2(19251),  75 },
          { AOM_CDF2(13198),  25 },
          { AOM_CDF2(12824),  75 },
          { AOM_CDF2(12472),  95 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(20959), 100 },
          { AOM_CDF2(20525),  50 },
          { AOM_CDF2(21190),  98 },
          { AOM_CDF2(20254),  98 },
          // BLOCK_64X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#endif  // CONFIG_CB1TO4_SPLIT
        },
        // VERT
        {
          // BLOCK_4X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_4X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X4
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X8
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16x16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X16
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X32
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X32
          { AOM_CDF2(20707), 100 },
          { AOM_CDF2(21168), 116 },
          { AOM_CDF2(19489), 100 },
          { AOM_CDF2(20945), 118 },
          // BLOCK_64X64
          { AOM_CDF2(13804),  75 },
          { AOM_CDF2(20684), 100 },
          { AOM_CDF2(17252), 100 },
          { AOM_CDF2(16447),  25 },
          // BLOCK_64X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X64
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_128X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X128
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_256X256
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
#if CONFIG_CB1TO4_SPLIT
          // BLOCK_4X16,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X4,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_8X32,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_32X8,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_16X64,
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          { AOM_CDF2(16384),   0 },  // unused entry
          // BLOCK_64X16,
          { AOM_CDF2(19479),   5 },
          { AOM_CDF2(23325),  90 },
          { AOM_CDF2(17805),   0 },
          { AOM_CDF2(23302),  78 },
#endif  // CONFIG_CB1TO4_SPLIT
        }
      },
    };
#endif  // CONFIG_PARTITION_CONTEXT_REDUCE

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
#else
static const aom_cdf_prob
    default_partition_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][CDF_SIZE(
        EXT_PARTITION_TYPES)] = {
      {
          { AOM_CDF4(19132, 25510, 30392) },
          { AOM_CDF4(13928, 19855, 28540) },
          { AOM_CDF4(12522, 23679, 28629) },
          { AOM_CDF4(9896, 18783, 25853) },
          { AOM_CDF10(15597, 20929, 24571, 26706, 27664, 28821, 29601, 30571,
                      31902) },
          { AOM_CDF10(7925, 11043, 16785, 22470, 23971, 25043, 26651, 28701,
                      29834) },
          { AOM_CDF10(5414, 13269, 15111, 20488, 22360, 24500, 25537, 26336,
                      32117) },
          { AOM_CDF10(2662, 6362, 8614, 20860, 23053, 24778, 26436, 27829,
                      31171) },
          { AOM_CDF10(18462, 20920, 23124, 27647, 28227, 29049, 29519, 30178,
                      31544) },
          { AOM_CDF10(7689, 9060, 12056, 24992, 25660, 26182, 26951, 28041,
                      29052) },
          { AOM_CDF10(6015, 9009, 10062, 24544, 25409, 26545, 27071, 27526,
                      32047) },
          { AOM_CDF10(1394, 2208, 2796, 28614, 29061, 29466, 29840, 30185,
                      31899) },
          { AOM_CDF10(20137, 21547, 23078, 29566, 29837, 30261, 30524, 30892,
                      31724) },
          { AOM_CDF10(6732, 7490, 9497, 27944, 28250, 28515, 28969, 29630,
                      30104) },
          { AOM_CDF10(5945, 7663, 8348, 28683, 29117, 29749, 30064, 30298,
                      32238) },
          { AOM_CDF10(870, 1212, 1487, 31198, 31394, 31574, 31743, 31881,
                      32332) },
          { AOM_CDF8(27899, 28219, 28529, 32484, 32539, 32619, 32639) },
          { AOM_CDF8(6607, 6990, 8268, 32060, 32219, 32338, 32371) },
          { AOM_CDF8(5429, 6676, 7122, 32027, 32227, 32531, 32582) },
          { AOM_CDF8(711, 966, 1172, 32448, 32538, 32617, 32664) },
      },
      {
          { AOM_CDF4(19132, 25510, 30392) },
          { AOM_CDF4(13928, 19855, 28540) },
          { AOM_CDF4(12522, 23679, 28629) },
          { AOM_CDF4(9896, 18783, 25853) },
          { AOM_CDF10(15597, 20929, 24571, 26706, 27664, 28821, 29601, 30571,
                      31902) },
          { AOM_CDF10(7925, 11043, 16785, 22470, 23971, 25043, 26651, 28701,
                      29834) },
          { AOM_CDF10(5414, 13269, 15111, 20488, 22360, 24500, 25537, 26336,
                      32117) },
          { AOM_CDF10(2662, 6362, 8614, 20860, 23053, 24778, 26436, 27829,
                      31171) },
          { AOM_CDF10(18462, 20920, 23124, 27647, 28227, 29049, 29519, 30178,
                      31544) },
          { AOM_CDF10(7689, 9060, 12056, 24992, 25660, 26182, 26951, 28041,
                      29052) },
          { AOM_CDF10(6015, 9009, 10062, 24544, 25409, 26545, 27071, 27526,
                      32047) },
          { AOM_CDF10(1394, 2208, 2796, 28614, 29061, 29466, 29840, 30185,
                      31899) },
          { AOM_CDF10(20137, 21547, 23078, 29566, 29837, 30261, 30524, 30892,
                      31724) },
          { AOM_CDF10(6732, 7490, 9497, 27944, 28250, 28515, 28969, 29630,
                      30104) },
          { AOM_CDF10(5945, 7663, 8348, 28683, 29117, 29749, 30064, 30298,
                      32238) },
          { AOM_CDF10(870, 1212, 1487, 31198, 31394, 31574, 31743, 31881,
                      32332) },
          { AOM_CDF8(27899, 28219, 28529, 32484, 32539, 32619, 32639) },
          { AOM_CDF8(6607, 6990, 8268, 32060, 32219, 32338, 32371) },
          { AOM_CDF8(5429, 6676, 7122, 32027, 32227, 32531, 32582) },
          { AOM_CDF8(711, 966, 1172, 32448, 32538, 32617, 32664) },
      }
    };
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_TX_TYPE_FLEX_IMPROVE
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
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
#if CONFIG_INTRA_TX_IST_PARSE
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
#if CONFIG_ENTROPY_PARA
          { AOM_CDF7(3910, 13624, 16648, 19644, 23773, 27952), 78 },
          { AOM_CDF7(11788, 21074, 24067, 27345, 29126, 30842), 78 },
          { AOM_CDF7(11068, 21436, 24806, 28312, 29521, 31139), 75 },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
#else
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
#endif  // CONFIG_ENTROPY_PARA
      },
      {
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(16384), 0 },
      },
    };
#else
static const aom_cdf_prob default_intra_ext_tx_cdf
    [EXT_TX_SETS_INTRA][EXT_TX_SIZES][INTRA_MODES][CDF_SIZE(TX_TYPES)] = {
      {
          {
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
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
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
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
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
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
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
              { 0 },
          },
      },
      {
#if CONFIG_ENTROPY_PARA
          {
              { AOM_CDF7(7079, 15798, 20375, 25171, 28212, 31332), 5 },
              { AOM_CDF7(2880, 9153, 11612, 13383, 17829, 25034), 5 },
              { AOM_CDF7(2915, 7995, 10087, 12075, 16477, 25050), 5 },
              { AOM_CDF7(7240, 18134, 21857, 25379, 27961, 30425), 75 },
              { AOM_CDF7(10110, 19503, 22520, 25414, 28355, 30824), 75 },
              { AOM_CDF7(6177, 13894, 16823, 19729, 23184, 29753), 0 },
              { AOM_CDF7(4606, 13704, 17492, 21537, 23860, 28171), 0 },
              { AOM_CDF7(6116, 12564, 15118, 18625, 23097, 27476), 75 },
              { AOM_CDF7(6136, 12808, 16164, 18181, 22187, 26731), 0 },
              { AOM_CDF7(5005, 15333, 19185, 23214, 26751, 29822), 75 },
              { AOM_CDF7(3758, 14018, 20248, 22902, 26788, 29666), 75 },
              { AOM_CDF7(3960, 14891, 17417, 22878, 26368, 30159), 0 },
              { AOM_CDF7(3065, 10762, 13532, 18790, 24634, 28767), 6 },
          },
          {
              { AOM_CDF7(11410, 22807, 25378, 28147, 29450, 31111), 0 },
              { AOM_CDF7(9060, 16042, 18386, 19332, 21908, 26134), 6 },
              { AOM_CDF7(10413, 17200, 18736, 21065, 23920, 28027), 6 },
              { AOM_CDF7(16168, 29412, 30479, 31286, 31703, 32262), 0 },
              { AOM_CDF7(16679, 30531, 31154, 31778, 31910, 32317), 75 },
              { AOM_CDF7(16140, 29626, 30369, 30838, 31012, 31615), 1 },
              { AOM_CDF7(8378, 24301, 26419, 29191, 29734, 30619), 0 },
              { AOM_CDF7(13012, 23573, 24615, 27447, 28706, 30860), 76 },
              { AOM_CDF7(15584, 28272, 30088, 30634, 31207, 31468), 75 },
              { AOM_CDF7(15770, 27785, 29156, 30494, 31132, 31842), 0 },
              { AOM_CDF7(13874, 23670, 28059, 28931, 29861, 31928), 0 },
              { AOM_CDF7(13574, 25446, 26190, 29331, 30353, 31090), 0 },
              { AOM_CDF7(1977, 8974, 12239, 14585, 22111, 28992), 32 },
          },
          {
              { AOM_CDF7(7924, 22424, 25286, 28370, 29587, 31181), 0 },
              { AOM_CDF7(9577, 17382, 21946, 23906, 25211, 28112), 7 },
              { AOM_CDF7(9550, 17635, 19762, 23854, 25388, 28592), 32 },
              { AOM_CDF7(14452, 29364, 30707, 31796, 31960, 32421), 1 },
              { AOM_CDF7(16424, 30889, 31413, 32045, 32087, 32429), 76 },
              { AOM_CDF7(15090, 29040, 30633, 31558, 31734, 32577), 2 },
              { AOM_CDF7(5024, 25715, 27794, 30249, 30473, 31057), 1 },
              { AOM_CDF7(13475, 24953, 25856, 29697, 30250, 30720), 1 },
              { AOM_CDF7(13419, 25758, 29361, 30120, 30813, 32586), 1 },
              { AOM_CDF7(17133, 28769, 30078, 31785, 31885, 32242), 0 },
              { AOM_CDF7(12194, 22235, 28518, 29596, 30337, 31955), 7 },
              { AOM_CDF7(12110, 24311, 25143, 29995, 30796, 31477), 1 },
              { AOM_CDF7(1239, 6911, 9409, 11360, 18090, 27819), 62 },
          },
          {
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
          },
#else
          {
              { AOM_CDF7(3368, 14670, 18533, 22660, 26441, 30407) },
              { AOM_CDF7(2892, 10846, 12929, 15022, 20279, 24848) },
              { AOM_CDF7(2970, 10092, 12111, 14056, 19042, 24231) },
              { AOM_CDF7(4675, 15520, 19289, 22860, 26126, 29323) },
              { AOM_CDF7(5741, 17285, 20299, 23101, 26811, 30150) },
              { AOM_CDF7(4046, 12361, 15094, 17963, 22334, 29679) },
              { AOM_CDF7(2645, 14187, 17494, 20824, 23478, 28652) },
              { AOM_CDF7(4491, 11957, 14256, 17747, 23116, 26692) },
              { AOM_CDF7(4572, 12171, 15386, 17502, 22970, 26387) },
              { AOM_CDF7(4818, 18277, 21330, 24328, 27157, 29767) },
              { AOM_CDF7(3133, 13519, 18447, 21542, 25893, 28907) },
              { AOM_CDF7(3034, 13651, 16494, 21145, 24306, 28845) },
              { AOM_CDF7(2897, 8281, 10381, 17034, 22446, 27394) },
          },
          {
              { AOM_CDF7(6913, 15909, 21003, 26934, 28464, 30480) },
              { AOM_CDF7(11567, 17963, 21143, 23834, 27212, 29744) },
              { AOM_CDF7(12143, 17474, 19848, 23648, 26868, 29636) },
              { AOM_CDF7(9814, 19582, 23675, 27984, 29550, 31079) },
              { AOM_CDF7(12675, 25454, 27677, 29916, 30466, 31574) },
              { AOM_CDF7(12920, 24484, 26753, 29154, 30052, 31578) },
              { AOM_CDF7(6977, 19974, 23611, 28014, 29128, 30383) },
              { AOM_CDF7(12055, 19503, 22014, 26902, 29041, 31594) },
              { AOM_CDF7(12331, 20997, 24825, 27187, 29128, 30275) },
              { AOM_CDF7(17925, 28050, 29454, 30862, 31560, 32031) },
              { AOM_CDF7(10669, 19564, 24634, 26808, 28587, 30808) },
              { AOM_CDF7(10600, 18770, 21109, 26488, 28800, 30563) },
              { AOM_CDF7(2685, 11088, 14733, 18441, 24856, 29321) },
          },
          {
              { AOM_CDF7(5370, 17001, 22323, 28306, 29331, 30830) },
              { AOM_CDF7(10530, 17424, 22261, 25690, 27734, 30576) },
              { AOM_CDF7(13202, 19027, 21686, 25915, 27548, 30274) },
              { AOM_CDF7(8603, 20698, 24945, 29372, 30199, 31482) },
              { AOM_CDF7(12880, 26916, 28764, 30860, 31155, 31986) },
              { AOM_CDF7(11519, 24604, 27187, 29897, 30656, 32093) },
              { AOM_CDF7(5960, 21787, 25173, 29317, 30018, 30862) },
              { AOM_CDF7(11501, 20325, 23107, 28189, 29337, 30192) },
              { AOM_CDF7(11751, 21168, 25372, 27966, 29637, 31954) },
              { AOM_CDF7(16014, 26363, 28654, 30958, 31336, 31926) },
              { AOM_CDF7(7719, 17330, 23701, 26018, 28012, 30480) },
              { AOM_CDF7(12072, 20163, 22020, 27254, 29100, 30709) },
              { AOM_CDF7(2891, 9281, 12547, 15931, 21415, 28198) },
          },
          {
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
              { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
          },
#endif  // CONFIG_ENTROPY_PARA
      },
      {
          {
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
          },
          {
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
          },
          {
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
          },
          {
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
          },
      },
    };
#endif  // CONFIG_INTRA_TX_IST_PARSE

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
#if CONFIG_ENTROPY_PARA
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
#else
      {
          {
              { AOM_CDF16(10569, 11484, 12610, 14058, 15880, 17184, 18929,
                          19803, 20702, 21995, 22642, 23795, 26269, 28128,
                          30321) },
              { AOM_CDF16(2184, 3028, 4033, 5127, 6410, 7400, 8605, 13222,
                          15760, 18377, 20510, 22737, 25720, 27841, 30221) },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720) },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720) },
          },
          {
              { AOM_CDF16(3919, 4527, 5261, 6289, 7251, 8118, 9179, 12234,
                          12471, 12730, 12785, 13079, 18477, 21441, 26844) },
              { AOM_CDF16(307, 498, 725, 1194, 1577, 1962, 2378, 26001, 26439,
                          26880, 27109, 27393, 29418, 30271, 31374) },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720) },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720) },
          },
          {
              { AOM_CDF16(18553, 19114, 19866, 21300, 23396, 24613, 26561,
                          26686, 26933, 27441, 27579, 27906, 29437, 30176,
                          31237) },
              { AOM_CDF16(14114, 15409, 17116, 18125, 19579, 20544, 21927,
                          24115, 25337, 26585, 27781, 28994, 29938, 30846,
                          31760) },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720) },
              { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                          18432, 20480, 22528, 24576, 26624, 28672, 30720) },
          },
      },
      {
          {
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
              { AOM_CDF12(847, 1837, 2897, 8379, 12029, 15839, 18755, 21734,
                          25244, 27430, 30001) },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
          },
          {
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
              { AOM_CDF12(56, 370, 765, 27899, 28744, 29465, 30060, 30562,
                          31471, 31806, 32229) },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
          },
          {
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
              { AOM_CDF12(25781, 26621, 27994, 28993, 29530, 30097, 30597,
                          31182, 31622, 32019, 32396) },
              { AOM_CDF12(2731, 5461, 8192, 10923, 13653, 16384, 19115, 21845,
                          24576, 27307, 30037) },
          },
      },
      {
          {
              { AOM_CDF2(16384) },
              { AOM_CDF2(2100) },
              { AOM_CDF2(1066) },
              { AOM_CDF2(938) },
          },
          {
              { AOM_CDF2(16384) },
              { AOM_CDF2(37) },
              { AOM_CDF2(15) },
              { AOM_CDF2(12) },
          },
          {
              { AOM_CDF2(16384) },
              { AOM_CDF2(29478) },
              { AOM_CDF2(29184) },
              { AOM_CDF2(27781) },
          },
      },
#endif  // CONFIG_ENTROPY_PARA
    };

#if CONFIG_ENTROPY_PARA
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
#else
static const aom_cdf_prob
    default_cctx_type_cdf[EXT_TX_SIZES][CCTX_CONTEXTS][CDF_SIZE(CCTX_TYPES)] = {
      { { AOM_CDF7(19143, 19642, 20876, 21362, 23684, 30645) },
        { AOM_CDF7(15852, 17519, 22430, 24276, 26473, 30362) },
        { AOM_CDF7(9981, 10351, 11021, 11340, 16893, 28901) } },
      { { AOM_CDF7(13312, 14068, 15345, 16249, 20082, 29648) },
        { AOM_CDF7(11802, 14635, 17918, 20493, 23927, 29206) },
        { AOM_CDF7(8348, 8915, 9727, 10347, 16584, 27923) } },
      { { AOM_CDF7(10604, 11887, 13486, 14485, 19798, 28529) },
        { AOM_CDF7(10790, 13346, 16867, 18854, 23398, 29133) },
        { AOM_CDF7(6538, 7104, 7997, 8723, 15658, 26864) } },
      { { AOM_CDF7(13226, 13959, 14918, 15707, 21009, 29328) },
        { AOM_CDF7(10336, 13195, 15614, 17813, 21992, 29469) },
        { AOM_CDF7(7769, 8772, 9617, 10150, 16729, 28132) } }
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_cfl_sign_cdf[CDF_SIZE(CFL_JOINT_SIGNS)] = {
  AOM_CDF8(5534, 6742, 11998, 19905, 28459, 29805, 32596), 37
};
#else
static const aom_cdf_prob default_cfl_sign_cdf[CDF_SIZE(CFL_JOINT_SIGNS)] = {
  AOM_CDF8(1418, 2123, 13340, 18405, 26972, 28343, 32294)
};
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_cfl_alpha_cdf[CFL_ALPHA_CONTEXTS][CDF_SIZE(CFL_ALPHABET_SIZE)] = {
      { AOM_CDF8(10366, 17785, 28218, 30893, 32471, 32638, 32666), 62 },
      { AOM_CDF8(4247, 18221, 24527, 31454, 32425, 32695, 32714), 37 },
      { AOM_CDF8(11483, 20769, 27162, 28811, 32007, 32287, 32375), 62 },
      { AOM_CDF8(27996, 31615, 32179, 32454, 32541, 32587, 32607), 62 },
      { AOM_CDF8(18158, 24791, 28870, 29367, 31384, 31714, 32004), 37 },
      { AOM_CDF8(18147, 27954, 31623, 31810, 31958, 32276, 32341), 62 },
    };
#else
static const aom_cdf_prob
    default_cfl_alpha_cdf[CFL_ALPHA_CONTEXTS][CDF_SIZE(CFL_ALPHABET_SIZE)] = {
      { AOM_CDF8(7650, 20740, 31430, 32520, 32700, 32730, 32740) },
      { AOM_CDF8(14400, 23680, 28230, 31270, 32290, 32530, 32640) },
      { AOM_CDF8(11560, 22430, 28510, 31430, 32430, 32610, 32680) },
      { AOM_CDF8(27000, 31430, 32310, 32610, 32730, 32740, 32750) },
      { AOM_CDF8(17320, 26210, 29100, 30820, 31550, 32150, 32430) },
      { AOM_CDF8(14990, 22180, 26430, 28600, 29820, 31200, 31980) }
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
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
#else
static const aom_cdf_prob
    default_switchable_interp_cdf[SWITCHABLE_FILTER_CONTEXTS][CDF_SIZE(
        SWITCHABLE_FILTERS)] = {
      { AOM_CDF3(31935, 32720) }, { AOM_CDF3(5568, 32719) },
      { AOM_CDF3(422, 2938) },    { AOM_CDF3(28244, 32608) },
      { AOM_CDF3(31206, 31953) }, { AOM_CDF3(4862, 32121) },
      { AOM_CDF3(770, 1152) },    { AOM_CDF3(20889, 25637) },
      { AOM_CDF3(31910, 32724) }, { AOM_CDF3(4120, 32712) },
      { AOM_CDF3(305, 2247) },    { AOM_CDF3(27403, 32636) },
      { AOM_CDF3(31022, 32009) }, { AOM_CDF3(2963, 32093) },
      { AOM_CDF3(601, 943) },     { AOM_CDF3(14969, 21398) }
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
static const aom_cdf_prob default_is_warpmv_or_warp_newmv_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16384),
};
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_inter_warp_mode_cdf[WARPMV_MODE_CONTEXT][CDF_SIZE(2)] = {
      { AOM_CDF2(31021), 118 }, { AOM_CDF2(25430), 76 },
      { AOM_CDF2(22319), 76 },  { AOM_CDF2(21114), 1 },
      { AOM_CDF2(17583), 1 },   { AOM_CDF2(14631), 1 },
      { AOM_CDF2(13520), 6 },   { AOM_CDF2(9907), 1 },
      { AOM_CDF2(7557), 1 },    { AOM_CDF2(6286), 6 },
    };
#else
static const aom_cdf_prob
    default_inter_warp_mode_cdf[WARPMV_MODE_CONTEXT][CDF_SIZE(2)] = {
      { AOM_CDF2(24626) }, { AOM_CDF2(24626) }, { AOM_CDF2(24626) },
      { AOM_CDF2(24626) }, { AOM_CDF2(24626) }, { AOM_CDF2(24626) },
      { AOM_CDF2(24626) }, { AOM_CDF2(24626) }, { AOM_CDF2(24626) },
      { AOM_CDF2(24626) }
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_OPT_INTER_MODE_CTX
static const aom_cdf_prob
    default_inter_single_mode_cdf[INTER_MODE_CONTEXTS]
                                 [CDF_SIZE(INTER_SINGLE_MODES)] = {
#if CONFIG_ENTROPY_PARA
                                   { AOM_CDF4(11339, 11624, 30039), 31 },
                                   { AOM_CDF4(24644, 24688, 30865), 6 },
                                   { AOM_CDF4(27689, 27719, 31612), 7 },
                                   { AOM_CDF4(13726, 13846, 27717), 1 },
                                   { AOM_CDF4(16389, 16521, 29001), 6 },
#else
                                   { AOM_CDF4(11339, 11624, 30039) },
                                   { AOM_CDF4(24644, 24688, 30865) },
                                   { AOM_CDF4(27689, 27719, 31612) },
                                   { AOM_CDF4(13726, 13846, 27717) },
                                   { AOM_CDF4(16389, 16521, 29001) },
#endif  // CONFIG_ENTROPY_PARA
                                 };
#else
#if CONFIG_C076_INTER_MOD_CTX
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_inter_single_mode_cdf[INTER_SINGLE_MODE_CONTEXTS]
                                 [CDF_SIZE(INTER_SINGLE_MODES)] = {
                                   { AOM_CDF4(7049, 7186, 31694), 31 },
                                   { AOM_CDF4(8192, 16384, 24576), 0 },
                                   { AOM_CDF4(8192, 16384, 24576), 0 },
                                   { AOM_CDF4(8192, 16384, 24576), 0 },
                                   { AOM_CDF4(21243, 21266, 30741), 6 },
                                   { AOM_CDF4(8192, 16384, 24576), 0 },
                                   { AOM_CDF4(13186, 13218, 27617), 1 },
                                   { AOM_CDF4(10898, 10994, 23939), 7 },
                                   { AOM_CDF4(23759, 23782, 30601), 7 },
                                   { AOM_CDF4(8192, 16384, 24576), 0 },
                                   { AOM_CDF4(17664, 17690, 27521), 6 },
                                   { AOM_CDF4(13399, 13523, 25524), 1 },
                                 };
#else
static const aom_cdf_prob
    default_inter_single_mode_cdf[INTER_SINGLE_MODE_CONTEXTS][CDF_SIZE(
        INTER_SINGLE_MODES)] = {
      { AOM_CDF4(10620, 10967, 29191) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) },  { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(23175, 23272, 28777) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(13576, 13699, 25666) }, { AOM_CDF4(15412, 15847, 24931) },
      { AOM_CDF4(26748, 26844, 29519) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(19677, 19785, 26067) }, { AOM_CDF4(13820, 14145, 24314) },
    };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob
    default_inter_single_mode_cdf[INTER_SINGLE_MODE_CONTEXTS][CDF_SIZE(
        INTER_SINGLE_MODES)] = {
      { AOM_CDF4(17346, 18771, 29200) }, { AOM_CDF4(10923, 21845, 29200) },
      { AOM_CDF4(8838, 9132, 29200) },   { AOM_CDF4(10923, 21845, 29200) },
      { AOM_CDF4(17910, 18959, 29200) }, { AOM_CDF4(16927, 17852, 29200) },
      { AOM_CDF4(11632, 11810, 29200) }, { AOM_CDF4(12506, 12827, 29200) },
      { AOM_CDF4(15831, 17676, 29200) }, { AOM_CDF4(15236, 17070, 29200) },
      { AOM_CDF4(13715, 13809, 29200) }, { AOM_CDF4(13869, 14031, 29200) },
      { AOM_CDF4(25678, 26470, 29200) }, { AOM_CDF4(23151, 23634, 29200) },
      { AOM_CDF4(21431, 21612, 29200) }, { AOM_CDF4(19838, 20021, 29200) },
      { AOM_CDF4(19562, 20206, 29200) }, { AOM_CDF4(10923, 21845, 29200) },
      { AOM_CDF4(14966, 15103, 29200) }, { AOM_CDF4(10923, 21845, 29200) },
      { AOM_CDF4(27072, 28206, 31200) }, { AOM_CDF4(10923, 21845, 29200) },
      { AOM_CDF4(24626, 24936, 30200) }, { AOM_CDF4(10923, 21845, 29200) }
    };
#endif  // CONFIG_C076_INTER_MOD_CTX
#endif  // CONFIG_OPT_INTER_MODE_CTX

#if CONFIG_OPT_INTER_MODE_CTX
static const aom_cdf_prob default_drl_cdf[3][DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
#if CONFIG_ENTROPY_PARA
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
#else
  { { AOM_CDF2(20581) },
    { AOM_CDF2(25770) },
    { AOM_CDF2(27043) },
    { AOM_CDF2(22024) },
    { AOM_CDF2(16590) } },
  { { AOM_CDF2(20638) },
    { AOM_CDF2(20418) },
    { AOM_CDF2(21113) },
    { AOM_CDF2(19645) },
    { AOM_CDF2(19650) } },
  { { AOM_CDF2(26306) },
    { AOM_CDF2(25139) },
    { AOM_CDF2(23285) },
    { AOM_CDF2(26265) },
    { AOM_CDF2(23464) } },
#endif  // CONFIG_ENTROPY_PARA
};
#else
#if CONFIG_C076_INTER_MOD_CTX
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_drl0_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(19791), 118 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(27208), 90 },  { AOM_CDF2(23688), 118 },
      { AOM_CDF2(26859), 75 },  { AOM_CDF2(17262), 123 },
    };
#else
static const aom_cdf_prob
    default_drl0_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(19182) }, { AOM_CDF2(16384) }, { AOM_CDF2(26594) },
      { AOM_CDF2(23343) }, { AOM_CDF2(25555) }, { AOM_CDF2(16773) }
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_drl1_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(19830), 118 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(20622), 90 },  { AOM_CDF2(20232), 123 },
      { AOM_CDF2(23044), 115 }, { AOM_CDF2(22767), 90 },
    };
#else
static const aom_cdf_prob
    default_drl1_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(16790) }, { AOM_CDF2(16384) }, { AOM_CDF2(16961) },
      { AOM_CDF2(16293) }, { AOM_CDF2(20567) }, { AOM_CDF2(20683) }
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_drl2_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(24851), 90 },  { AOM_CDF2(16384), 0 },
      { AOM_CDF2(22953), 120 }, { AOM_CDF2(23922), 115 },
      { AOM_CDF2(23192), 90 },  { AOM_CDF2(23606), 118 },
    };
#else
static const aom_cdf_prob
    default_drl2_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(18781) }, { AOM_CDF2(16384) }, { AOM_CDF2(19074) },
      { AOM_CDF2(19083) }, { AOM_CDF2(20824) }, { AOM_CDF2(21487) }
    };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob
    default_drl0_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(18923) }, { AOM_CDF2(12861) }, { AOM_CDF2(15472) },
      { AOM_CDF2(13796) }, { AOM_CDF2(21474) }, { AOM_CDF2(24491) },
      { AOM_CDF2(23482) }, { AOM_CDF2(23176) }, { AOM_CDF2(15143) },
      { AOM_CDF2(16155) }, { AOM_CDF2(20465) }, { AOM_CDF2(20185) }
    };
static const aom_cdf_prob default_drl0_cdf[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(26658) }, { AOM_CDF2(22485) }, { AOM_CDF2(19400) },
  { AOM_CDF2(17600) }, { AOM_CDF2(23001) }, { AOM_CDF2(25649) },
  { AOM_CDF2(25420) }, { AOM_CDF2(25271) }, { AOM_CDF2(15742) },
  { AOM_CDF2(16468) }, { AOM_CDF2(21428) }, { AOM_CDF2(21326) }
};

static const aom_cdf_prob
    default_drl1_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(6862) },  { AOM_CDF2(7013) },  { AOM_CDF2(11644) },
      { AOM_CDF2(11423) }, { AOM_CDF2(11683) }, { AOM_CDF2(12322) },
      { AOM_CDF2(11637) }, { AOM_CDF2(10987) }, { AOM_CDF2(16528) },
      { AOM_CDF2(21970) }, { AOM_CDF2(15118) }, { AOM_CDF2(17207) }
    };
static const aom_cdf_prob default_drl1_cdf[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(19705) }, { AOM_CDF2(15838) }, { AOM_CDF2(18496) },
  { AOM_CDF2(18312) }, { AOM_CDF2(15248) }, { AOM_CDF2(16292) },
  { AOM_CDF2(15982) }, { AOM_CDF2(16247) }, { AOM_CDF2(17936) },
  { AOM_CDF2(22903) }, { AOM_CDF2(16244) }, { AOM_CDF2(19319) }
};

static const aom_cdf_prob
    default_drl2_cdf_refmvbank[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(14694) }, { AOM_CDF2(13186) }, { AOM_CDF2(14211) },
      { AOM_CDF2(12899) }, { AOM_CDF2(12637) }, { AOM_CDF2(12295) },
      { AOM_CDF2(14358) }, { AOM_CDF2(13386) }, { AOM_CDF2(12462) },
      { AOM_CDF2(13917) }, { AOM_CDF2(14188) }, { AOM_CDF2(13904) }
    };
static const aom_cdf_prob default_drl2_cdf[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(12992) }, { AOM_CDF2(7518) },  { AOM_CDF2(18309) },
  { AOM_CDF2(17119) }, { AOM_CDF2(15195) }, { AOM_CDF2(15214) },
  { AOM_CDF2(16777) }, { AOM_CDF2(16998) }, { AOM_CDF2(14311) },
  { AOM_CDF2(16618) }, { AOM_CDF2(14980) }, { AOM_CDF2(15963) }
};
#endif  // CONFIG_C076_INTER_MOD_CTX
#endif  // CONFIG_OPT_INTER_MODE_CTX

#if CONFIG_ENTROPY_PARA
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
#else
static const aom_cdf_prob default_cwp_idx_cdf[MAX_CWP_CONTEXTS][MAX_CWP_NUM - 1]
                                             [CDF_SIZE(2)] = {
                                               { { AOM_CDF2(16384) },
                                                 { AOM_CDF2(16384) },
                                                 { AOM_CDF2(16384) },
                                                 { AOM_CDF2(16384) } },
                                               { { AOM_CDF2(16384) },
                                                 { AOM_CDF2(16384) },
                                                 { AOM_CDF2(16384) },
                                                 { AOM_CDF2(16384) } },
                                             };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_jmvd_scale_mode_cdf[CDF_SIZE(
    JOINT_NEWMV_SCALE_FACTOR_CNT)] = { AOM_CDF5(18498, 21150, 23573, 28129),
                                       1 };

static const aom_cdf_prob default_jmvd_amvd_scale_mode_cdf[CDF_SIZE(
    JOINT_AMVD_SCALE_FACTOR_CNT)] = { AOM_CDF3(24903, 28074), 75 };
#else
static const aom_cdf_prob
    default_jmvd_scale_mode_cdf[CDF_SIZE(JOINT_NEWMV_SCALE_FACTOR_CNT)] = {
      AOM_CDF5(22000, 25000, 28000, 30000),
    };
static const aom_cdf_prob
    default_jmvd_amvd_scale_mode_cdf[CDF_SIZE(JOINT_AMVD_SCALE_FACTOR_CNT)] = {
      AOM_CDF3(22000, 27000),
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_skip_drl_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(18247), 0 },
  { AOM_CDF2(20001), 90 },
  { AOM_CDF2(19850), 118 },
};
#else
static const aom_cdf_prob default_skip_drl_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(24394) },
  { AOM_CDF2(22637) },
  { AOM_CDF2(21474) },
};
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP

#if CONFIG_OPT_INTER_MODE_CTX
#if CONFIG_OPFL_CTX_OPT
static const aom_cdf_prob default_use_optflow_cdf[OPFL_MODE_CONTEXTS]
                                                 [CDF_SIZE(2)] = {
                                                   { AOM_CDF2(16384) },
                                                   { AOM_CDF2(16384) },
                                                 };
#else
static const aom_cdf_prob
    default_use_optflow_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(2)] = {
#if CONFIG_ENTROPY_PARA
      { AOM_CDF2(24210), 75 }, { AOM_CDF2(19707), 1 }, { AOM_CDF2(15604), 1 },
      { AOM_CDF2(21549), 1 },  { AOM_CDF2(19455), 1 },
#else
      { AOM_CDF2(24210) },
      { AOM_CDF2(19707) },
      { AOM_CDF2(15604) },
      { AOM_CDF2(21549) },
      { AOM_CDF2(19455) }
#endif  // CONFIG_ENTROPY_PARA
    };
#endif  // CONFIG_OPFL_CTX_OPT

#if CONFIG_INTER_COMPOUND_BY_JOINT

static const aom_cdf_prob
    default_inter_compound_mode_is_joint_cdf[NUM_CTX_IS_JOINT]
                                            [CDF_SIZE(NUM_OPTIONS_IS_JOINT)] = {
                                              { AOM_CDF2(16384) },
                                              { AOM_CDF2(32752) },
                                            };

static const aom_cdf_prob default_inter_compound_mode_non_joint_type_cdf
    [NUM_CTX_NON_JOINT_TYPE][CDF_SIZE(NUM_OPTIONS_NON_JOINT_TYPE)] = {
      { AOM_CDF5(15595, 24373, 27298, 27816) },
      { AOM_CDF5(21488, 26280, 28926, 29475) },
      { AOM_CDF5(26399, 28508, 30156, 30906) },
      { AOM_CDF5(13688, 20940, 25305, 25462) },
      { AOM_CDF5(18077, 21902, 25229, 25641) }
    };

static const aom_cdf_prob
    default_inter_compound_mode_joint_type_cdf[NUM_CTX_JOINT_TYPE][CDF_SIZE(
        NUM_OPTIONS_JOINT_TYPE)] = {
      { AOM_CDF2(16384) },
    };

#else

static const aom_cdf_prob
    default_inter_compound_mode_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(
        INTER_COMPOUND_REF_TYPES)] = {
#if CONFIG_ENTROPY_PARA
      { AOM_CDF7(15145, 23670, 26510, 27013, 31822, 32123), 1 },
      { AOM_CDF7(19679, 24068, 26491, 26994, 30009, 30935), 1 },
      { AOM_CDF7(22866, 24693, 26120, 26770, 28382, 29803), 76 },
      { AOM_CDF7(11666, 17847, 21567, 21701, 27927, 29225), 1 },
      { AOM_CDF7(12678, 15361, 17694, 17983, 22981, 25711), 6 }
#else
      { AOM_CDF7(15145, 23670, 26510, 27013, 31822, 32123) },
      { AOM_CDF7(19679, 24068, 26491, 26994, 30009, 30935) },
      { AOM_CDF7(22866, 24693, 26120, 26770, 28382, 29803) },
      { AOM_CDF7(11666, 17847, 21567, 21701, 27927, 29225) },
      { AOM_CDF7(12678, 15361, 17694, 17983, 22981, 25711) }
#endif  // CONFIG_ENTROPY_PARA
    };
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT

static const aom_cdf_prob
    default_inter_compound_mode_same_refs_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(
        INTER_COMPOUND_SAME_REFS_TYPES)] = {
#if CONFIG_NO_JOINTMODE_WHEN_SAME_REFINDEX
      { AOM_CDF4(10155, 29278, 29355) },
      { AOM_CDF4(16755, 29980, 30097) },
      { AOM_CDF4(20563, 30064, 30344) },
      { AOM_CDF4(12042, 28942, 29047) },
      { AOM_CDF4(13526, 28071, 28214) }
#else  // CONFIG_NO_JOINTMODE_WHEN_SAME_REFINDEX
#if CONFIG_ENTROPY_PARA
      { AOM_CDF6(10146, 29250, 29327, 32736, 32740), 1 },
      { AOM_CDF6(16739, 29951, 30068, 32736, 32740), 1 },
      { AOM_CDF6(20543, 30035, 30315, 32736, 32740), 76 },
      { AOM_CDF6(12031, 28914, 29019, 32736, 32740), 1 },
      { AOM_CDF6(13513, 28044, 28187, 32736, 32740), 6 }
#else
      { AOM_CDF6(10146, 29250, 29327, 32736, 32740) },
      { AOM_CDF6(16739, 29951, 30068, 32736, 32740) },
      { AOM_CDF6(20543, 30035, 30315, 32736, 32740) },
      { AOM_CDF6(12031, 28914, 29019, 32736, 32740) },
      { AOM_CDF6(13513, 28044, 28187, 32736, 32740) }
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_NO_JOINTMODE_WHEN_SAME_REFINDEX

    };
#else
#if CONFIG_C076_INTER_MOD_CTX
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_use_optflow_cdf[INTER_COMPOUND_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(30909), 75 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(26950), 1 },
      { AOM_CDF2(29678), 1 },  { AOM_CDF2(22260), 1 }, { AOM_CDF2(27827), 6 },
    };
#else
static const aom_cdf_prob
    default_use_optflow_cdf[INTER_COMPOUND_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(20258) }, { AOM_CDF2(16384) }, { AOM_CDF2(15212) },
      { AOM_CDF2(17153) }, { AOM_CDF2(13469) }, { AOM_CDF2(15388) }
    };
#endif  // CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_inter_compound_mode_cdf[INTER_COMPOUND_MODE_CONTEXTS][CDF_SIZE(
        INTER_COMPOUND_REF_TYPES)] = {
#if CONFIG_ENTROPY_PARA
      { AOM_CDF7(9967, 23734, 27123, 27502, 30774, 32039), 1 },
      { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
      { AOM_CDF7(15975, 20288, 23514, 23575, 25932, 29006), 1 },
      { AOM_CDF7(7601, 15010, 19863, 19907, 24870, 28126), 6 },
      { AOM_CDF7(20022, 21644, 23250, 23348, 24690, 27825), 75 },
      { AOM_CDF7(8179, 11165, 13971, 14207, 19868, 23899), 6 },
#else
      { AOM_CDF7(5669, 13946, 20791, 22484, 30450, 31644) },
      { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087) },
      { AOM_CDF7(16180, 21006, 25627, 26678, 28477, 30443) },
      { AOM_CDF7(7854, 15239, 22214, 22438, 26028, 28838) },
      { AOM_CDF7(20767, 23511, 26065, 27191, 27788, 29855) },
      { AOM_CDF7(11099, 16124, 20537, 20678, 22039, 25779) }
#endif  // CONFIG_ENTROPY_PARA
    };
static const aom_cdf_prob
    default_use_optflow_cdf[INTER_COMPOUND_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
    };
static const aom_cdf_prob
    default_inter_compound_mode_cdf[INTER_COMPOUND_MODE_CONTEXTS][CDF_SIZE(
        INTER_COMPOUND_REF_TYPES)] = {
      { AOM_CDF7(8510, 13103, 16330, 17536, 23536, 29536) },
      { AOM_CDF7(12805, 16117, 19655, 20891, 24891, 29891) },
      { AOM_CDF7(13700, 16333, 19425, 20305, 25305, 29305) },
      { AOM_CDF7(13047, 16124, 19840, 20223, 25223, 29223) },
      { AOM_CDF7(20632, 22637, 24394, 25608, 28608, 31608) },
      { AOM_CDF7(13703, 16315, 19653, 20122, 25122, 30122) },
      { AOM_CDF7(20458, 22512, 24304, 25008, 29008, 31008) },
      { AOM_CDF7(19368, 22274, 23890, 24364, 28364, 31364) }
    };
#endif  // CONFIG_C076_INTER_MOD_CTX
#endif  // CONFIG_OPT_INTER_MODE_CTX

#if CONFIG_WARP_INTER_INTRA
static const aom_cdf_prob default_warp_interintra_cdf[BLOCK_SIZE_GROUPS]
                                                     [CDF_SIZE(2)] = {
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                     };
#endif  // CONFIG_WARP_INTER_INTRA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_interintra_cdf[BLOCK_SIZE_GROUPS]
                                                [CDF_SIZE(2)] = {
                                                  { AOM_CDF2(30376), 75 },
                                                  { AOM_CDF2(20784), 1 },
                                                  { AOM_CDF2(22326), 1 },
                                                  { AOM_CDF2(24412), 1 },
                                                };
#else
static const aom_cdf_prob default_interintra_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(
    2)] = { { AOM_CDF2(16384) },
            { AOM_CDF2(26887) },
            { AOM_CDF2(27597) },
            { AOM_CDF2(30237) } };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_interintra_mode_cdf[4][CDF_SIZE(4)] = {
  { AOM_CDF4(5420, 20952, 31034), 7 },
  { AOM_CDF4(1948, 17325, 31146), 75 },
  { AOM_CDF4(3623, 17784, 29374), 1 },
  { AOM_CDF4(2843, 14004, 27752), 7 },
};
#else
static const aom_cdf_prob
    default_interintra_mode_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(
        INTERINTRA_MODES)] = { { AOM_CDF4(8192, 16384, 24576) },
                               { AOM_CDF4(1875, 11082, 27332) },
                               { AOM_CDF4(2473, 9996, 26388) },
                               { AOM_CDF4(4238, 11537, 25926) } };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_D149_CTX_MODELING_OPT
static const aom_cdf_prob default_wedge_interintra_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(14247), 75
};
#else
static const aom_cdf_prob
    default_wedge_interintra_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(20036) }, { AOM_CDF2(24957) }, { AOM_CDF2(26704) },
      { AOM_CDF2(27530) }, { AOM_CDF2(29564) }, { AOM_CDF2(29444) },
      { AOM_CDF2(26872) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
#endif  // CONFIG_D149_CTX_MODELING_OPT

#if CONFIG_D149_CTX_MODELING_OPT
static const aom_cdf_prob default_compound_type_cdf[CDF_SIZE(
    MASKED_COMPOUND_TYPES)] = { AOM_CDF2(18804), 6 };
#else
static const aom_cdf_prob default_compound_type_cdf[BLOCK_SIZES_ALL][CDF_SIZE(
    MASKED_COMPOUND_TYPES)] = {
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(23431) }, { AOM_CDF2(13171) }, { AOM_CDF2(11470) },
  { AOM_CDF2(9770) },  { AOM_CDF2(9100) },  { AOM_CDF2(8233) },
  { AOM_CDF2(6172) },  { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(11820) }, { AOM_CDF2(7701) },  { AOM_CDF2(16384) },
  { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
};
#endif  // CONFIG_D149_CTX_MODELING_OPT

#if CONFIG_WEDGE_MOD_EXT
/*wedge_angle_dir is first decoded. Depending on the wedge angle_dir, the
 * wedge_angle is decoded. Depending on the wedge_angle, the wedge_dist is
 * decoded.*/
#if CONFIG_D149_CTX_MODELING_OPT
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
#else
static const aom_cdf_prob
    default_wedge_angle_dir_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };

static const aom_cdf_prob
    default_wedge_angle_0_cdf[BLOCK_SIZES_ALL][CDF_SIZE(H_WEDGE_ANGLES)] = {
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
static const aom_cdf_prob
    default_wedge_angle_1_cdf[BLOCK_SIZES_ALL][CDF_SIZE(H_WEDGE_ANGLES)] = {
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
      { AOM_CDF10(3277, 6554, 9830, 13107, 16384, 19661, 22938, 26214, 29491) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };

static const aom_cdf_prob
    default_wedge_dist_cdf[BLOCK_SIZES_ALL][CDF_SIZE(NUM_WEDGE_DIST)] = {
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
      { AOM_CDF4(8192, 16384, 24576) }, { AOM_CDF4(8192, 16384, 24576) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
static const aom_cdf_prob
    default_wedge_dist_cdf2[BLOCK_SIZES_ALL][CDF_SIZE(NUM_WEDGE_DIST - 1)] = {
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }, { AOM_CDF3(10923, 21845) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
#endif  // CONFIG_D149_CTX_MODELING_OPT
#else
static const aom_cdf_prob default_wedge_idx_cdf[BLOCK_SIZES_ALL][CDF_SIZE(
    16)] = { { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2438, 4440, 6599, 8663, 11005, 12874, 15751, 18094,
                         20359, 22362, 24127, 25702, 27752, 29450, 31171) },
             { AOM_CDF16(806, 3266, 6005, 6738, 7218, 7367, 7771, 14588, 16323,
                         17367, 18452, 19422, 22839, 26127, 29629) },
             { AOM_CDF16(2779, 3738, 4683, 7213, 7775, 8017, 8655, 14357, 17939,
                         21332, 24520, 27470, 29456, 30529, 31656) },
             { AOM_CDF16(1684, 3625, 5675, 7108, 9302, 11274, 14429, 17144,
                         19163, 20961, 22884, 24471, 26719, 28714, 30877) },
             { AOM_CDF16(1142, 3491, 6277, 7314, 8089, 8355, 9023, 13624, 15369,
                         16730, 18114, 19313, 22521, 26012, 29550) },
             { AOM_CDF16(2742, 4195, 5727, 8035, 8980, 9336, 10146, 14124,
                         17270, 20533, 23434, 25972, 27944, 29570, 31416) },
             { AOM_CDF16(1727, 3948, 6101, 7796, 9841, 12344, 15766, 18944,
                         20638, 22038, 23963, 25311, 26988, 28766, 31012) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
#if CONFIG_EXT_RECUR_PARTITIONS
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(154, 987, 1925, 2051, 2088, 2111, 2151, 23033, 23703,
                         24284, 24985, 25684, 27259, 28883, 30911) },
             { AOM_CDF16(1135, 1322, 1493, 2635, 2696, 2737, 2770, 21016, 22935,
                         25057, 27251, 29173, 30089, 30960, 31933) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) },
             { AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384,
                         18432, 20480, 22528, 24576, 26624, 28672, 30720) } };
#endif  // CONFIG_WEDGE_MOD_EXT

#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
static const aom_cdf_prob default_warp_causal_cdf[WARP_CAUSAL_MODE_CTX]
                                                 [CDF_SIZE(2)] = {
                                                   { AOM_CDF2(16384) },
                                                   { AOM_CDF2(16384) },
                                                   { AOM_CDF2(16384) },
                                                   { AOM_CDF2(16384) }
                                                 };
#else
#if CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
static const aom_cdf_prob default_warp_causal_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(8354), 76
};
#else
static const aom_cdf_prob
    default_warp_causal_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
      { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
      { AOM_CDF2(21827), 76 }, { AOM_CDF2(20801), 7 },  { AOM_CDF2(22822), 7 },
      { AOM_CDF2(28283), 7 },  { AOM_CDF2(17490), 7 },  { AOM_CDF2(22156), 7 },
      { AOM_CDF2(29137), 7 },  { AOM_CDF2(26381), 25 }, { AOM_CDF2(25945), 36 },
      { AOM_CDF2(29190), 37 }, { AOM_CDF2(30434), 70 }, { AOM_CDF2(30786), 60 },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(31582), 70 }, { AOM_CDF2(30434), 50 }, { AOM_CDF2(30786), 50 },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(31582), 50 }, { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
      { AOM_CDF2(30177), 5 },  { AOM_CDF2(30093), 7 },  { AOM_CDF2(31776), 31 },
      { AOM_CDF2(31514), 37 },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(24000), 0 },  { AOM_CDF2(24000), 0 },  { AOM_CDF2(24000), 2 },
      { AOM_CDF2(24000), 30 }, { AOM_CDF2(24000), 0 },  { AOM_CDF2(24000), 0 },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
#endif  // CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

#if CONFIG_WARP_PRECISION
static const aom_cdf_prob
    default_warp_precision_idx_cdf[BLOCK_SIZES_ALL][CDF_SIZE(
        NUM_WARP_PRECISION_MODES)] = {
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
#endif  // CONFIG_WARP_PRECISION

#if CONFIG_D149_CTX_MODELING_OPT
static const aom_cdf_prob default_warp_causal_warpmv_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(7108), 76
};
#else
static const aom_cdf_prob
    default_warp_causal_warpmv_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(21827) }, { AOM_CDF2(20801) }, { AOM_CDF2(22822) },
      { AOM_CDF2(28283) }, { AOM_CDF2(17490) }, { AOM_CDF2(22156) },
      { AOM_CDF2(29137) }, { AOM_CDF2(26381) }, { AOM_CDF2(25945) },
      { AOM_CDF2(29190) }, { AOM_CDF2(30434) }, { AOM_CDF2(30786) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(31582) }, { AOM_CDF2(30434) }, { AOM_CDF2(30786) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(31582) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(30177) }, { AOM_CDF2(30093) }, { AOM_CDF2(31776) },
      { AOM_CDF2(31514) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(24000) }, { AOM_CDF2(24000) }, { AOM_CDF2(24000) },
      { AOM_CDF2(24000) }, { AOM_CDF2(24000) }, { AOM_CDF2(24000) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
#endif  // CONFIG_D149_CTX_MODELING_OPT

#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
#if CONFIG_D149_CTX_MODELING_OPT
static const aom_cdf_prob default_warp_delta_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16880), 1
};
#else
static const aom_cdf_prob
    default_warp_delta_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(4015) },  { AOM_CDF2(5407) },  { AOM_CDF2(4988) },
      { AOM_CDF2(9806) },  { AOM_CDF2(7405) },  { AOM_CDF2(7949) },
      { AOM_CDF2(14870) }, { AOM_CDF2(18438) }, { AOM_CDF2(16459) },
      { AOM_CDF2(19468) }, { AOM_CDF2(24415) }, { AOM_CDF2(22864) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(23527) }, { AOM_CDF2(24415) }, { AOM_CDF2(22864) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(23527) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(19610) }, { AOM_CDF2(16215) }, { AOM_CDF2(25420) },
      { AOM_CDF2(25105) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(24000) }, { AOM_CDF2(24000) }, { AOM_CDF2(24000) },
      { AOM_CDF2(24000) }, { AOM_CDF2(24000) }, { AOM_CDF2(24000) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
#endif  // CONFIG_D149_CTX_MODELING_OPT
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

#if CONFIG_ENTROPY_PARA
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
#else
static const aom_cdf_prob default_warp_ref_idx0_cdf[WARP_REF_CONTEXTS][CDF_SIZE(
    2)] = { { AOM_CDF2(15906) } };
static const aom_cdf_prob default_warp_ref_idx1_cdf[WARP_REF_CONTEXTS][CDF_SIZE(
    2)] = { { AOM_CDF2(15903) } };
static const aom_cdf_prob default_warp_ref_idx2_cdf[WARP_REF_CONTEXTS][CDF_SIZE(
    2)] = { { AOM_CDF2(18242) } };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_D149_CTX_MODELING_OPT
static const aom_cdf_prob default_warpmv_with_mvd_flag_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(15615), 1
};
#else
static const aom_cdf_prob
    default_warpmv_with_mvd_flag_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
#endif  // CONFIG_D149_CTX_MODELING_OPT

#if CONFIG_ENTROPY_PARA

#if CONFIG_WARP_PRECISION
static const aom_cdf_prob
    default_warp_delta_param_cdf[2][CDF_SIZE(WARP_DELTA_NUMSYMBOLS_LOW)] = {
      { AOM_CDF8(9299, 17448, 21838, 26151, 27798, 30186, 30701), 0 },
      { AOM_CDF8(9299, 17448, 21838, 26151, 27798, 30186, 30701), 0 },
    };
static const aom_cdf_prob default_warp_delta_param_high_cdf[2][CDF_SIZE(
    WARP_DELTA_NUMSYMBOLS_HIGH)] = {
  { AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672), 0 },
  { AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672), 0 },
};

static const aom_cdf_prob default_warp_param_sign_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16384) };
#else
static const aom_cdf_prob
    default_warp_delta_param_cdf[2][CDF_SIZE(WARP_DELTA_NUMSYMBOLS_LOW)] = {
      { AOM_CDF15(1145, 1345, 2592, 3313, 5807, 7676, 11006, 22326, 26018,
                  27897, 29625, 30747, 31669, 31902),
        75 },
      { AOM_CDF15(1211, 1656, 2876, 3713, 6446, 8632, 12954, 20000, 23904,
                  26663, 28718, 29949, 31370, 31806),
        75 },
    };
#endif  // CONFIG_WARP_PRECISION

#if CONFIG_OPTIMIZE_CTX_TIP_WARP
static const aom_cdf_prob default_warp_extend_cdf[WARP_EXTEND_CTX][CDF_SIZE(
    2)] = { { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) } };
#else
static const aom_cdf_prob default_warp_extend_cdf[WARP_EXTEND_CTXS1]
                                                 [WARP_EXTEND_CTXS2]
                                                 [CDF_SIZE(2)] = {
                                                   {
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                   },
                                                   {
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(29503), 75 },
                                                       { AOM_CDF2(16863), 1 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(27913), 75 },
                                                   },
                                                   {
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16628), 0 },
                                                       { AOM_CDF2(9629), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(14836), 1 },
                                                   },
                                                   {
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(16384), 0 },
                                                   },
                                                   {
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(28376), 75 },
                                                       { AOM_CDF2(16236), 1 },
                                                       { AOM_CDF2(16384), 0 },
                                                       { AOM_CDF2(23492), 76 },
                                                   },
                                                 };
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
#else
static const aom_cdf_prob
    default_warp_delta_param_cdf[2][CDF_SIZE(WARP_DELTA_NUMSYMBOLS_LOW)] = {
      { AOM_CDF15(2185, 4369, 6554, 8738, 10923, 13107, 15292, 17476, 19661,
                  21845, 24030, 26214, 28399, 30583) },
      { AOM_CDF15(2185, 4369, 6554, 8738, 10923, 13107, 15292, 17476, 19661,
                  21845, 24030, 26214, 28399, 30583) }
    };

#if CONFIG_OPTIMIZE_CTX_TIP_WARP
static const aom_cdf_prob default_warp_extend_cdf[WARP_EXTEND_CTX][CDF_SIZE(
    2)] = { { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) } };
#else
static const aom_cdf_prob
    default_warp_extend_cdf[WARP_EXTEND_CTXS1][WARP_EXTEND_CTXS2]
                           [CDF_SIZE(2)] = { { { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) } },
                                             { { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) } },
                                             { { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) } },
                                             { { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) } },
                                             { { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) },
                                               { AOM_CDF2(16384) } } };
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_REFINEMV
#if CONFIG_ENTROPY_PARA
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
#else
static const aom_cdf_prob default_refinemv_flag_cdf[NUM_REFINEMV_CTX][CDF_SIZE(
    REFINEMV_NUM_MODES)] = {
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) }
};
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_REFINEMV

#if CONFIG_BAWP
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_bawp_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(27422), 1 },
  { AOM_CDF2(15131), 6 },
};
#else
static const aom_cdf_prob default_bawp_cdf[CDF_SIZE(2)] = { AOM_CDF2(23664) };
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_BAWP
#if CONFIG_EXPLICIT_BAWP
static const aom_cdf_prob
    default_explicit_bawp_cdf[BAWP_SCALES_CTX_COUNT][CDF_SIZE(2)] = {
      { AOM_CDF2(19664) }, { AOM_CDF2(21664) }, { AOM_CDF2(23664) }
    };
static const aom_cdf_prob default_explicit_bawp_scale_cdf[CDF_SIZE(
    EXPLICIT_BAWP_SCALE_CNT)] = { AOM_CDF2(16384) };
#endif  // CONFIG_EXPLICIT_BAWP

#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
#if CONFIG_NEW_CONTEXT_MODELING
static const aom_cdf_prob default_intra_inter_cdf
    [INTRA_INTER_SKIP_TXFM_CONTEXTS][INTRA_INTER_CONTEXTS][CDF_SIZE(2)] = {
      { { AOM_CDF2(2981) },
        { AOM_CDF2(16980) },
        { AOM_CDF2(16384) },
        { AOM_CDF2(29992) } },
      { { AOM_CDF2(4) }, { AOM_CDF2(4) }, { AOM_CDF2(16384) }, { AOM_CDF2(4) } }
    };
#else
static const aom_cdf_prob
    default_intra_inter_cdf[INTRA_INTER_SKIP_TXFM_CONTEXTS]
                           [INTRA_INTER_CONTEXTS][CDF_SIZE(2)] = {
                             { { AOM_CDF2(806) },
                               { AOM_CDF2(16662) },
                               { AOM_CDF2(20186) },
                               { AOM_CDF2(26538) } },
                             { { AOM_CDF2(806) },
                               { AOM_CDF2(16662) },
                               { AOM_CDF2(20186) },
                               { AOM_CDF2(26538) } },
                           };
#endif  // CONFIG_NEW_CONTEXT_MODELING
#else
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_intra_inter_cdf[INTRA_INTER_CONTEXTS]
                                                 [CDF_SIZE(2)] = {
                                                   { AOM_CDF2(2375), 75 },
                                                   { AOM_CDF2(16902), 75 },
                                                   { AOM_CDF2(16384), 0 },
                                                   { AOM_CDF2(29584), 0 },
                                                 };
#else
static const aom_cdf_prob default_intra_inter_cdf[INTRA_INTER_CONTEXTS]
                                                 [CDF_SIZE(2)] = {
                                                   { AOM_CDF2(806) },
                                                   { AOM_CDF2(16662) },
                                                   { AOM_CDF2(20186) },
                                                   { AOM_CDF2(26538) }
                                                 };
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_tip_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(31852), 118 },
  { AOM_CDF2(18438), 90 },
  { AOM_CDF2(8752), 90 },
};

#if CONFIG_OPTIMIZE_CTX_TIP_WARP
static const aom_cdf_prob default_tip_pred_mode_cdf[CDF_SIZE(
    TIP_PRED_MODES)] = { AOM_CDF3(10923, 21845), 0 };
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
#else
static const aom_cdf_prob default_tip_cdf[TIP_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(23040) }, { AOM_CDF2(15360) }, { AOM_CDF2(10240) }
};

#if CONFIG_OPTIMIZE_CTX_TIP_WARP
static const aom_cdf_prob default_tip_pred_mode_cdf[CDF_SIZE(
    TIP_PRED_MODES)] = { AOM_CDF3(10923, 21845) };
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(27078), 75 }, { AOM_CDF2(22913), 1 }, { AOM_CDF2(15254), 1 },
      { AOM_CDF2(13473), 1 },  { AOM_CDF2(5765), 0 },
    };
#else
static const aom_cdf_prob default_comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(
    2)] = { { AOM_CDF2(28501) },
            { AOM_CDF2(26110) },
            { AOM_CDF2(16161) },
            { AOM_CDF2(13261) },
            { AOM_CDF2(4456) } };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob default_comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(
    2)] = { { AOM_CDF2(26828) },
            { AOM_CDF2(24035) },
            { AOM_CDF2(12031) },
            { AOM_CDF2(10640) },
            { AOM_CDF2(2901) } };
#endif  // CONFIG_NEW_CONTEXT_MODELING

#if CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENTROPY_PARA
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
#else
static const aom_cdf_prob
    default_single_ref_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
                          [CDF_SIZE(2)] = { { { AOM_CDF2(25719) },
                                              { AOM_CDF2(27480) },
                                              { AOM_CDF2(29046) },
                                              { AOM_CDF2(28671) },
                                              { AOM_CDF2(28017) },
                                              { AOM_CDF2(28196) } },
                                            { { AOM_CDF2(14843) },
                                              { AOM_CDF2(16287) },
                                              { AOM_CDF2(19737) },
                                              { AOM_CDF2(17261) },
                                              { AOM_CDF2(16079) },
                                              { AOM_CDF2(10556) } },
                                            { { AOM_CDF2(3646) },
                                              { AOM_CDF2(4988) },
                                              { AOM_CDF2(6556) },
                                              { AOM_CDF2(4514) },
                                              { AOM_CDF2(4734) },
                                              { AOM_CDF2(1722) } } };

static const aom_cdf_prob
#if CONFIG_SAME_REF_COMPOUND
    default_comp_ref0_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
#else
    default_comp_ref0_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 2]
#endif  // CONFIG_SAME_REF_COMPOUND
                         [CDF_SIZE(2)] = { { { AOM_CDF2(10451) },
                                             { AOM_CDF2(18507) },
                                             { AOM_CDF2(16384) },
                                             { AOM_CDF2(16384) },
#if CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(23235) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(16384) } },
                                           { { AOM_CDF2(1381) },
                                             { AOM_CDF2(5629) },
                                             { AOM_CDF2(16384) },
                                             { AOM_CDF2(16384) },
#if CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(29626) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(16384) } },
                                           { { AOM_CDF2(1328) },
                                             { AOM_CDF2(4223) },
                                             { AOM_CDF2(16384) },
                                             { AOM_CDF2(16384) },
#if CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(11282) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(16384) } } };

static const aom_cdf_prob default_comp_ref1_cdf[REF_CONTEXTS][COMPREF_BIT_TYPES]
#if CONFIG_SAME_REF_COMPOUND
                                               [INTER_REFS_PER_FRAME - 1]
#else
                                               [INTER_REFS_PER_FRAME - 2]
#endif  // CONFIG_SAME_REF_COMPOUND
                                               [CDF_SIZE(2)] = {
                                                 { { { AOM_CDF2(27841) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(901) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(29341) },
                                                     { AOM_CDF2(30001) },
                                                     { AOM_CDF2(29029) },
                                                     { AOM_CDF2(27250) } },
                                                   { { AOM_CDF2(20857) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(1294) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(25943) },
                                                     { AOM_CDF2(23748) },
                                                     { AOM_CDF2(24547) },
                                                     { AOM_CDF2(25559) } } },
                                                 { { { AOM_CDF2(15336) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(18827) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(19099) },
                                                     { AOM_CDF2(21068) },
                                                     { AOM_CDF2(20352) },
                                                     { AOM_CDF2(16553) } },
                                                   { { AOM_CDF2(9172) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(20397) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(14182) },
                                                     { AOM_CDF2(10930) },
                                                     { AOM_CDF2(8985) },
                                                     { AOM_CDF2(4744) } } },
                                                 { { { AOM_CDF2(4205) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(10566) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(5538) },
                                                     { AOM_CDF2(8404) },
                                                     { AOM_CDF2(9013) },
                                                     { AOM_CDF2(6228) } },
                                                   { { AOM_CDF2(1280) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(800) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(5071) },
                                                     { AOM_CDF2(2384) },
                                                     { AOM_CDF2(1409) },
                                                     { AOM_CDF2(500) } } }
                                               };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob
    default_single_ref_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
                          [CDF_SIZE(2)] = { { { AOM_CDF2(26431) },
                                              { AOM_CDF2(27737) },
                                              { AOM_CDF2(30341) },
                                              { AOM_CDF2(30525) },
                                              { AOM_CDF2(30361) },
                                              { AOM_CDF2(28368) } },
                                            { { AOM_CDF2(15825) },
                                              { AOM_CDF2(15748) },
                                              { AOM_CDF2(22176) },
                                              { AOM_CDF2(22342) },
                                              { AOM_CDF2(19463) },
                                              { AOM_CDF2(9639) } },
                                            { { AOM_CDF2(5075) },
                                              { AOM_CDF2(3515) },
                                              { AOM_CDF2(7199) },
                                              { AOM_CDF2(6223) },
                                              { AOM_CDF2(4186) },
                                              { AOM_CDF2(927) } } };

static const aom_cdf_prob
#if CONFIG_SAME_REF_COMPOUND
    default_comp_ref0_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
#else
    default_comp_ref0_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 2]
#endif  // CONFIG_SAME_REF_COMPOUND
                         [CDF_SIZE(2)] = { { { AOM_CDF2(9565) },
                                             { AOM_CDF2(20372) },
                                             { AOM_CDF2(26108) },
                                             { AOM_CDF2(25698) },
#if CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(23235) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(23235) } },
                                           { { AOM_CDF2(29266) },
                                             { AOM_CDF2(29841) },
                                             { AOM_CDF2(31056) },
                                             { AOM_CDF2(31670) },
#if CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(29626) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(29626) } },
                                           { { AOM_CDF2(6865) },
                                             { AOM_CDF2(16538) },
                                             { AOM_CDF2(17412) },
                                             { AOM_CDF2(15905) },
#if CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(11282) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                             { AOM_CDF2(11282) } } };
static const aom_cdf_prob default_comp_ref1_cdf[REF_CONTEXTS][COMPREF_BIT_TYPES]
#if CONFIG_SAME_REF_COMPOUND
                                               [INTER_REFS_PER_FRAME - 1]
#else
                                               [INTER_REFS_PER_FRAME - 2]
#endif  // CONFIG_SAME_REF_COMPOUND
                                               [CDF_SIZE(2)] = {
                                                 { { { AOM_CDF2(901) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(901) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(4025) },
                                                     { AOM_CDF2(11946) },
                                                     { AOM_CDF2(12060) },
                                                     { AOM_CDF2(9161) } },
                                                   { { AOM_CDF2(1294) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(1294) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(2591) },
                                                     { AOM_CDF2(8201) },
                                                     { AOM_CDF2(7951) },
                                                     { AOM_CDF2(4942) } } },
                                                 { { { AOM_CDF2(18827) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(18827) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(29089) },
                                                     { AOM_CDF2(29533) },
                                                     { AOM_CDF2(29695) },
                                                     { AOM_CDF2(28668) } },
                                                   { { AOM_CDF2(20397) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(20397) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(19716) },
                                                     { AOM_CDF2(22602) },
                                                     { AOM_CDF2(23821) },
                                                     { AOM_CDF2(16842) } } },
                                                 { { { AOM_CDF2(10566) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(10566) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(8314) },
                                                     { AOM_CDF2(7659) },
                                                     { AOM_CDF2(7571) },
                                                     { AOM_CDF2(5115) } },
                                                   { { AOM_CDF2(800) },
#if CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(800) },
#endif  // CONFIG_SAME_REF_COMPOUND
                                                     { AOM_CDF2(4065) },
                                                     { AOM_CDF2(3440) },
                                                     { AOM_CDF2(2442) },
                                                     { AOM_CDF2(1696) } } }
                                               };
#endif  // CONFIG_NEW_CONTEXT_MODELING

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_palette_y_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(7)] = {
      { AOM_CDF7(8980, 17001, 23975, 28227, 31080, 32391), 31 },
      { AOM_CDF7(7869, 14161, 20649, 25333, 29368, 31714), 62 },
      { AOM_CDF7(8692, 14565, 20188, 24407, 28613, 31377), 57 },
      { AOM_CDF7(11733, 17824, 22892, 26328, 29474, 31560), 57 },
      { AOM_CDF7(14858, 19966, 24263, 27042, 29636, 31578), 62 },
      { AOM_CDF7(20123, 24904, 28760, 30353, 31536, 32374), 62 },
      { AOM_CDF7(22538, 25895, 28878, 30477, 31436, 32342), 50 },
    };

static const aom_cdf_prob
    default_palette_uv_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(7)] = {
      { AOM_CDF7(11398, 27960, 31878, 32234, 32412, 32590), 35 },
      { AOM_CDF7(9753, 16957, 25860, 31807, 32325, 32546), 22 },
      { AOM_CDF7(13637, 24235, 28521, 30625, 31677, 32106), 14 },
      { AOM_CDF7(8294, 17365, 21627, 29797, 32237, 32520), 25 },
      { AOM_CDF7(7198, 22557, 26240, 28583, 30341, 32182), 50 },
      { AOM_CDF7(5790, 20135, 25662, 27372, 30399, 31320), 0 },
      { AOM_CDF7(10923, 19275, 23773, 28913, 30198, 31483), 0 },
    };
#else
static const aom_cdf_prob
    default_palette_y_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(PALETTE_SIZES)] = {
      { AOM_CDF7(7952, 13000, 18149, 21478, 25527, 29241) },
      { AOM_CDF7(7139, 11421, 16195, 19544, 23666, 28073) },
      { AOM_CDF7(7788, 12741, 17325, 20500, 24315, 28530) },
      { AOM_CDF7(8271, 14064, 18246, 21564, 25071, 28533) },
      { AOM_CDF7(12725, 19180, 21863, 24839, 27535, 30120) },
      { AOM_CDF7(9711, 14888, 16923, 21052, 25661, 27875) },
      { AOM_CDF7(14940, 20797, 21678, 24186, 27033, 28999) }
    };

static const aom_cdf_prob
    default_palette_uv_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(PALETTE_SIZES)] = {
      { AOM_CDF7(8713, 19979, 27128, 29609, 31331, 32272) },
      { AOM_CDF7(5839, 15573, 23581, 26947, 29848, 31700) },
      { AOM_CDF7(4426, 11260, 17999, 21483, 25863, 29430) },
      { AOM_CDF7(3228, 9464, 14993, 18089, 22523, 27420) },
      { AOM_CDF7(3768, 8886, 13091, 17852, 22495, 27207) },
      { AOM_CDF7(2464, 8451, 12861, 21632, 25525, 28555) },
      { AOM_CDF7(1269, 5435, 10433, 18963, 21700, 25865) }
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENTROPY_PARA
const aom_cdf_prob default_palette_y_mode_cdf[PALATTE_BSIZE_CTXS]
                                             [PALETTE_Y_MODE_CONTEXTS]
                                             [CDF_SIZE(2)] = {
                                               {
                                                   { AOM_CDF2(30536), 31 },
                                                   { AOM_CDF2(6486), 6 },
                                                   { AOM_CDF2(1648), 79 },
                                               },
                                               {
                                                   { AOM_CDF2(29901), 32 },
                                                   { AOM_CDF2(3603), 1 },
                                                   { AOM_CDF2(385), 94 },
                                               },
                                               {
                                                   { AOM_CDF2(30493), 32 },
                                                   { AOM_CDF2(4418), 2 },
                                                   { AOM_CDF2(362), 24 },
                                               },
                                               {
                                                   { AOM_CDF2(30056), 32 },
                                                   { AOM_CDF2(2219), 1 },
                                                   { AOM_CDF2(227), 104 },
                                               },
                                               {
                                                   { AOM_CDF2(30645), 37 },
                                                   { AOM_CDF2(3467), 0 },
                                                   { AOM_CDF2(311), 110 },
                                               },
                                               {
                                                   { AOM_CDF2(31958), 7 },
                                                   { AOM_CDF2(5573), 50 },
                                                   { AOM_CDF2(391), 100 },
                                               },
                                               {
                                                   { AOM_CDF2(32285), 32 },
                                                   { AOM_CDF2(5537), 50 },
                                                   { AOM_CDF2(446), 0 },
                                               },
                                             };
#else
const aom_cdf_prob default_palette_y_mode_cdf
    [PALATTE_BSIZE_CTXS][PALETTE_Y_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { { AOM_CDF2(30733) }, { AOM_CDF2(5392) }, { AOM_CDF2(1632) } },
      { { AOM_CDF2(30764) }, { AOM_CDF2(2316) }, { AOM_CDF2(498) } },
      { { AOM_CDF2(31520) }, { AOM_CDF2(5631) }, { AOM_CDF2(1056) } },
      { { AOM_CDF2(31432) }, { AOM_CDF2(1647) }, { AOM_CDF2(347) } },
      { { AOM_CDF2(31770) }, { AOM_CDF2(4855) }, { AOM_CDF2(642) } },
      { { AOM_CDF2(31894) }, { AOM_CDF2(2429) }, { AOM_CDF2(275) } },
      { { AOM_CDF2(31813) }, { AOM_CDF2(2439) }, { AOM_CDF2(56) } }
    };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob default_palette_y_mode_cdf
    [PALATTE_BSIZE_CTXS][PALETTE_Y_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { { AOM_CDF2(31676) }, { AOM_CDF2(3419) }, { AOM_CDF2(1261) } },
      { { AOM_CDF2(31912) }, { AOM_CDF2(2859) }, { AOM_CDF2(980) } },
      { { AOM_CDF2(31823) }, { AOM_CDF2(3400) }, { AOM_CDF2(781) } },
      { { AOM_CDF2(32030) }, { AOM_CDF2(3561) }, { AOM_CDF2(904) } },
      { { AOM_CDF2(32309) }, { AOM_CDF2(7337) }, { AOM_CDF2(1462) } },
      { { AOM_CDF2(32265) }, { AOM_CDF2(4015) }, { AOM_CDF2(1521) } },
      { { AOM_CDF2(32450) }, { AOM_CDF2(7946) }, { AOM_CDF2(129) } }
    };
#endif  // CONFIG_NEW_CONTEXT_MODELING

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_palette_uv_mode_cdf[PALETTE_UV_MODE_CONTEXTS]
                                                     [CDF_SIZE(2)] = {
                                                       { AOM_CDF2(32725), 37 },
                                                       { AOM_CDF2(32721), 62 },
                                                     };
#else
static const aom_cdf_prob
    default_palette_uv_mode_cdf[PALETTE_UV_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(32461) }, { AOM_CDF2(21488) }
    };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_PALETTE_IMPROVEMENTS
#if CONFIG_ENTROPY_PARA
#if CONFIG_PALETTE_LINE_COPY
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
static const aom_cdf_prob default_palette_direction_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(21697), 6
};
#else
static const aom_cdf_prob default_identity_row_cdf_y[3][CDF_SIZE(2)] = {
  { AOM_CDF2(16384), 28 },
  { AOM_CDF2(16384), 30 },
  { AOM_CDF2(16384), 1 },
};

static const aom_cdf_prob default_identity_row_cdf_uv[3][CDF_SIZE(2)] = {
  { AOM_CDF2(16384), 31 },
  { AOM_CDF2(16384), 30 },
  { AOM_CDF2(16384), 27 },
};
#endif  // CONFIG_PALETTE_LINE_COPY

static const aom_cdf_prob default_palette_y_color_index_cdf
    [PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
      {
          { AOM_CDF2(27736), 118 },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(11503), 0 },
          { AOM_CDF2(27936), 0 },
          { AOM_CDF2(30969), 118 },
          { AOM_CDF2(25926), 32 },
      },
      {
          { AOM_CDF3(25986, 29935), 118 },
          { AOM_CDF3(13551, 27642), 75 },
          { AOM_CDF3(9780, 30721), 78 },
          { AOM_CDF3(27000, 30194), 0 },
          { AOM_CDF3(30822, 31898), 118 },
          { AOM_CDF3(14607, 31499), 39 },
      },
      {
          { AOM_CDF4(24363, 27733, 30404), 75 },
          { AOM_CDF4(12281, 24615, 29312), 0 },
          { AOM_CDF4(8342, 29344, 31236), 5 },
          { AOM_CDF4(25124, 27914, 30574), 6 },
          { AOM_CDF4(30525, 31368, 32024), 118 },
          { AOM_CDF4(16475, 30324, 31501), 60 },
      },
      {
          { AOM_CDF5(24106, 27034, 28992, 30726), 90 },
          { AOM_CDF5(11807, 22655, 26994, 29901), 0 },
          { AOM_CDF5(8155, 28558, 30113, 31477), 0 },
          { AOM_CDF5(24345, 27154, 29214, 30779), 6 },
          { AOM_CDF5(31104, 31595, 31990, 32347), 118 },
          { AOM_CDF5(22342, 31279, 31576, 32172), 52 },
      },
      {
          { AOM_CDF6(23135, 26179, 27859, 29224, 30685), 75 },
          { AOM_CDF6(10496, 19940, 23918, 27086, 29383), 75 },
          { AOM_CDF6(6889, 27218, 28683, 30027, 31337), 75 },
          { AOM_CDF6(22660, 25383, 27624, 28763, 30134), 1 },
          { AOM_CDF6(30870, 31385, 31775, 32098, 32382), 118 },
          { AOM_CDF6(17994, 31821, 32010, 32200, 32389), 60 },
      },
      {
          { AOM_CDF7(23321, 25235, 27127, 28801, 30205, 31551), 78 },
          { AOM_CDF7(11647, 24221, 27037, 28767, 30225, 31521), 0 },
          { AOM_CDF7(8054, 27873, 29104, 30228, 31169, 32021), 75 },
          { AOM_CDF7(26203, 28093, 29264, 30307, 31135, 31944), 0 },
          { AOM_CDF7(30786, 31142, 31524, 31875, 32187, 32487), 118 },
          { AOM_CDF7(10773, 27830, 28279, 28728, 30075, 30972), 60 },
      },
      {
          { AOM_CDF8(22333, 24302, 26044, 27605, 28875, 30027, 31276), 78 },
          { AOM_CDF8(10779, 22091, 24836, 26895, 28272, 29595, 30940), 78 },
          { AOM_CDF8(7840, 27014, 28224, 29323, 30266, 31126, 31986), 75 },
          { AOM_CDF8(25928, 27927, 29029, 29851, 30506, 31153, 31894), 75 },
          { AOM_CDF8(30635, 30976, 31348, 31692, 31996, 32261, 32521), 123 },
          { AOM_CDF8(20311, 31143, 31414, 31685, 31956, 32226, 32497), 65 },
      },
    };

static const aom_cdf_prob default_palette_uv_color_index_cdf
    [PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
      {
          { AOM_CDF2(27433), 20 },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(12338), 29 },
          { AOM_CDF2(25404), 49 },
          { AOM_CDF2(30187), 118 },
          { AOM_CDF2(28813), 0 },
      },
      {
          { AOM_CDF3(25007, 29015), 119 },
          { AOM_CDF3(17269, 26693), 29 },
          { AOM_CDF3(8723, 28388), 26 },
          { AOM_CDF3(20114, 23829), 5 },
          { AOM_CDF3(30245, 31584), 90 },
          { AOM_CDF3(28880, 29436), 0 },
      },
      {
          { AOM_CDF4(19597, 24558, 29366), 82 },
          { AOM_CDF4(14190, 23894, 28093), 37 },
          { AOM_CDF4(9909, 27096, 29946), 32 },
          { AOM_CDF4(25232, 27159, 30351), 64 },
          { AOM_CDF4(30583, 31261, 32092), 76 },
          { AOM_CDF4(4520, 22599, 25988), 0 },
      },
      {
          { AOM_CDF5(16496, 22487, 26738, 29774), 80 },
          { AOM_CDF5(14083, 19036, 23681, 32337), 37 },
          { AOM_CDF5(9937, 26045, 28714, 30794), 6 },
          { AOM_CDF5(28036, 29183, 29685, 31669), 67 },
          { AOM_CDF5(29823, 31034, 32219, 32443), 76 },
          { AOM_CDF5(6554, 13107, 19661, 26214), 50 },
      },
      {
          { AOM_CDF6(14849, 17447, 21892, 24855, 28077), 1 },
          { AOM_CDF6(10853, 17460, 21725, 23828, 29985), 11 },
          { AOM_CDF6(7527, 24992, 27049, 28997, 30771), 82 },
          { AOM_CDF6(20014, 21423, 22498, 27848, 31860), 25 },
          { AOM_CDF6(30128, 30915, 31585, 31956, 32349), 106 },
          { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
      },
      {
          { AOM_CDF7(21398, 22447, 23517, 25195, 27523, 29495), 15 },
          { AOM_CDF7(7172, 14795, 20593, 23470, 26798, 30857), 0 },
          { AOM_CDF7(8288, 24218, 25902, 28493, 29804, 31810), 2 },
          { AOM_CDF7(17010, 19420, 20671, 23267, 27901, 32212), 0 },
          { AOM_CDF7(30604, 30960, 31225, 31563, 32015, 32485), 75 },
          { AOM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), 0 },
      },
      {
          { AOM_CDF8(19018, 20396, 21284, 23458, 26306, 28266, 30165), 90 },
          { AOM_CDF8(7862, 14052, 17890, 21130, 23895, 27712, 30539), 7 },
          { AOM_CDF8(7378, 23854, 25482, 27006, 28634, 30493, 31798), 7 },
          { AOM_CDF8(18860, 22253, 25202, 27093, 29374, 30876, 31822), 77 },
          { AOM_CDF8(27977, 28613, 29552, 30099, 30680, 31316, 32198), 76 },
          { AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672), 0 },
      },
    };
#else
#if CONFIG_PALETTE_LINE_COPY
static const aom_cdf_prob
    default_identity_row_cdf_y[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(3)] = {
      { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }
    };
static const aom_cdf_prob
    default_identity_row_cdf_uv[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(3)] = {
      { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) },
      { AOM_CDF3(10923, 21845) }
    };
static const aom_cdf_prob default_palette_direction_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16384)
};
#else
static const aom_cdf_prob
    default_identity_row_cdf_y[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) }
    };
static const aom_cdf_prob
    default_identity_row_cdf_uv[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) }
    };
#endif  // CONFIG_PALETTE_LINE_COPY

static const aom_cdf_prob default_palette_y_color_index_cdf
    [PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
      {
          { AOM_CDF2(28710) },
          { AOM_CDF2(16384) },
          { AOM_CDF2(10553) },
          { AOM_CDF2(27036) },
          { AOM_CDF2(31603) },
          { AOM_CDF2(28710) },
      },
      {
          { AOM_CDF3(27877, 30490) },
          { AOM_CDF3(11532, 25697) },
          { AOM_CDF3(6544, 30234) },
          { AOM_CDF3(23018, 28072) },
          { AOM_CDF3(31915, 32385) },
          { AOM_CDF3(27877, 30490) },
      },
      {
          { AOM_CDF4(25572, 28046, 30045) },
          { AOM_CDF4(9478, 21590, 27256) },
          { AOM_CDF4(7248, 26837, 29824) },
          { AOM_CDF4(19167, 24486, 28349) },
          { AOM_CDF4(31400, 31825, 32250) },
          { AOM_CDF4(25572, 28046, 30045) },
      },
      {
          { AOM_CDF5(24779, 26955, 28576, 30282) },
          { AOM_CDF5(8669, 20364, 24073, 28093) },
          { AOM_CDF5(4255, 27565, 29377, 31067) },
          { AOM_CDF5(19864, 23674, 26716, 29530) },
          { AOM_CDF5(31646, 31893, 32147, 32426) },
          { AOM_CDF5(24779, 26955, 28576, 30282) },
      },
      {
          { AOM_CDF6(23132, 25407, 26970, 28435, 30073) },
          { AOM_CDF6(7443, 17242, 20717, 24762, 27982) },
          { AOM_CDF6(6300, 24862, 26944, 28784, 30671) },
          { AOM_CDF6(18916, 22895, 25267, 27435, 29652) },
          { AOM_CDF6(31270, 31550, 31808, 32059, 32353) },
          { AOM_CDF6(23132, 25407, 26970, 28435, 30073) },
      },
      {
          { AOM_CDF7(23105, 25199, 26464, 27684, 28931, 30318) },
          { AOM_CDF7(6950, 15447, 18952, 22681, 25567, 28563) },
          { AOM_CDF7(7560, 23474, 25490, 27203, 28921, 30708) },
          { AOM_CDF7(18544, 22373, 24457, 26195, 28119, 30045) },
          { AOM_CDF7(31198, 31451, 31670, 31882, 32123, 32391) },
          { AOM_CDF7(23105, 25199, 26464, 27684, 28931, 30318) },
      },
      {
          { AOM_CDF8(21689, 23883, 25163, 26352, 27506, 28827, 30195) },
          { AOM_CDF8(6892, 15385, 17840, 21606, 24287, 26753, 29204) },
          { AOM_CDF8(5651, 23182, 25042, 26518, 27982, 29392, 30900) },
          { AOM_CDF8(19349, 22578, 24418, 25994, 27524, 29031, 30448) },
          { AOM_CDF8(31028, 31270, 31504, 31705, 31927, 32153, 32392) },
          { AOM_CDF8(21689, 23883, 25163, 26352, 27506, 28827, 30195) },
      },
    };
static const aom_cdf_prob default_palette_uv_color_index_cdf
    [PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
      {
          { AOM_CDF2(29089) },
          { AOM_CDF2(16384) },
          { AOM_CDF2(8713) },
          { AOM_CDF2(29257) },
          { AOM_CDF2(31610) },
          { AOM_CDF2(29089) },
      },
      {
          { AOM_CDF3(25257, 29145) },
          { AOM_CDF3(12287, 27293) },
          { AOM_CDF3(7033, 27960) },
          { AOM_CDF3(20145, 25405) },
          { AOM_CDF3(30608, 31639) },
          { AOM_CDF3(25257, 29145) },
      },
      {
          { AOM_CDF4(24210, 27175, 29903) },
          { AOM_CDF4(9888, 22386, 27214) },
          { AOM_CDF4(5901, 26053, 29293) },
          { AOM_CDF4(18318, 22152, 28333) },
          { AOM_CDF4(30459, 31136, 31926) },
          { AOM_CDF4(24210, 27175, 29903) },
      },
      {
          { AOM_CDF5(22980, 25479, 27781, 29986) },
          { AOM_CDF5(8413, 21408, 24859, 28874) },
          { AOM_CDF5(2257, 29449, 30594, 31598) },
          { AOM_CDF5(19189, 21202, 25915, 28620) },
          { AOM_CDF5(31844, 32044, 32281, 32518) },
          { AOM_CDF5(22980, 25479, 27781, 29986) },
      },
      {
          { AOM_CDF6(22217, 24567, 26637, 28683, 30548) },
          { AOM_CDF6(7307, 16406, 19636, 24632, 28424) },
          { AOM_CDF6(4441, 25064, 26879, 28942, 30919) },
          { AOM_CDF6(17210, 20528, 23319, 26750, 29582) },
          { AOM_CDF6(30674, 30953, 31396, 31735, 32207) },
          { AOM_CDF6(22217, 24567, 26637, 28683, 30548) },
      },
      {
          { AOM_CDF7(21239, 23168, 25044, 26962, 28705, 30506) },
          { AOM_CDF7(6545, 15012, 18004, 21817, 25503, 28701) },
          { AOM_CDF7(3448, 26295, 27437, 28704, 30126, 31442) },
          { AOM_CDF7(15889, 18323, 21704, 24698, 26976, 29690) },
          { AOM_CDF7(30988, 31204, 31479, 31734, 31983, 32325) },
          { AOM_CDF7(21239, 23168, 25044, 26962, 28705, 30506) },
      },
      {
          { AOM_CDF8(21442, 23288, 24758, 26246, 27649, 28980, 30563) },
          { AOM_CDF8(5863, 14933, 17552, 20668, 23683, 26411, 29273) },
          { AOM_CDF8(3415, 25810, 26877, 27990, 29223, 30394, 31618) },
          { AOM_CDF8(17965, 20084, 22232, 23974, 26274, 28402, 30390) },
          { AOM_CDF8(31190, 31329, 31516, 31679, 31825, 32026, 32322) },
          { AOM_CDF8(21442, 23288, 24758, 26246, 27649, 28980, 30563) },
      },
    };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob default_palette_y_color_index_cdf
    [PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
      {
          { AOM_CDF2(28710) },
          { AOM_CDF2(16384) },
          { AOM_CDF2(10553) },
          { AOM_CDF2(27036) },
          { AOM_CDF2(31603) },
      },
      {
          { AOM_CDF3(27877, 30490) },
          { AOM_CDF3(11532, 25697) },
          { AOM_CDF3(6544, 30234) },
          { AOM_CDF3(23018, 28072) },
          { AOM_CDF3(31915, 32385) },
      },
      {
          { AOM_CDF4(25572, 28046, 30045) },
          { AOM_CDF4(9478, 21590, 27256) },
          { AOM_CDF4(7248, 26837, 29824) },
          { AOM_CDF4(19167, 24486, 28349) },
          { AOM_CDF4(31400, 31825, 32250) },
      },
      {
          { AOM_CDF5(24779, 26955, 28576, 30282) },
          { AOM_CDF5(8669, 20364, 24073, 28093) },
          { AOM_CDF5(4255, 27565, 29377, 31067) },
          { AOM_CDF5(19864, 23674, 26716, 29530) },
          { AOM_CDF5(31646, 31893, 32147, 32426) },
      },
      {
          { AOM_CDF6(23132, 25407, 26970, 28435, 30073) },
          { AOM_CDF6(7443, 17242, 20717, 24762, 27982) },
          { AOM_CDF6(6300, 24862, 26944, 28784, 30671) },
          { AOM_CDF6(18916, 22895, 25267, 27435, 29652) },
          { AOM_CDF6(31270, 31550, 31808, 32059, 32353) },
      },
      {
          { AOM_CDF7(23105, 25199, 26464, 27684, 28931, 30318) },
          { AOM_CDF7(6950, 15447, 18952, 22681, 25567, 28563) },
          { AOM_CDF7(7560, 23474, 25490, 27203, 28921, 30708) },
          { AOM_CDF7(18544, 22373, 24457, 26195, 28119, 30045) },
          { AOM_CDF7(31198, 31451, 31670, 31882, 32123, 32391) },
      },
      {
          { AOM_CDF8(21689, 23883, 25163, 26352, 27506, 28827, 30195) },
          { AOM_CDF8(6892, 15385, 17840, 21606, 24287, 26753, 29204) },
          { AOM_CDF8(5651, 23182, 25042, 26518, 27982, 29392, 30900) },
          { AOM_CDF8(19349, 22578, 24418, 25994, 27524, 29031, 30448) },
          { AOM_CDF8(31028, 31270, 31504, 31705, 31927, 32153, 32392) },
      },
    };

static const aom_cdf_prob default_palette_uv_color_index_cdf
    [PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)] = {
      {
          { AOM_CDF2(29089) },
          { AOM_CDF2(16384) },
          { AOM_CDF2(8713) },
          { AOM_CDF2(29257) },
          { AOM_CDF2(31610) },
      },
      {
          { AOM_CDF3(25257, 29145) },
          { AOM_CDF3(12287, 27293) },
          { AOM_CDF3(7033, 27960) },
          { AOM_CDF3(20145, 25405) },
          { AOM_CDF3(30608, 31639) },
      },
      {
          { AOM_CDF4(24210, 27175, 29903) },
          { AOM_CDF4(9888, 22386, 27214) },
          { AOM_CDF4(5901, 26053, 29293) },
          { AOM_CDF4(18318, 22152, 28333) },
          { AOM_CDF4(30459, 31136, 31926) },
      },
      {
          { AOM_CDF5(22980, 25479, 27781, 29986) },
          { AOM_CDF5(8413, 21408, 24859, 28874) },
          { AOM_CDF5(2257, 29449, 30594, 31598) },
          { AOM_CDF5(19189, 21202, 25915, 28620) },
          { AOM_CDF5(31844, 32044, 32281, 32518) },
      },
      {
          { AOM_CDF6(22217, 24567, 26637, 28683, 30548) },
          { AOM_CDF6(7307, 16406, 19636, 24632, 28424) },
          { AOM_CDF6(4441, 25064, 26879, 28942, 30919) },
          { AOM_CDF6(17210, 20528, 23319, 26750, 29582) },
          { AOM_CDF6(30674, 30953, 31396, 31735, 32207) },
      },
      {
          { AOM_CDF7(21239, 23168, 25044, 26962, 28705, 30506) },
          { AOM_CDF7(6545, 15012, 18004, 21817, 25503, 28701) },
          { AOM_CDF7(3448, 26295, 27437, 28704, 30126, 31442) },
          { AOM_CDF7(15889, 18323, 21704, 24698, 26976, 29690) },
          { AOM_CDF7(30988, 31204, 31479, 31734, 31983, 32325) },
      },
      {
          { AOM_CDF8(21442, 23288, 24758, 26246, 27649, 28980, 30563) },
          { AOM_CDF8(5863, 14933, 17552, 20668, 23683, 26411, 29273) },
          { AOM_CDF8(3415, 25810, 26877, 27990, 29223, 30394, 31618) },
          { AOM_CDF8(17965, 20084, 22232, 23974, 26274, 28402, 30390) },
          { AOM_CDF8(31190, 31329, 31516, 31679, 31825, 32026, 32322) },
      },
    };
#endif  // CONFIG_PALETTE_IMPROVEMENTS

#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_IMPROVEIDTX
static const aom_cdf_prob
    default_txfm_do_partition_cdf[FSC_MODES][2][TXFM_SPLIT_GROUP]
                                 [CDF_SIZE(2)] = {
                                   {
                                       {
                                           { AOM_CDF2(20283), 30 },
                                           { AOM_CDF2(30337), 0 },
                                           { AOM_CDF2(24506), 6 },
                                           { AOM_CDF2(26359), 0 },
                                           { AOM_CDF2(22519), 1 },
                                           { AOM_CDF2(26216), 0 },
                                           { AOM_CDF2(24951), 1 },
                                           { AOM_CDF2(25620), 1 },
                                           { AOM_CDF2(18120), 0 },
                                       },
                                       {
                                           { AOM_CDF2(21323), 55 },
                                           { AOM_CDF2(28319), 30 },
                                           { AOM_CDF2(26036), 1 },
                                           { AOM_CDF2(21978), 6 },
                                           { AOM_CDF2(24924), 6 },
                                           { AOM_CDF2(25786), 1 },
                                           { AOM_CDF2(28041), 1 },
                                           { AOM_CDF2(30178), 0 },
                                           { AOM_CDF2(24573), 1 },
                                       },
                                   },
                                   {
                                       {
                                           { AOM_CDF2(25064), 25 },
                                           { AOM_CDF2(30878), 0 },
                                           { AOM_CDF2(26564), 31 },
                                           { AOM_CDF2(28487), 34 },
                                           { AOM_CDF2(29119), 31 },
                                           { AOM_CDF2(29145), 35 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(24798), 1 },
                                       },
                                       {
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                           { AOM_CDF2(16384), 0 },
                                       },
                                   },
                                 };
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
static const aom_cdf_prob default_txfm_2or3_way_partition_type_cdf
    [FSC_MODES][2][TX_PARTITION_TYPE_NUM_VERT_OR_HORZ - 1][CDF_SIZE(2)] = {
      {
          {
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
          },
          {
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
          },
      },
      {
          {
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
          },
          {
              { AOM_CDF2(16384), 0 },
              { AOM_CDF2(16384), 0 },
          },
      },
    };
static const aom_cdf_prob default_txfm_4way_partition_type_cdf
    [FSC_MODES][2][TX_PARTITION_TYPE_NUM_VERT_AND_HORZ]
    [CDF_SIZE(TX_PARTITION_TYPE_NUM)] = {
      {
          {
              { AOM_CDF5(32751, 32755, 32759, 32763), 0 },
              { AOM_CDF5(20802, 25030, 26995, 32740), 31 },
              { AOM_CDF5(20182, 23705, 26405, 26409), 6 },
              { AOM_CDF5(20751, 23141, 25139, 28909), 6 },
              { AOM_CDF5(17326, 22603, 23766, 30230), 32 },
              { AOM_CDF5(17851, 19208, 23924, 25954), 6 },
              { AOM_CDF5(24058, 25552, 26862, 29426), 7 },
              { AOM_CDF5(21828, 23313, 24974, 27712), 32 },
              { AOM_CDF5(23017, 24906, 25944, 29589), 32 },
              { AOM_CDF5(16350, 20009, 22555, 27368), 32 },
              { AOM_CDF5(20664, 24212, 26338, 32740), 31 },
              { AOM_CDF5(19545, 22293, 25070, 25074), 32 },
              { AOM_CDF5(19646, 21407, 23167, 28403), 32 },
              { AOM_CDF5(19716, 21727, 23149, 27062), 32 },
          },
          {
              { AOM_CDF5(21425, 26046, 31928, 32348), 31 },
              { AOM_CDF5(15582, 18790, 21769, 32539), 6 },
              { AOM_CDF5(18853, 20873, 22444, 22668), 6 },
              { AOM_CDF5(13126, 16246, 18357, 22212), 7 },
              { AOM_CDF5(16899, 19309, 20106, 29011), 31 },
              { AOM_CDF5(16171, 17392, 20548, 23669), 31 },
              { AOM_CDF5(21215, 23286, 25314, 28383), 37 },
              { AOM_CDF5(17021, 18215, 19362, 25723), 37 },
              { AOM_CDF5(15657, 16965, 18137, 22984), 62 },
              { AOM_CDF5(11119, 13843, 15897, 22593), 62 },
              { AOM_CDF5(14637, 18457, 22669, 32740), 32 },
              { AOM_CDF5(13145, 17976, 22158, 22168), 31 },
              { AOM_CDF5(11363, 12537, 13289, 22514), 57 },
              { AOM_CDF5(13238, 15047, 16184, 22635), 62 },
          },
      },
      {
          {
              { AOM_CDF5(30093, 30762, 31431, 32099), 60 },
              { AOM_CDF5(4766, 14299, 23831, 32172), 1 },
              { AOM_CDF5(4454, 12089, 22270, 22588), 31 },
              { AOM_CDF5(5958, 13902, 20852, 25817), 47 },
              { AOM_CDF5(9362, 17164, 24966, 28087), 45 },
              { AOM_CDF5(8856, 12399, 17712, 25683), 44 },
              { AOM_CDF5(3781, 11343, 17644, 28987), 45 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 15729, 23593, 31457), 100 },
              { AOM_CDF5(6342, 16913, 24312, 25369), 75 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
      },
    };
#else
static const aom_cdf_prob default_txfm_4way_partition_type_cdf
    [FSC_MODES][2][TXFM_PARTITION_GROUP - 1]
    [CDF_SIZE(TX_PARTITION_TYPE_NUM)] = {
      {
          {
              { AOM_CDF5(32751, 32755, 32759, 32763), 0 },
              { AOM_CDF5(20802, 25030, 26995, 32740), 31 },
              { AOM_CDF5(20182, 23705, 26405, 26409), 6 },
              { AOM_CDF5(20751, 23141, 25139, 28909), 6 },
              { AOM_CDF5(17326, 22603, 23766, 30230), 32 },
              { AOM_CDF5(17851, 19208, 23924, 25954), 6 },
              { AOM_CDF5(24058, 25552, 26862, 29426), 7 },
              { AOM_CDF5(21828, 23313, 24974, 27712), 32 },
              { AOM_CDF5(23017, 24906, 25944, 29589), 32 },
              { AOM_CDF5(16350, 20009, 22555, 27368), 32 },
              { AOM_CDF5(27790, 32748, 32752, 32756), 31 },
              { AOM_CDF5(27379, 32748, 32752, 32756), 31 },
              { AOM_CDF5(20664, 24212, 26338, 32740), 31 },
              { AOM_CDF5(19545, 22293, 25070, 25074), 32 },
              { AOM_CDF5(19646, 21407, 23167, 28403), 32 },
              { AOM_CDF5(19716, 21727, 23149, 27062), 32 },
          },
          {
              { AOM_CDF5(21425, 26046, 31928, 32348), 31 },
              { AOM_CDF5(15582, 18790, 21769, 32539), 6 },
              { AOM_CDF5(18853, 20873, 22444, 22668), 6 },
              { AOM_CDF5(13126, 16246, 18357, 22212), 7 },
              { AOM_CDF5(16899, 19309, 20106, 29011), 31 },
              { AOM_CDF5(16171, 17392, 20548, 23669), 31 },
              { AOM_CDF5(21215, 23286, 25314, 28383), 37 },
              { AOM_CDF5(17021, 18215, 19362, 25723), 37 },
              { AOM_CDF5(15657, 16965, 18137, 22984), 62 },
              { AOM_CDF5(11119, 13843, 15897, 22593), 62 },
              { AOM_CDF5(25746, 32329, 32475, 32622), 62 },
              { AOM_CDF5(25422, 32478, 32575, 32671), 62 },
              { AOM_CDF5(14637, 18457, 22669, 32740), 32 },
              { AOM_CDF5(13145, 17976, 22158, 22168), 31 },
              { AOM_CDF5(11363, 12537, 13289, 22514), 57 },
              { AOM_CDF5(13238, 15047, 16184, 22635), 62 },
          },
      },
      {
          {
              { AOM_CDF5(30093, 30762, 31431, 32099), 60 },
              { AOM_CDF5(4766, 14299, 23831, 32172), 1 },
              { AOM_CDF5(4454, 12089, 22270, 22588), 31 },
              { AOM_CDF5(5958, 13902, 20852, 25817), 47 },
              { AOM_CDF5(9362, 17164, 24966, 28087), 45 },
              { AOM_CDF5(8856, 12399, 17712, 25683), 44 },
              { AOM_CDF5(3781, 11343, 17644, 28987), 45 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(23255, 31711, 32063, 32416), 37 },
              { AOM_CDF5(26331, 31890, 32183, 32475), 37 },
              { AOM_CDF5(6554, 15729, 23593, 31457), 100 },
              { AOM_CDF5(6342, 16913, 24312, 25369), 75 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
      },
    };
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#else
static const aom_cdf_prob
    default_txfm_do_partition_cdf[2][TXFM_SPLIT_GROUP][CDF_SIZE(2)] = {
      { { AOM_CDF2(20632), 0 },
        { AOM_CDF2(29862), 0 },
        { AOM_CDF2(23828), 0 },
        { AOM_CDF2(26060), 0 },
        { AOM_CDF2(21902), 0 },
        { AOM_CDF2(25710), 0 },
        { AOM_CDF2(24489), 0 },
        { AOM_CDF2(25587), 0 },
        { AOM_CDF2(17164), 0 } },
      { { AOM_CDF2(24794), 0 },
        { AOM_CDF2(31474), 0 },
        { AOM_CDF2(25563), 0 },
        { AOM_CDF2(21545), 0 },
        { AOM_CDF2(22498), 0 },
        { AOM_CDF2(24370), 0 },
        { AOM_CDF2(11806), 0 },
        { AOM_CDF2(29665), 0 },
        { AOM_CDF2(23174), 0 } }
    };

static const aom_cdf_prob
    default_txfm_4way_partition_type_cdf[2][TXFM_PARTITION_GROUP - 1][CDF_SIZE(
        TX_PARTITION_TYPE_NUM)] = {
      { { AOM_CDF5(32752, 32756, 32760, 32764), 0 },
        { AOM_CDF5(16549, 22822, 25872, 32740), 0 },
        { AOM_CDF5(15340, 20500, 25966, 25970), 0 },
        { AOM_CDF5(18230, 21466, 24216, 28561), 0 },
        { AOM_CDF5(14935, 22264, 23398, 30368), 0 },
        { AOM_CDF5(15649, 17012, 23660, 25757), 0 },
        { AOM_CDF5(23648, 25383, 26749, 29566), 0 },
        { AOM_CDF5(21521, 23074, 25020, 27669), 0 },
        { AOM_CDF5(22766, 25127, 26100, 29626), 0 },
        { AOM_CDF5(15863, 19633, 22458, 27238), 0 },
        { AOM_CDF5(25257, 32748, 32752, 32756), 0 },
        { AOM_CDF5(24393, 32748, 32752, 32756), 0 },
        { AOM_CDF5(18894, 23485, 25768, 32740), 0 },
        { AOM_CDF5(17643, 20372, 24334, 24338), 0 },
        { AOM_CDF5(19275, 21416, 23255, 28161), 0 },
        { AOM_CDF5(19317, 21686, 23361, 26871), 0 } },
      { { AOM_CDF5(19661, 28231, 31760, 32264), 0 },
        { AOM_CDF5(16550, 18677, 20272, 32702), 0 },
        { AOM_CDF5(15124, 18085, 19283, 19346), 0 },
        { AOM_CDF5(13731, 15655, 17229, 23585), 0 },
        { AOM_CDF5(17776, 20731, 21624, 29587), 0 },
        { AOM_CDF5(17883, 19176, 22070, 24907), 0 },
        { AOM_CDF5(22221, 24828, 27231, 29368), 0 },
        { AOM_CDF5(17078, 18451, 19686, 25478), 0 },
        { AOM_CDF5(15594, 17509, 19179, 24413), 0 },
        { AOM_CDF5(9877, 12942, 15665, 22406), 0 },
        { AOM_CDF5(24067, 32615, 32666, 32717), 0 },
        { AOM_CDF5(22884, 32661, 32696, 32732), 0 },
        { AOM_CDF5(14173, 17393, 21695, 32740), 0 },
        { AOM_CDF5(13697, 18582, 22134, 22138), 0 },
        { AOM_CDF5(10851, 12023, 13254, 22599), 0 },
        { AOM_CDF5(13302, 15600, 17200, 24322), 0 } }
    };
#endif  // CONFIG_IMPROVEIDTX
#else
static const aom_cdf_prob
    default_txfm_do_partition_cdf[2][TXFM_PARTITION_GROUP][CDF_SIZE(2)] = {
      { // intra
        { AOM_CDF2(20611) },
        { AOM_CDF2(24192) },
        { AOM_CDF2(18182) },
        { AOM_CDF2(24924) },
        { AOM_CDF2(23143) },
        { AOM_CDF2(27141) },
        { AOM_CDF2(26227) },
        { AOM_CDF2(29654) } },
      { // inter
        { AOM_CDF2(25160) },
        { AOM_CDF2(27750) },
        { AOM_CDF2(26384) },
        { AOM_CDF2(26847) },
        { AOM_CDF2(21474) },
        { AOM_CDF2(22726) },
        { AOM_CDF2(29609) },
        { AOM_CDF2(30945) } }
    };
static const aom_cdf_prob
    default_txfm_4way_partition_type_cdf[2][TXFM_PARTITION_GROUP - 1][CDF_SIZE(
        3)] = { { // intra
                  { AOM_CDF3(32760, 32764) },
                  { AOM_CDF3(6342, 22640) },
                  { AOM_CDF3(14298, 22864) },
                  { AOM_CDF3(7287, 19333) },
                  { AOM_CDF3(19825, 28798) },
                  { AOM_CDF3(1824, 28283) },
                  { AOM_CDF3(9256, 24339) } },
                { // inter
                  { AOM_CDF3(16749, 24722) },
                  { AOM_CDF3(20629, 25838) },
                  { AOM_CDF3(21140, 25334) },
                  { AOM_CDF3(29351, 30800) },
                  { AOM_CDF3(28304, 30575) },
                  { AOM_CDF3(21051, 26911) },
                  { AOM_CDF3(11702, 22867) } } };
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#else
static const aom_cdf_prob default_inter_4way_txfm_partition_cdf
    [2][TXFM_PARTITION_INTER_CONTEXTS][CDF_SIZE(4)] = {
      {
          // Square
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) },
      },
      {
          // Rectangular
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) }, { AOM_CDF4(28581, 29581, 29681) },
          { AOM_CDF4(28581, 29581, 29681) },
      }
    };
static const aom_cdf_prob default_inter_2way_txfm_partition_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(30531)
};
#endif  // CONFIG_TX_PARTITION_CTX
#else   // CONFIG_NEW_TX_PARTITION
static const aom_cdf_prob
    default_txfm_partition_cdf[TXFM_PARTITION_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(28581) }, { AOM_CDF2(23846) }, { AOM_CDF2(20847) },
      { AOM_CDF2(24315) }, { AOM_CDF2(18196) }, { AOM_CDF2(12133) },
      { AOM_CDF2(18791) }, { AOM_CDF2(10887) }, { AOM_CDF2(11005) },
      { AOM_CDF2(27179) }, { AOM_CDF2(20004) }, { AOM_CDF2(11281) },
      { AOM_CDF2(26549) }, { AOM_CDF2(19308) }, { AOM_CDF2(14224) },
      { AOM_CDF2(28015) }, { AOM_CDF2(21546) }, { AOM_CDF2(14400) },
      { AOM_CDF2(28165) }, { AOM_CDF2(22401) }, { AOM_CDF2(16088) }
    };
#endif  // CONFIG_NEW_TX_PARTITION

#if CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENTROPY_PARA && CONFIG_SKIP_MODE_ENHANCEMENT
static const aom_cdf_prob default_skip_txfm_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(23601), 0 }, { AOM_CDF2(12657), 75 }, { AOM_CDF2(3777), 90 },
  { AOM_CDF2(23222), 1 }, { AOM_CDF2(8799), 76 },  { AOM_CDF2(1437), 90 },
#else
static const aom_cdf_prob default_skip_txfm_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(21670) },
  { AOM_CDF2(17991) },
  { AOM_CDF2(5679) }
#if CONFIG_SKIP_MODE_ENHANCEMENT
  ,
  { AOM_CDF2(26686) },
  { AOM_CDF2(8797) },
  { AOM_CDF2(941) }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
#endif  // CONFIG_ENTROPY_PARA && CONFIG_SKIP_MODE_ENHANCEMENT
};
#else
static const aom_cdf_prob default_skip_txfm_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(31671) },
  { AOM_CDF2(16515) },
  { AOM_CDF2(4576) }
#if CONFIG_SKIP_MODE_ENHANCEMENT
  ,
  { AOM_CDF2(24549) },
  { AOM_CDF2(10887) },
  { AOM_CDF2(3576) }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
};

#endif  // CONFIG_NEW_CONTEXT_MODELING

#if CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_skip_mode_cdfs[SKIP_MODE_CONTEXTS]
                                                [CDF_SIZE(2)] = {
                                                  { AOM_CDF2(30093), 118 },
                                                  { AOM_CDF2(19983), 90 },
                                                  { AOM_CDF2(12096), 76 },
                                                };
#else
static const aom_cdf_prob default_skip_mode_cdfs[SKIP_MODE_CONTEXTS][CDF_SIZE(
    2)] = { { AOM_CDF2(32298) }, { AOM_CDF2(23079) }, { AOM_CDF2(7376) } };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob default_skip_mode_cdfs[SKIP_MODE_CONTEXTS][CDF_SIZE(
    2)] = { { AOM_CDF2(32621) }, { AOM_CDF2(20708) }, { AOM_CDF2(8127) } };
#endif

#if CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_comp_group_idx_cdfs[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(9916), 0 },  { AOM_CDF2(7647), 0 }, { AOM_CDF2(4172), 0 },
      { AOM_CDF2(6353), 1 },  { AOM_CDF2(3423), 0 }, { AOM_CDF2(4917), 6 },
      { AOM_CDF2(11013), 0 }, { AOM_CDF2(5843), 0 }, { AOM_CDF2(7222), 1 },
      { AOM_CDF2(3158), 1 },  { AOM_CDF2(2495), 0 }, { AOM_CDF2(4723), 3 },
    };
#else
static const aom_cdf_prob
    default_comp_group_idx_cdfs[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(18033) }, { AOM_CDF2(13290) }, { AOM_CDF2(12030) },
      { AOM_CDF2(7528) },  { AOM_CDF2(6722) },  { AOM_CDF2(9736) },
      { AOM_CDF2(9328) },  { AOM_CDF2(6401) },  { AOM_CDF2(8115) },
      { AOM_CDF2(3067) },  { AOM_CDF2(4355) },  { AOM_CDF2(6879) }
    };
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob
    default_comp_group_idx_cdfs[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(19384) }, { AOM_CDF2(19384) },
      { AOM_CDF2(21384) }, { AOM_CDF2(19384) }, { AOM_CDF2(19834) },
      { AOM_CDF2(15384) }, { AOM_CDF2(17384) }, { AOM_CDF2(17384) },
      { AOM_CDF2(20384) }, { AOM_CDF2(17384) }, { AOM_CDF2(17384) },
    };
#endif  // CONFIG_NEW_CONTEXT_MODELING

#if CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_intrabc_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(30958), 1 },
  { AOM_CDF2(19490), 0 },
  { AOM_CDF2(8708), 90 },
};
#else
static const aom_cdf_prob default_intrabc_cdf[INTRABC_CONTEXTS][CDF_SIZE(2)] = {
  { AOM_CDF2(32332) }, { AOM_CDF2(19186) }, { AOM_CDF2(3756) }
};
#endif  // CONFIG_ENTROPY_PARA
#else
static const aom_cdf_prob default_intrabc_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    30531) };
#endif  // CONFIG_NEW_CONTEXT_MODELING

#if CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_intrabc_mode_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(26560), 31
};

static const aom_cdf_prob default_intrabc_drl_idx_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(22959), 123 },
  { AOM_CDF2(19303), 123 },
  { AOM_CDF2(18859), 124 },
};
#else
static const aom_cdf_prob default_intrabc_mode_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16384) };
static const aom_cdf_prob
    default_intrabc_drl_idx_cdf[MAX_REF_BV_STACK_SIZE - 1][CDF_SIZE(2)] = {
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) }
    };
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_IBC_BV_IMPROVEMENT

#if CONFIG_IBC_SUBPEL_PRECISION
aom_cdf_prob
    default_intrabc_bv_precision_cdf[NUM_BV_PRECISION_CONTEXTS]
                                    [CDF_SIZE(NUM_ALLOWED_BV_PRECISIONS)] = {
                                      { AOM_CDF2(24576), 0 },
                                    };
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_filter_intra_mode_cdf[CDF_SIZE(
    FILTER_INTRA_MODES)] = { AOM_CDF5(7939, 11923, 16608, 28264), 0 };
#else
static const aom_cdf_prob default_filter_intra_mode_cdf[CDF_SIZE(
    FILTER_INTRA_MODES)] = { AOM_CDF5(8949, 12776, 17211, 29558) };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_MORPH_PRED
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_morph_pred_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(19186), 50 },
  { AOM_CDF2(16483), 1 },
  { AOM_CDF2(8242), 95 },
};
#else
static const aom_cdf_prob default_morph_pred_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(19186) }, { AOM_CDF2(16483) }, { AOM_CDF2(8242) }
};
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_MORPH_PRED

#if CONFIG_D149_CTX_MODELING_OPT
static const aom_cdf_prob default_filter_intra_cdfs[CDF_SIZE(2)] = {
  AOM_CDF2(23506), 0
};
#else
static const aom_cdf_prob
    default_filter_intra_cdfs[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
      { AOM_CDF2(4621) },  { AOM_CDF2(6743) },  { AOM_CDF2(5893) },
      { AOM_CDF2(7866) },  { AOM_CDF2(12551) }, { AOM_CDF2(9394) },
      { AOM_CDF2(12408) }, { AOM_CDF2(14301) }, { AOM_CDF2(12756) },
      { AOM_CDF2(22343) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(12770) }, { AOM_CDF2(10368) },
      { AOM_CDF2(20229) }, { AOM_CDF2(18101) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) },
#if CONFIG_EXT_RECUR_PARTITIONS
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
      { AOM_CDF2(16384) }, { AOM_CDF2(16384) }, { AOM_CDF2(16384) },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    };
#endif  // CONFIG_D149_CTX_MODELING_OPT

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_switchable_flex_restore_cdf[MAX_LR_FLEX_SWITCHABLE_BITS]
                                       [MAX_MB_PLANE][CDF_SIZE(2)] = {
                                         {
                                             { AOM_CDF2(21337), 37 },
                                             { AOM_CDF2(13763), 32 },
                                             { AOM_CDF2(13677), 37 },
                                         },
                                         {
                                             { AOM_CDF2(16384), 0 },
                                             { AOM_CDF2(16384), 0 },
                                             { AOM_CDF2(16384), 0 },
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
#else
static const aom_cdf_prob
    default_switchable_flex_restore_cdf[MAX_LR_FLEX_SWITCHABLE_BITS]
                                       [MAX_MB_PLANE][CDF_SIZE(2)] = {
                                         {
                                             { AOM_CDF2(20384) },
                                             { AOM_CDF2(20384) },
                                             { AOM_CDF2(20384) },
                                         },
                                         {
                                             { AOM_CDF2(20384) },
                                             { AOM_CDF2(20384) },
                                             { AOM_CDF2(20384) },
                                         },
                                         {
                                             { AOM_CDF2(24384) },
                                             { AOM_CDF2(24384) },
                                             { AOM_CDF2(24384) },
                                         },
                                         {
                                             { AOM_CDF2(20384) },
                                             { AOM_CDF2(20384) },
                                             { AOM_CDF2(20384) },
                                         },
                                       };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_wiener_restore_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16384), 0
};
#else
static const aom_cdf_prob default_wiener_restore_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    11570) };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
#if CONFIG_CCSO_IMPROVE
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
#else
static const aom_cdf_prob default_ccso_cdf[3][CDF_SIZE(2)] = {
  { AOM_CDF2(12979), 37 },
  { AOM_CDF2(16118), 37 },
  { AOM_CDF2(15153), 37 },
};
#endif  // CONFIG_CCSO_IMPROVE
#else
static const aom_cdf_prob default_ccso_cdf[CDF_SIZE(2)] = { AOM_CDF2(11570) };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_CDEF_ENHANCEMENTS
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
#endif  // CONFIG_CDEF_ENHANCEMENTS

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_sgrproj_restore_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(13795), 32
};
#else
static const aom_cdf_prob default_sgrproj_restore_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16855) };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_wienerns_length_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(16384), 61 },
  { AOM_CDF2(16384), 31 },
};
static const aom_cdf_prob default_wienerns_uv_sym_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(16384), 57
};
#else
static const aom_cdf_prob default_wienerns_length_cdf[2][CDF_SIZE(2)] = {
  { AOM_CDF2(16384) }, { AOM_CDF2(16384) }
};
static const aom_cdf_prob default_wienerns_uv_sym_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16384) };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob
    default_wienerns_4part_cdf[WIENERNS_4PART_CTX_MAX][CDF_SIZE(4)] = {
      { AOM_CDF4(16384, 24576, 28672), 7 },
    };
#else
static const aom_cdf_prob
    default_wienerns_4part_cdf[WIENERNS_4PART_CTX_MAX][CDF_SIZE(4)] = {
      { AOM_CDF4(16384, 24576, 28672) },
    };
#endif
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_wienerns_restore_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(6995), 37
};
#else
static const aom_cdf_prob default_wienerns_restore_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(12000)
};
#endif  // CONFIG_ENTROPY_PARA
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_pc_wiener_restore_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(14330), 8
};
#else
static const aom_cdf_prob default_pc_wiener_restore_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(10000)
};
#endif

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_merged_param_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(14319), 7
};
#else
static const aom_cdf_prob default_merged_param_cdf[CDF_SIZE(2)] = { AOM_CDF2(
    16855) };
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_DELTAQ_OPT
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_delta_q_cdf[CDF_SIZE(DELTA_Q_PROBS + 1)] = {
  AOM_CDF8(16594, 23325, 26424, 28225, 29358, 30099, 30613), 56
};
#else
static const aom_cdf_prob default_delta_q_cdf[CDF_SIZE(DELTA_Q_PROBS + 1)] = {
  AOM_CDF8(16594, 23325, 26424, 28225, 29358, 30099, 30613)
};
#endif  // CONFIG_ENTROPY_PARA
#else
#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_delta_q_cdf[CDF_SIZE(DELTA_Q_PROBS + 1)] = {
  AOM_CDF4(8192, 16384, 24576), 0
};
#else
static const aom_cdf_prob default_delta_q_cdf[CDF_SIZE(DELTA_Q_PROBS + 1)] = {
  AOM_CDF4(28160, 32120, 32677)
};
#endif  // CONFIG_ENTROPY_PARA
#endif

#if CONFIG_ENTROPY_PARA
static const aom_cdf_prob default_delta_lf_multi_cdf[4][CDF_SIZE(4)] = {
  { AOM_CDF4(28160, 32120, 32677), 0 },
  { AOM_CDF4(28160, 32120, 32677), 0 },
  { AOM_CDF4(28160, 32120, 32677), 0 },
  { AOM_CDF4(28160, 32120, 32677), 0 },
};

static const aom_cdf_prob default_delta_lf_cdf[CDF_SIZE(4)] = {
  AOM_CDF4(8192, 16384, 24576), 0
};
#else
static const aom_cdf_prob default_delta_lf_multi_cdf[FRAME_LF_COUNT][CDF_SIZE(
    DELTA_LF_PROBS + 1)] = { { AOM_CDF4(28160, 32120, 32677) },
                             { AOM_CDF4(28160, 32120, 32677) },
                             { AOM_CDF4(28160, 32120, 32677) },
                             { AOM_CDF4(28160, 32120, 32677) } };
static const aom_cdf_prob default_delta_lf_cdf[CDF_SIZE(DELTA_LF_PROBS + 1)] = {
  AOM_CDF4(28160, 32120, 32677)
};

#endif  // CONFIG_ENTROPY_PARA
// FIXME(someone) need real defaults here
static const aom_cdf_prob default_seg_tree_cdf[CDF_SIZE(MAX_SEGMENTS)] = {
#if CONFIG_EXT_SEG
  AOM_CDF16(2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384, 18432, 20480,
            22528, 24576, 26624, 28672, 30720)
#else   // CONFIG_EXT_SEG
  AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672)
#endif  // CONFIG_EXT_SEG
};

static const aom_cdf_prob
    default_segment_pred_cdf[SEG_TEMPORAL_PRED_CTXS][CDF_SIZE(2)] = {
      { AOM_CDF2(128 * 128) }, { AOM_CDF2(128 * 128) }, { AOM_CDF2(128 * 128) }
    };

static const aom_cdf_prob
    default_spatial_pred_seg_tree_cdf[SPATIAL_PREDICTION_PROBS][CDF_SIZE(
        MAX_SEGMENTS)] = {
#if CONFIG_EXT_SEG
      {
          // all different
          AOM_CDF16(5622, 6757, 7893, 11993, 16093, 17163, 18233, 23021, 27809,
                    28091, 28373, 30453, 32533, 32650, 32762),
      },
      {
          // either of three pair of two neighbors is the same
          AOM_CDF16(14274, 16252, 18230, 20393, 22557, 23746, 24935, 27362,
                    29980, 30415, 30851, 31597, 32344, 32553, 32762),
      },
      {
          // UL == L && UL == U, all the same
          AOM_CDF16(27527, 28007, 28487, 28605, 28723, 28806, 28890, 30643,
                    32397, 32522, 32647, 32663, 32679, 32720, 32762),
      },
#else   // CONFIG_EXT_SEG
      {
          AOM_CDF8(5622, 7893, 16093, 18233, 27809, 28373, 32533),
      },
      {
          AOM_CDF8(14274, 18230, 22557, 24935, 29980, 30851, 32344),
      },
      {
          AOM_CDF8(27527, 28487, 28723, 28890, 32397, 32647, 32679),
      },
#endif  // CONFIG_EXT_SEG
    };

#if CONFIG_NEW_TX_PARTITION
#if !CONFIG_TX_PARTITION_CTX
#if CONFIG_EXT_RECUR_PARTITIONS
static const aom_cdf_prob
    default_intra_4way_txfm_partition_cdf[2][TX_SIZE_CONTEXTS][CDF_SIZE(4)] = {
      { { AOM_CDF4(23833, 29543, 30199) },
        { AOM_CDF4(26803, 30401, 30864) },
        { AOM_CDF4(30480, 31851, 32016) } },
      { { AOM_CDF4(16056, 19436, 22911) },
        { AOM_CDF4(18856, 20728, 24099) },
        { AOM_CDF4(24218, 25110, 26664) } }
    };

static const aom_cdf_prob default_intra_2way_txfm_partition_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(21783)
};
#else
static const aom_cdf_prob
    default_intra_4way_txfm_partition_cdf[2][TX_SIZE_CONTEXTS][CDF_SIZE(4)] = {
      { { AOM_CDF4(19968, 20968, 21968) },
        { AOM_CDF4(19968, 20968, 21968) },
        { AOM_CDF4(24320, 25320, 26320) } },
      { { AOM_CDF4(12272, 13272, 14272) },
        { AOM_CDF4(12272, 13272, 14272) },
        { AOM_CDF4(18677, 19677, 20677) } },
    };
static const aom_cdf_prob default_intra_2way_txfm_partition_cdf[CDF_SIZE(2)] = {
  AOM_CDF2(30531)
};
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#endif  // !CONFIG_TX_PARTITION_CTX
#else   // CONFIG_NEW_TX_PARTITION
#if CONFIG_NEW_CONTEXT_MODELING
static const aom_cdf_prob default_tx_size_cdf[MAX_TX_CATS][TX_SIZE_CONTEXTS]
                                             [CDF_SIZE(MAX_TX_DEPTH + 1)] = {
                                               {
                                                   { AOM_CDF2(13970) },
                                                   { AOM_CDF2(19724) },
                                                   { AOM_CDF2(24258) },
                                               },
                                               {
                                                   { AOM_CDF3(19343, 32576) },
                                                   { AOM_CDF3(21600, 32366) },
                                                   { AOM_CDF3(25800, 32554) },
                                               },
                                               {
                                                   { AOM_CDF3(21737, 31741) },
                                                   { AOM_CDF3(21209, 30920) },
                                                   { AOM_CDF3(26144, 31763) },
                                               },
                                               {
                                                   { AOM_CDF3(18476, 31424) },
                                                   { AOM_CDF3(24024, 30870) },
                                                   { AOM_CDF3(28863, 32066) },
                                               },
                                             };
#else
static const aom_cdf_prob default_tx_size_cdf[MAX_TX_CATS][TX_SIZE_CONTEXTS]
                                             [CDF_SIZE(MAX_TX_DEPTH + 1)] = {
                                               { { AOM_CDF2(19968) },
                                                 { AOM_CDF2(19968) },
                                                 { AOM_CDF2(24320) } },
                                               { { AOM_CDF3(12272, 30172) },
                                                 { AOM_CDF3(12272, 30172) },
                                                 { AOM_CDF3(18677, 30848) } },
                                               { { AOM_CDF3(12986, 15180) },
                                                 { AOM_CDF3(12986, 15180) },
                                                 { AOM_CDF3(24302, 25602) } },
                                               { { AOM_CDF3(5782, 11475) },
                                                 { AOM_CDF3(5782, 11475) },
                                                 { AOM_CDF3(16803, 22759) } },
                                             };
#endif  // CONFIG_NEW_CONTEXT_MODELING
#endif  // CONFIG_NEW_TX_PARTITION

#if CONFIG_ENTROPY_PARA
#if CONFIG_IST_ANY_SET
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
#else
static const aom_cdf_prob default_stx_cdf[TX_SIZES][CDF_SIZE(STX_TYPES)] = {
  { AOM_CDF4(1542, 11565, 24287), 0 },  { AOM_CDF4(4776, 13664, 21624), 0 },
  { AOM_CDF4(7447, 17278, 24725), 0 },  { AOM_CDF4(5783, 17348, 21203), 0 },
  { AOM_CDF4(17873, 20852, 23831), 1 },
};
#endif  // CONFIG_IST_ANY_SET
#else
#if CONFIG_IST_ANY_SET
static const aom_cdf_prob default_stx_cdf[TX_SIZES][CDF_SIZE(STX_TYPES)] = {
  { AOM_CDF4(293, 11683, 25053) },
  { AOM_CDF4(2952, 9945, 16750) },
  { AOM_CDF4(2684, 9484, 16065) },
  { AOM_CDF4(3552, 10398, 15130) },
  { AOM_CDF4(10685, 14127, 17177) }
};
#else
static const aom_cdf_prob default_stx_cdf[TX_SIZES][CDF_SIZE(STX_TYPES)] = {
  { AOM_CDF4(1542, 11565, 24287) },  { AOM_CDF4(4776, 13664, 21624) },
  { AOM_CDF4(7447, 17278, 24725) },  { AOM_CDF4(5783, 17348, 21203) },
  { AOM_CDF4(17873, 20852, 23831) },
};
#endif  // CONFIG_IST_ANY_SET
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
#if CONFIG_IST_SET_FLAG
#if CONFIG_IST_ANY_SET
#if CONFIG_INTRA_TX_IST_PARSE
static const aom_cdf_prob
    default_most_probable_stx_set_cdf[CDF_SIZE(IST_DIR_SIZE)] = {
      AOM_CDF7(16328, 21408, 25613, 27672, 29722, 31413),
      75,
    };
#if CONFIG_F105_IST_MEM_REDUCE
static const aom_cdf_prob default_most_probable_stx_set_cdf_ADST_ADST[CDF_SIZE(
    IST_REDUCE_SET_SIZE_ADST_ADST)] = {
  AOM_CDF4(16328, 21408, 25613),
  75,
};
#endif  // CONFIG_F105_IST_MEM_REDUCE
#else
static const aom_cdf_prob
    default_stx_set_cdf[IST_DIR_SIZE][CDF_SIZE(IST_DIR_SIZE)] = {
      { AOM_CDF7(6417, 12393, 15085, 17442, 20031, 22297), 1 },
      { AOM_CDF7(3076, 18630, 19930, 21004, 24286, 25111), 0 },
      { AOM_CDF7(7046, 9228, 23532, 23815, 24012, 28205), 5 },
      { AOM_CDF7(863, 2168, 2359, 23667, 30807, 30954), 6 },
      { AOM_CDF7(563, 5103, 5254, 13098, 30865, 30997), 6 },
      { AOM_CDF7(5241, 6198, 10449, 10682, 10890, 29524), 1 },
      { AOM_CDF7(3803, 8413, 9289, 11494, 14019, 15456), 0 },
    };
#endif  // CONFIG_INTRA_TX_IST_PARSE
#else
static const aom_cdf_prob
    default_stx_set_cdf[IST_DIR_SIZE][CDF_SIZE(IST_DIR_SIZE)] = {
      { AOM_CDF7(32744, 32748, 32752, 32756, 32760, 32764), 0 },
      { AOM_CDF7(4, 32748, 32752, 32756, 32760, 32764), 0 },
      { AOM_CDF7(4, 8, 32752, 32756, 32760, 32764), 0 },
      { AOM_CDF7(4, 8, 12, 32756, 32760, 32764), 0 },
      { AOM_CDF7(4, 8, 12, 16, 32760, 32764), 0 },
      { AOM_CDF7(4, 8, 12, 16, 20, 32764), 0 },
      { AOM_CDF7(4, 8, 12, 16, 20, 24), 0 },
    };
#endif  // CONFIG_IST_ANY_SET
#endif  // CONFIG_IST_SET_FLAG
#else
#if CONFIG_IST_ANY_SET
static const aom_cdf_prob
    default_stx_set_cdf[IST_DIR_SIZE][CDF_SIZE(IST_DIR_SIZE)] = {
      { AOM_CDF7(5692, 11506, 13749, 15315, 17302, 19960) },
      { AOM_CDF7(3438, 17716, 19234, 20157, 22792, 23932) },
      { AOM_CDF7(6683, 9938, 19120, 19586, 20108, 25255) },
      { AOM_CDF7(1766, 4598, 5150, 20269, 28609, 29192) },
      { AOM_CDF7(1404, 7798, 8308, 14176, 29107, 29633) },
      { AOM_CDF7(5269, 7191, 11003, 11497, 12075, 27829) },
      { AOM_CDF7(4205, 9285, 10640, 12331, 14381, 16440) },
    };
#else
static const aom_cdf_prob
    default_stx_set_cdf[IST_DIR_SIZE][CDF_SIZE(IST_DIR_SIZE)] = {
      { AOM_CDF7(32744, 32748, 32752, 32756, 32760, 32764) },
      { AOM_CDF7(4, 32748, 32752, 32756, 32760, 32764) },
      { AOM_CDF7(4, 8, 32752, 32756, 32760, 32764) },
      { AOM_CDF7(4, 8, 12, 32756, 32760, 32764) },
      { AOM_CDF7(4, 8, 12, 16, 32760, 32764) },
      { AOM_CDF7(4, 8, 12, 16, 20, 32764) },
      { AOM_CDF7(4, 8, 12, 16, 20, 24) },
    };
#endif  // CONFIG_IST_ANY_SET
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ENTROPY_PARA
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
#else
static const aom_cdf_prob
    default_pb_mv_most_probable_precision_cdf[NUM_MV_PREC_MPP_CONTEXT][CDF_SIZE(
        2)] = { { AOM_CDF2(26227) }, { AOM_CDF2(22380) }, { AOM_CDF2(15446) } };
static const aom_cdf_prob default_pb_mv_precision_cdf
    [MV_PREC_DOWN_CONTEXTS][NUM_PB_FLEX_QUALIFIED_MAX_PREC]
    [CDF_SIZE(FLEX_MV_COSTS_SIZE)] = { { { AOM_CDF3(10923, 21845) },
                                         { AOM_CDF3(25702, 31870) },
                                         { AOM_CDF3(18150, 31007) } },
                                       { { AOM_CDF3(10923, 21845) },
                                         { AOM_CDF3(25055, 31858) },
                                         { AOM_CDF3(21049, 31413) } } };
#endif  // CONFIG_ENTROPY_PARA

#define MAX_COLOR_CONTEXT_HASH 8
#if !CONFIG_PALETTE_THREE_NEIGHBOR
// Negative values are invalid
static const int palette_color_index_context_lookup[MAX_COLOR_CONTEXT_HASH +
                                                    1] = { -1, -1, 0, -1, -1,
                                                           4,  3,  2, 1 };
#endif  // !CONFIG_PALETTE_THREE_NEIGHBOR

#define NUM_PALETTE_NEIGHBORS 3  // left, top-left and top.

#if CONFIG_PALETTE_THREE_NEIGHBOR
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
      int min_color = AOMMIN(color_neighbors[0], color_neighbors[2]);
      int max_color = AOMMAX(color_neighbors[0], color_neighbors[2]);
      swap_color_order(color_order, color_status, 0, min_color, &color_cnt);
      swap_color_order(color_order, color_status, 1, max_color, &color_cnt);
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
                                        int *color_idx
#if CONFIG_PALETTE_IMPROVEMENTS
                                        ,
                                        int row_flag, int prev_row_flag
#endif  // CONFIG_PALETTE_IMPROVEMENTS
) {
  assert(r > 0 || c > 0);

  int color_index_ctx =
      derive_color_index_ctx(color_order, color_idx, color_map, stride, r, c);
#if CONFIG_PALETTE_IMPROVEMENTS
  // Special context value for the first (and only) index of an identity row
  // and when the previous row is also an identity row.
  if (c == 0 && row_flag && prev_row_flag)
    color_index_ctx = PALETTE_COLOR_INDEX_CONTEXTS - 1;
#endif  // CONFIG_PALETTE_IMPROVEMENTS
  return color_index_ctx;
}
#else
int av1_get_palette_color_index_context(const uint8_t *color_map, int stride,
                                        int r, int c, int palette_size,
                                        uint8_t *color_order, int *color_idx
#if CONFIG_PALETTE_IMPROVEMENTS
                                        ,
                                        int row_flag, int prev_row_flag
#endif  // CONFIG_PALETTE_IMPROVEMENTS
) {
  assert(palette_size <= PALETTE_MAX_SIZE);
  assert(r > 0 || c > 0);

  // Get color indices of neighbors.
  int color_neighbors[NUM_PALETTE_NEIGHBORS];
  color_neighbors[0] = (c - 1 >= 0) ? color_map[r * stride + c - 1] : -1;
  color_neighbors[1] =
      (c - 1 >= 0 && r - 1 >= 0) ? color_map[(r - 1) * stride + c - 1] : -1;
  color_neighbors[2] = (r - 1 >= 0) ? color_map[(r - 1) * stride + c] : -1;

  // The +10 below should not be needed. But we get a warning "array subscript
  // is above array bounds [-Werror=array-bounds]" without it, possibly due to
  // this (or similar) bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59124
  int scores[PALETTE_MAX_SIZE + 10] = { 0 };
  int i;
  static const int weights[NUM_PALETTE_NEIGHBORS] = { 2, 1, 2 };
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    if (color_neighbors[i] >= 0) {
      scores[color_neighbors[i]] += weights[i];
    }
  }

  int inverse_color_order[PALETTE_MAX_SIZE];
  for (i = 0; i < PALETTE_MAX_SIZE; ++i) {
    color_order[i] = i;
    inverse_color_order[i] = i;
  }

  // Get the top NUM_PALETTE_NEIGHBORS scores (sorted from large to small).
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    int max = scores[i];
    int max_idx = i;
    for (int j = i + 1; j < palette_size; ++j) {
      if (scores[j] > max) {
        max = scores[j];
        max_idx = j;
      }
    }
    if (max_idx != i) {
      // Move the score at index 'max_idx' to index 'i', and shift the scores
      // from 'i' to 'max_idx - 1' by 1.
      const int max_score = scores[max_idx];
      const uint8_t max_color_order = color_order[max_idx];
      for (int k = max_idx; k > i; --k) {
        scores[k] = scores[k - 1];
        color_order[k] = color_order[k - 1];
        inverse_color_order[color_order[k]] = k;
      }
      scores[i] = max_score;
      color_order[i] = max_color_order;
      inverse_color_order[color_order[i]] = i;
    }
  }

  if (color_idx != NULL)
    *color_idx = inverse_color_order[color_map[r * stride + c]];

#if CONFIG_PALETTE_IMPROVEMENTS
  // Special context value for the first (and only) index of an identity row and
  // when the previous row is also an identity row.
  if (c == 0 && row_flag && prev_row_flag)
    return PALETTE_COLOR_INDEX_CONTEXTS - 1;
#endif  // CONFIG_PALETTE_IMPROVEMENTS

  // Get hash value of context.
  int color_index_ctx_hash = 0;
  static const int hash_multipliers[NUM_PALETTE_NEIGHBORS] = { 1, 2, 2 };
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    color_index_ctx_hash += scores[i] * hash_multipliers[i];
  }
  assert(color_index_ctx_hash > 0);
  assert(color_index_ctx_hash <= MAX_COLOR_CONTEXT_HASH);

  // Lookup context from hash.
  const int color_index_ctx =
      palette_color_index_context_lookup[color_index_ctx_hash];
  assert(color_index_ctx >= 0);
  assert(color_index_ctx < PALETTE_COLOR_INDEX_CONTEXTS);
  return color_index_ctx;
}
#endif  // CONFIG_PALETTE_THREE_NEIGHBOR

#if !CONFIG_PALETTE_THREE_NEIGHBOR
int av1_fast_palette_color_index_context(const uint8_t *color_map, int stride,
                                         int r, int c, int *color_idx
#if CONFIG_PALETTE_IMPROVEMENTS
                                         ,
                                         int row_flag, int prev_row_flag
#endif  // CONFIG_PALETTE_IMPROVEMENTS
) {
  assert(r > 0 || c > 0);

  // This goes in the order of left, top, and top-left. This has the advantage
  // that unless anything here are not distinct or invalid, this will already
  // be in sorted order. Furthermore, if either of the first two are not
  // invalid, we know the last one is also invalid.
  int color_neighbors[NUM_PALETTE_NEIGHBORS];
  color_neighbors[0] = (c - 1 >= 0) ? color_map[r * stride + c - 1] : -1;
  color_neighbors[1] = (r - 1 >= 0) ? color_map[(r - 1) * stride + c] : -1;
  color_neighbors[2] =
      (c - 1 >= 0 && r - 1 >= 0) ? color_map[(r - 1) * stride + c - 1] : -1;

  // Since our array is so small, using a couple if statements is faster
  int scores[NUM_PALETTE_NEIGHBORS] = { 2, 2, 1 };
  if (color_neighbors[0] == color_neighbors[1]) {
    scores[0] += scores[1];
    color_neighbors[1] = -1;

    if (color_neighbors[0] == color_neighbors[2]) {
      scores[0] += scores[2];
      color_neighbors[2] = -1;
    }
  } else if (color_neighbors[0] == color_neighbors[2]) {
    scores[0] += scores[2];
    color_neighbors[2] = -1;
  } else if (color_neighbors[1] == color_neighbors[2]) {
    scores[1] += scores[2];
    color_neighbors[2] = -1;
  }

  int color_rank[NUM_PALETTE_NEIGHBORS] = { -1, -1, -1 };
  int score_rank[NUM_PALETTE_NEIGHBORS] = { 0, 0, 0 };
  int num_valid_colors = 0;
  for (int idx = 0; idx < NUM_PALETTE_NEIGHBORS; idx++) {
    if (color_neighbors[idx] != -1) {
      score_rank[num_valid_colors] = scores[idx];
      color_rank[num_valid_colors] = color_neighbors[idx];
      num_valid_colors++;
    }
  }

  // Sort everything
  // We need to swap the first two elements if they have the same score but
  // the color indices are not in the right order
  if (score_rank[0] < score_rank[1] ||
      (score_rank[0] == score_rank[1] && color_rank[0] > color_rank[1])) {
    const int tmp_score = score_rank[0];
    const int tmp_color = color_rank[0];
    score_rank[0] = score_rank[1];
    color_rank[0] = color_rank[1];
    score_rank[1] = tmp_score;
    color_rank[1] = tmp_color;
  }
  if (score_rank[0] < score_rank[2]) {
    const int tmp_score = score_rank[0];
    const int tmp_color = color_rank[0];
    score_rank[0] = score_rank[2];
    color_rank[0] = color_rank[2];
    score_rank[2] = tmp_score;
    color_rank[2] = tmp_color;
  }
  if (score_rank[1] < score_rank[2]) {
    const int tmp_score = score_rank[1];
    const int tmp_color = color_rank[1];
    score_rank[1] = score_rank[2];
    color_rank[1] = color_rank[2];
    score_rank[2] = tmp_score;
    color_rank[2] = tmp_color;
  }

  if (color_idx != NULL) {
    // If any of the neighbor color has higher index than current color index,
    // then we move up by 1 unless the current color is the same as one of the
    // neighbor
    const int current_color = *color_idx = color_map[r * stride + c];
    int same_neighbor = -1;
    for (int idx = 0; idx < NUM_PALETTE_NEIGHBORS; idx++) {
      if (color_rank[idx] > current_color) {
        (*color_idx)++;
      } else if (color_rank[idx] == current_color) {
        same_neighbor = idx;
      }
    }
    if (same_neighbor != -1) {
      *color_idx = same_neighbor;
    }
  }

#if CONFIG_PALETTE_IMPROVEMENTS
  // Special context value for the first (and only) index of an identity row and
  // when the previous row is also an identity row.
  if (c == 0 && row_flag && prev_row_flag)
    return PALETTE_COLOR_INDEX_CONTEXTS - 1;
#endif  // CONFIG_PALETTE_IMPROVEMENTS

  // Get hash value of context.
  int color_index_ctx_hash = 0;
  static const int hash_multipliers[NUM_PALETTE_NEIGHBORS] = { 1, 2, 2 };
  for (int idx = 0; idx < NUM_PALETTE_NEIGHBORS; ++idx) {
    color_index_ctx_hash += score_rank[idx] * hash_multipliers[idx];
  }
  assert(color_index_ctx_hash > 0);
  assert(color_index_ctx_hash <= MAX_COLOR_CONTEXT_HASH);

  // Lookup context from hash.
  const int color_index_ctx =
      palette_color_index_context_lookup[color_index_ctx_hash];
  assert(color_index_ctx >= 0);
  assert(color_index_ctx < PALETTE_COLOR_INDEX_CONTEXTS);
  return color_index_ctx;
}
#endif  // !CONFIG_PALETTE_THREE_NEIGHBOR
#undef NUM_PALETTE_NEIGHBORS
#undef MAX_COLOR_CONTEXT_HASH

static void init_mode_probs(FRAME_CONTEXT *fc,
                            const SequenceHeader *const seq_params) {
  (void)seq_params;
  av1_copy(fc->palette_y_size_cdf, default_palette_y_size_cdf);
  av1_copy(fc->palette_uv_size_cdf, default_palette_uv_size_cdf);
#if CONFIG_PALETTE_IMPROVEMENTS
  av1_copy(fc->identity_row_cdf_y, default_identity_row_cdf_y);
  av1_copy(fc->identity_row_cdf_uv, default_identity_row_cdf_uv);
#if CONFIG_PALETTE_LINE_COPY
  av1_copy(fc->palette_direction_cdf, default_palette_direction_cdf);
#endif  // CONFIG_PALETTE_LINE_COPY
#endif  // CONFIG_PALETTE_IMPROVEMENTS
  av1_copy(fc->palette_y_color_index_cdf, default_palette_y_color_index_cdf);
  av1_copy(fc->palette_uv_color_index_cdf, default_palette_uv_color_index_cdf);
#if !CONFIG_AIMC
  av1_copy(fc->kf_y_cdf, default_kf_y_mode_cdf);
  av1_copy(fc->angle_delta_cdf, default_angle_delta_cdf);
#endif  // !CONFIG_AIMC
  av1_copy(fc->comp_inter_cdf, default_comp_inter_cdf);
  av1_copy(fc->tip_cdf, default_tip_cdf);
#if CONFIG_OPTIMIZE_CTX_TIP_WARP
  av1_copy(fc->tip_pred_mode_cdf, default_tip_pred_mode_cdf);
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
  av1_copy(fc->palette_y_mode_cdf, default_palette_y_mode_cdf);
  av1_copy(fc->palette_uv_mode_cdf, default_palette_uv_mode_cdf);
  av1_copy(fc->single_ref_cdf, default_single_ref_cdf);
  av1_copy(fc->comp_ref0_cdf, default_comp_ref0_cdf);
  av1_copy(fc->comp_ref1_cdf, default_comp_ref1_cdf);
#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
  av1_copy(fc->txfm_do_partition_cdf, default_txfm_do_partition_cdf);
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  av1_copy(fc->txfm_2or3_way_partition_type_cdf,
           default_txfm_2or3_way_partition_type_cdf);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  av1_copy(fc->txfm_4way_partition_type_cdf,
           default_txfm_4way_partition_type_cdf);
#else
  av1_copy(fc->inter_4way_txfm_partition_cdf,
           default_inter_4way_txfm_partition_cdf);
  av1_copy(fc->inter_2way_txfm_partition_cdf,
           default_inter_2way_txfm_partition_cdf);
#endif  // CONFIG_TX_PARTITION_CTX
#else
  av1_copy(fc->txfm_partition_cdf, default_txfm_partition_cdf);
#endif  // CONFIG_NEW_TX_PARTITION
  av1_copy(fc->comp_group_idx_cdf, default_comp_group_idx_cdfs);
  av1_copy(fc->inter_single_mode_cdf, default_inter_single_mode_cdf);

  av1_copy(fc->inter_warp_mode_cdf, default_inter_warp_mode_cdf);
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  av1_copy(fc->is_warpmv_or_warp_newmv_cdf,
           default_is_warpmv_or_warp_newmv_cdf);
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

#if CONFIG_OPT_INTER_MODE_CTX
  av1_copy(fc->drl_cdf, default_drl_cdf);
#else
#if CONFIG_ENTROPY_PARA
  av1_copy(fc->drl_cdf[0], default_drl0_cdf_refmvbank);
  av1_copy(fc->drl_cdf[1], default_drl1_cdf_refmvbank);
  av1_copy(fc->drl_cdf[2], default_drl2_cdf_refmvbank);
#else
  if (seq_params->enable_refmvbank) {
    av1_copy(fc->drl_cdf[0], default_drl0_cdf_refmvbank);
    av1_copy(fc->drl_cdf[1], default_drl1_cdf_refmvbank);
    av1_copy(fc->drl_cdf[2], default_drl2_cdf_refmvbank);
  } else {
    av1_copy(fc->drl_cdf[0], default_drl0_cdf);
    av1_copy(fc->drl_cdf[1], default_drl1_cdf);
    av1_copy(fc->drl_cdf[2], default_drl2_cdf);
  }
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_OPT_INTER_MODE_CTX

#if CONFIG_REFINEMV
  av1_copy(fc->refinemv_flag_cdf, default_refinemv_flag_cdf);
#endif  // CONFIG_REFINEMV
  av1_copy(fc->warp_causal_cdf, default_warp_causal_cdf);
#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  av1_copy(fc->warp_delta_cdf, default_warp_delta_cdf);
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  av1_copy(fc->warp_causal_warpmv_cdf, default_warp_causal_warpmv_cdf);
  av1_copy(fc->warp_ref_idx_cdf[0], default_warp_ref_idx0_cdf);
  av1_copy(fc->warp_ref_idx_cdf[1], default_warp_ref_idx1_cdf);
  av1_copy(fc->warp_ref_idx_cdf[2], default_warp_ref_idx2_cdf);
  av1_copy(fc->warpmv_with_mvd_flag_cdf, default_warpmv_with_mvd_flag_cdf);
#if CONFIG_WARP_PRECISION
  av1_copy(fc->warp_precision_idx_cdf, default_warp_precision_idx_cdf);
#endif  // CONFIG_WARP_PRECISION

  av1_copy(fc->warp_delta_param_cdf, default_warp_delta_param_cdf);
#if CONFIG_WARP_PRECISION
  av1_copy(fc->warp_delta_param_high_cdf, default_warp_delta_param_high_cdf);
  av1_copy(fc->warp_param_sign_cdf, default_warp_param_sign_cdf);
#endif  // CONFIG_WARP_PRECISION
  av1_copy(fc->warp_extend_cdf, default_warp_extend_cdf);
#if CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
  av1_copy(fc->skip_drl_cdf, default_skip_drl_cdf);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  av1_copy(fc->bawp_cdf[0], default_bawp_cdf[0]);
  av1_copy(fc->bawp_cdf[1], default_bawp_cdf[1]);
#else
  av1_copy(fc->bawp_cdf, default_bawp_cdf);
#endif  // CONFIG_BAWP_CHROMA
#endif  // CONFIG_BAWP
#if CONFIG_EXPLICIT_BAWP
  av1_copy(fc->explicit_bawp_cdf, default_explicit_bawp_cdf);
  av1_copy(fc->explicit_bawp_scale_cdf, default_explicit_bawp_scale_cdf);
#endif  // CONFIG_EXPLICIT_BAWP
  av1_copy(fc->use_optflow_cdf, default_use_optflow_cdf);

  av1_copy(fc->cwp_idx_cdf, default_cwp_idx_cdf);
  av1_copy(fc->jmvd_scale_mode_cdf, default_jmvd_scale_mode_cdf);
  av1_copy(fc->jmvd_amvd_scale_mode_cdf, default_jmvd_amvd_scale_mode_cdf);

#if CONFIG_INTER_COMPOUND_BY_JOINT
  av1_copy(fc->inter_compound_mode_is_joint_cdf,
           default_inter_compound_mode_is_joint_cdf);
  av1_copy(fc->inter_compound_mode_non_joint_type_cdf,
           default_inter_compound_mode_non_joint_type_cdf);
  av1_copy(fc->inter_compound_mode_joint_type_cdf,
           default_inter_compound_mode_joint_type_cdf);
#else
  av1_copy(fc->inter_compound_mode_cdf, default_inter_compound_mode_cdf);
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT

#if CONFIG_OPT_INTER_MODE_CTX
  av1_copy(fc->inter_compound_mode_same_refs_cdf,
           default_inter_compound_mode_same_refs_cdf);
#endif  // CONFIG_OPT_INTER_MODE_CTX
  av1_copy(fc->compound_type_cdf, default_compound_type_cdf);
#if CONFIG_WEDGE_MOD_EXT
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
#else
  av1_copy(fc->wedge_idx_cdf, default_wedge_idx_cdf);
#endif  // CONFIG_WEDGE_MOD_EXT
  av1_copy(fc->interintra_cdf, default_interintra_cdf);
#if CONFIG_WARP_INTER_INTRA
  av1_copy(fc->warp_interintra_cdf, default_warp_interintra_cdf);
#endif  // CONFIG_WARP_INTER_INTRA
  av1_copy(fc->wedge_interintra_cdf, default_wedge_interintra_cdf);
  av1_copy(fc->interintra_mode_cdf, default_interintra_mode_cdf);
  av1_copy(fc->seg.pred_cdf, default_segment_pred_cdf);
  av1_copy(fc->seg.tree_cdf, default_seg_tree_cdf);
  av1_copy(fc->filter_intra_cdfs, default_filter_intra_cdfs);
  av1_copy(fc->filter_intra_mode_cdf, default_filter_intra_mode_cdf);
  av1_copy(fc->switchable_flex_restore_cdf,
           default_switchable_flex_restore_cdf);
  av1_copy(fc->wiener_restore_cdf, default_wiener_restore_cdf);
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
#if CONFIG_ENTROPY_PARA
    av1_copy(fc->ccso_cdf[plane], default_ccso_cdf[plane]);
#else
    av1_copy(fc->ccso_cdf[plane], default_ccso_cdf);
#endif  // CONFIG_ENTROPY_PARA
  }
#if CONFIG_CDEF_ENHANCEMENTS
  av1_copy(fc->cdef_strength_index0_cdf, default_cdef_strength_index0_cdf);
  av1_copy(fc->cdef_cdf, default_cdef_cdf);
#endif  // CONFIG_CDEF_ENHANCEMENTS
  av1_copy(fc->sgrproj_restore_cdf, default_sgrproj_restore_cdf);
  av1_copy(fc->wienerns_restore_cdf, default_wienerns_restore_cdf);
  av1_copy(fc->wienerns_length_cdf, default_wienerns_length_cdf);
  av1_copy(fc->wienerns_uv_sym_cdf, default_wienerns_uv_sym_cdf);
  av1_copy(fc->wienerns_4part_cdf, default_wienerns_4part_cdf);
  av1_copy(fc->pc_wiener_restore_cdf, default_pc_wiener_restore_cdf);
  av1_copy(fc->merged_param_cdf, default_merged_param_cdf);
#if CONFIG_AIMC
  av1_copy(fc->y_mode_set_cdf, default_y_mode_set_cdf);
  av1_copy(fc->y_mode_idx_cdf_0, default_y_first_mode_cdf);
  av1_copy(fc->y_mode_idx_cdf_1, default_y_second_mode_cdf);
#else
  av1_copy(fc->y_mode_cdf, default_if_y_mode_cdf);
#endif  // CONFIG_AIMC
  av1_copy(fc->uv_mode_cdf, default_uv_mode_cdf);
#if CONFIG_AIMC
  av1_copy(fc->cfl_cdf, default_cfl_cdf);
#endif  // CONFIG_AIMC
  av1_copy(fc->mrl_index_cdf, default_mrl_index_cdf);
#if CONFIG_MRLS_IMPROVE
  av1_copy(fc->multi_line_mrl_cdf, default_multi_line_mrl_cdf);
#endif
  av1_copy(fc->fsc_mode_cdf, default_fsc_mode_cdf);
#if CONFIG_LOSSLESS_DPCM
  av1_copy(fc->dpcm_cdf, default_dpcm_cdf);
  av1_copy(fc->dpcm_vert_horz_cdf, default_dpcm_vert_horz_cdf);
  av1_copy(fc->dpcm_uv_cdf, default_dpcm_uv_cdf);
  av1_copy(fc->dpcm_uv_vert_horz_cdf, default_dpcm_uv_vert_horz_cdf);
#endif  // CONFIG_LOSSLESS_DPCM
  av1_copy(fc->cfl_index_cdf, default_cfl_index_cdf);
#if CONFIG_ENABLE_MHCCP
  av1_copy(fc->filter_dir_cdf, default_filter_dir_cdf);
#endif  // CONFIG_ENABLE_MHCCP
  av1_copy(fc->switchable_interp_cdf, default_switchable_interp_cdf);
#if CONFIG_EXTENDED_SDP
  av1_copy(fc->region_type_cdf, default_region_type_cdf);
#endif  // CONFIG_EXTENDED_SDP
#if CONFIG_EXT_RECUR_PARTITIONS
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
#else
  av1_copy(fc->partition_cdf, default_partition_cdf);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_TX_TYPE_FLEX_IMPROVE
  av1_copy(fc->intra_ext_tx_short_side_cdf,
           default_intra_ext_tx_short_side_cdf);
  av1_copy(fc->inter_ext_tx_short_side_cdf,
           default_inter_ext_tx_short_side_cdf);
  av1_copy(fc->tx_ext_32_cdf, default_tx_ext_32_cdf);
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
  av1_copy(fc->intra_ext_tx_cdf, default_intra_ext_tx_cdf);
  av1_copy(fc->inter_ext_tx_cdf, default_inter_ext_tx_cdf);
  av1_copy(fc->skip_mode_cdfs, default_skip_mode_cdfs);
  av1_copy(fc->skip_txfm_cdfs, default_skip_txfm_cdfs);
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  av1_copy(fc->intra_inter_cdf[0], default_intra_inter_cdf[0]);
  av1_copy(fc->intra_inter_cdf[1], default_intra_inter_cdf[1]);
#else
  av1_copy(fc->intra_inter_cdf, default_intra_inter_cdf);
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  for (int i = 0; i < SPATIAL_PREDICTION_PROBS; i++)
    av1_copy(fc->seg.spatial_pred_seg_cdf[i],
             default_spatial_pred_seg_tree_cdf[i]);
#if CONFIG_NEW_TX_PARTITION
#if !CONFIG_TX_PARTITION_CTX
  av1_copy(fc->intra_4way_txfm_partition_cdf,
           default_intra_4way_txfm_partition_cdf);
  av1_copy(fc->intra_2way_txfm_partition_cdf,
           default_intra_2way_txfm_partition_cdf);
#endif  // !CONFIG_TX_PARTITION_CTX
#else
  av1_copy(fc->tx_size_cdf, default_tx_size_cdf);
#endif  // CONFIG_NEW_TX_PARTITION
  av1_copy(fc->delta_q_cdf, default_delta_q_cdf);
  av1_copy(fc->delta_lf_cdf, default_delta_lf_cdf);
  av1_copy(fc->delta_lf_multi_cdf, default_delta_lf_multi_cdf);
  av1_copy(fc->cfl_sign_cdf, default_cfl_sign_cdf);
  av1_copy(fc->cfl_alpha_cdf, default_cfl_alpha_cdf);
  av1_copy(fc->intrabc_cdf, default_intrabc_cdf);
#if CONFIG_IBC_BV_IMPROVEMENT
  av1_copy(fc->intrabc_mode_cdf, default_intrabc_mode_cdf);
  av1_copy(fc->intrabc_drl_idx_cdf, default_intrabc_drl_idx_cdf);
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_SUBPEL_PRECISION
  av1_copy(fc->intrabc_bv_precision_cdf, default_intrabc_bv_precision_cdf);
#endif  // CONFIG_IBC_SUBPEL_PRECISION
#if CONFIG_MORPH_PRED
  av1_copy(fc->morph_pred_cdf, default_morph_pred_cdf);
#endif  // CONFIG_MORPH_PRED
  av1_copy(fc->stx_cdf, default_stx_cdf);
#if CONFIG_IST_SET_FLAG
#if CONFIG_INTRA_TX_IST_PARSE
  av1_copy(fc->most_probable_stx_set_cdf, default_most_probable_stx_set_cdf);
#if CONFIG_F105_IST_MEM_REDUCE
  av1_copy(fc->most_probable_stx_set_cdf_ADST_ADST,
           default_most_probable_stx_set_cdf_ADST_ADST);
#endif  // CONFIG_F105_IST_MEM_REDUCE
#else
  av1_copy(fc->stx_set_cdf, default_stx_set_cdf);
#endif  // CONFIG_INTRA_TX_IST_PARSE
#endif  // CONFIG_IST_SET_FLAG
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

#if CONFIG_TILE_CDFS_AVG_TO_FRAME
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
    CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                           nmv_tr->joint_shell_class_cdf_1[prec],
                           num_mv_class_1);
  }
#else
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    int num_mv_class = get_default_num_shell_class(prec);
    CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf[prec],
                           nmv_tr->joint_shell_class_cdf[prec], num_mv_class);
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_low_class_cdf,
                         nmv_tr->shell_offset_low_class_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_class2_cdf,
                         nmv_tr->shell_offset_class2_cdf, 2);
#if !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_other_class_cdf,
                         nmv_tr->shell_offset_other_class_cdf, 2);
#endif  // !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
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

    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].sign_cdf,
                           nmv_tr->comps[i].sign_cdf, 2);

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
#if CONFIG_CONTEXT_DERIVATION
  CUMULATIVE_AVERAGE_CDF(ctx_left->v_txb_skip_cdf, ctx_tr->v_txb_skip_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_extra_cdf, ctx_tr->eob_extra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dc_sign_cdf, ctx_tr->dc_sign_cdf, 2);
#if CONFIG_CONTEXT_DERIVATION
  CUMULATIVE_AVERAGE_CDF(ctx_left->v_dc_sign_cdf, ctx_tr->v_dc_sign_cdf, 2);
#if !CONFIG_CTX_V_AC_SIGN
  CUMULATIVE_AVERAGE_CDF(ctx_left->v_ac_sign_cdf, ctx_tr->v_ac_sign_cdf, 2);
#endif  // !CONFIG_CTX_V_AC_SIGN
#endif  // CONFIG_CONTEXT_DERIVATION
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf16, ctx_tr->eob_flag_cdf16,
                         EOB_MAX_SYMS - 6);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf32, ctx_tr->eob_flag_cdf32,
                         EOB_MAX_SYMS - 5);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf64, ctx_tr->eob_flag_cdf64,
                         EOB_MAX_SYMS - 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf128, ctx_tr->eob_flag_cdf128,
                         EOB_MAX_SYMS - 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
                         EOB_MAX_SYMS - 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
                         EOB_MAX_SYMS - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
                         EOB_MAX_SYMS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_eob_cdf,
                         ctx_tr->coeff_base_eob_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_bob_cdf,
                         ctx_tr->coeff_base_bob_cdf, 3);
#if CONFIG_DIP
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_dip_cdf, ctx_tr->intra_dip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_dip_mode_n6_cdf,
                         ctx_tr->intra_dip_mode_n6_cdf, 6);
#endif  // CONFIG_DIP
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
#if CONFIG_CHROMA_CODING
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
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_lf_uv_cdf,
                         ctx_tr->coeff_br_lf_uv_cdf, BR_CDF_SIZE);
#endif  // CONFIG_CHROMA_CODING

  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_warp_mode_cdf,
                         ctx_tr->inter_warp_mode_cdf, 2);
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  CUMULATIVE_AVERAGE_CDF(ctx_left->is_warpmv_or_warp_newmv_cdf,
                         ctx_tr->is_warpmv_or_warp_newmv_cdf, 2);
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

#if CONFIG_REFINEMV
  CUMULATIVE_AVERAGE_CDF(ctx_left->refinemv_flag_cdf, ctx_tr->refinemv_flag_cdf,
                         REFINEMV_NUM_MODES);
#endif  // CONFIG_REFINEMV

  CUMULATIVE_AVERAGE_CDF(ctx_left->drl_cdf, ctx_tr->drl_cdf, 2);
#if CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_drl_cdf, ctx_tr->skip_drl_cdf, 2);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
  CUMULATIVE_AVERAGE_CDF(ctx_left->use_optflow_cdf, ctx_tr->use_optflow_cdf, 2);

#if CONFIG_INTER_COMPOUND_BY_JOINT
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_is_joint_cdf,
                         ctx_tr->inter_compound_mode_is_joint_cdf,
                         NUM_OPTIONS_IS_JOINT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_non_joint_type_cdf,
                         ctx_tr->inter_compound_mode_non_joint_type_cdf,
                         NUM_OPTIONS_NON_JOINT_TYPE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_joint_type_cdf,
                         ctx_tr->inter_compound_mode_joint_type_cdf,
                         NUM_OPTIONS_JOINT_TYPE);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_cdf,
                         ctx_tr->inter_compound_mode_cdf,
                         INTER_COMPOUND_REF_TYPES);
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT

#if CONFIG_OPT_INTER_MODE_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_same_refs_cdf,
                         ctx_tr->inter_compound_mode_same_refs_cdf,
                         INTER_COMPOUND_SAME_REFS_TYPES);
#endif  // CONFIG_OPT_INTER_MODE_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->cwp_idx_cdf, ctx_tr->cwp_idx_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->jmvd_scale_mode_cdf,
                         ctx_tr->jmvd_scale_mode_cdf,
                         JOINT_NEWMV_SCALE_FACTOR_CNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->jmvd_amvd_scale_mode_cdf,
                         ctx_tr->jmvd_amvd_scale_mode_cdf,
                         JOINT_AMVD_SCALE_FACTOR_CNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->compound_type_cdf, ctx_tr->compound_type_cdf,
                         MASKED_COMPOUND_TYPES);
#if CONFIG_WEDGE_MOD_EXT
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
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_idx_cdf, ctx_tr->wedge_idx_cdf, 16);
#endif
#if CONFIG_WARP_INTER_INTRA
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_interintra_cdf,
                         ctx_tr->warp_interintra_cdf, 2);
#endif  // CONFIG_WARP_INTER_INTRA

  CUMULATIVE_AVERAGE_CDF(ctx_left->interintra_cdf, ctx_tr->interintra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_interintra_cdf,
                         ctx_tr->wedge_interintra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->interintra_mode_cdf,
                         ctx_tr->interintra_mode_cdf, INTERINTRA_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_causal_cdf, ctx_tr->warp_causal_cdf, 2);
#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_delta_cdf, ctx_tr->warp_delta_cdf, 2);
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_delta_param_cdf,
                         ctx_tr->warp_delta_param_cdf,
                         WARP_DELTA_NUMSYMBOLS_LOW);
#if CONFIG_WARP_PRECISION
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_precision_idx_cdf,
                         ctx_tr->warp_precision_idx_cdf,
                         NUM_WARP_PRECISION_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_delta_param_high_cdf,
                         ctx_tr->warp_delta_param_high_cdf,
                         WARP_DELTA_NUMSYMBOLS_HIGH);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_param_sign_cdf,
                         ctx_tr->warp_param_sign_cdf, 2);
#endif  // CONFIG_WARP_PRECISION

  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_causal_warpmv_cdf,
                         ctx_tr->warp_causal_warpmv_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_ref_idx_cdf, ctx_tr->warp_ref_idx_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warpmv_with_mvd_flag_cdf,
                         ctx_tr->warpmv_with_mvd_flag_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_extend_cdf, ctx_tr->warp_extend_cdf, 2);

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  CUMULATIVE_AVERAGE_CDF(ctx_left->bawp_cdf, ctx_tr->bawp_cdf, 2);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->bawp_cdf, ctx_tr->bawp_cdf, 2);
#endif  // CONFIG_BAWP_CHROMA
#if CONFIG_EXPLICIT_BAWP
  CUMULATIVE_AVERAGE_CDF(ctx_left->explicit_bawp_cdf, ctx_tr->explicit_bawp_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->explicit_bawp_scale_cdf,
                         ctx_tr->explicit_bawp_scale_cdf,
                         EXPLICIT_BAWP_SCALE_CNT);
#endif  // CONFIG_EXPLICIT_BAWP
#endif
  CUMULATIVE_AVERAGE_CDF(ctx_left->tip_cdf, ctx_tr->tip_cdf, 2);
#if CONFIG_OPTIMIZE_CTX_TIP_WARP
  CUMULATIVE_AVERAGE_CDF(ctx_left->tip_pred_mode_cdf, ctx_tr->tip_pred_mode_cdf,
                         TIP_PRED_MODES);
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
#if CONFIG_PALETTE_IMPROVEMENTS
#if CONFIG_PALETTE_LINE_COPY
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_y,
                         ctx_tr->identity_row_cdf_y, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_uv,
                         ctx_tr->identity_row_cdf_uv, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_direction_cdf,
                         ctx_tr->palette_direction_cdf, 2);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_y,
                         ctx_tr->identity_row_cdf_y, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_uv,
                         ctx_tr->identity_row_cdf_uv, 2);
#endif  // CONFIG_PALETTE_LINE_COPY
#endif  // CONFIG_PALETTE_IMPROVEMENTS
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
    CUMULATIVE_AVG_CDF_STRIDE(ctx_left->palette_uv_color_index_cdf[j],
                              ctx_tr->palette_uv_color_index_cdf[j], nsymbs,
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
#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_do_partition_cdf,
                         ctx_tr->txfm_do_partition_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_4way_partition_type_cdf,
                         ctx_tr->txfm_4way_partition_type_cdf,
                         TX_PARTITION_TYPE_NUM);
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_2or3_way_partition_type_cdf,
                         ctx_tr->txfm_2or3_way_partition_type_cdf, 2);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_4way_txfm_partition_cdf,
                         ctx_tr->inter_4way_txfm_partition_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_2way_txfm_partition_cdf,
                         ctx_tr->inter_2way_txfm_partition_cdf, 2);
#endif  // CONFIG_TX_PARTITION_CTX
#else   // CONFIG_NEW_TX_PARTITION
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_partition_cdf,
                         ctx_tr->txfm_partition_cdf, 2);
#endif  // CONFIG_NEW_TX_PARTITION
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_group_idx_cdf,
                         ctx_tr->comp_group_idx_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_mode_cdfs, ctx_tr->skip_mode_cdfs, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_txfm_cdfs, ctx_tr->skip_txfm_cdfs, 2);
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  cumulative_avg_nmv(&ctx_left->nmvc, &ctx_tr->nmvc, total_tiles_log2);
  cumulative_avg_nmv(&ctx_left->ndvc, &ctx_tr->ndvc, total_tiles_log2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_cdf, ctx_tr->intrabc_cdf, 2);
#if CONFIG_IBC_BV_IMPROVEMENT
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_mode_cdf, ctx_tr->intrabc_mode_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_drl_idx_cdf,
                         ctx_tr->intrabc_drl_idx_cdf, 2);
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_SUBPEL_PRECISION
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_bv_precision_cdf,
                         ctx_tr->intrabc_bv_precision_cdf,
                         NUM_ALLOWED_BV_PRECISIONS);
#endif  // CONFIG_IBC_SUBPEL_PRECISION
#if CONFIG_MORPH_PRED
  CUMULATIVE_AVERAGE_CDF(ctx_left->morph_pred_cdf, ctx_tr->morph_pred_cdf, 2);
#endif  // CONFIG_MORPH_PRED
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf,
                         MAX_SEGMENTS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.pred_cdf, ctx_tr->seg.pred_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
                         ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->filter_intra_cdfs, ctx_tr->filter_intra_cdfs,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->filter_intra_mode_cdf,
                         ctx_tr->filter_intra_mode_cdf, FILTER_INTRA_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->switchable_flex_restore_cdf,
                         ctx_tr->switchable_flex_restore_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wiener_restore_cdf,
                         ctx_tr->wiener_restore_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->ccso_cdf, ctx_tr->ccso_cdf, 2);
#if CONFIG_CDEF_ENHANCEMENTS
  CUMULATIVE_AVERAGE_CDF(ctx_left->cdef_strength_index0_cdf,
                         ctx_tr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    CUMULATIVE_AVG_CDF_STRIDE(ctx_left->cdef_cdf[j], ctx_tr->cdef_cdf[j], j + 2,
                              CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
#endif  // CONFIG_CDEF_ENHANCEMENTS
  CUMULATIVE_AVERAGE_CDF(ctx_left->sgrproj_restore_cdf,
                         ctx_tr->sgrproj_restore_cdf, 2);
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
  CUMULATIVE_AVERAGE_CDF(ctx_left->merged_param_cdf, ctx_tr->merged_param_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->fsc_mode_cdf, ctx_tr->fsc_mode_cdf,
                         FSC_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->mrl_index_cdf, ctx_tr->mrl_index_cdf,
                         MRL_LINE_NUMBER);
#if CONFIG_MRLS_IMPROVE
  CUMULATIVE_AVERAGE_CDF(ctx_left->multi_line_mrl_cdf,
                         ctx_tr->multi_line_mrl_cdf, 2);
#endif

#if CONFIG_LOSSLESS_DPCM
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_cdf, ctx_tr->dpcm_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_vert_horz_cdf,
                         ctx_tr->dpcm_vert_horz_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_uv_cdf, ctx_tr->dpcm_uv_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_uv_vert_horz_cdf,
                         ctx_tr->dpcm_uv_vert_horz_cdf, 2);
#endif  // CONFIG_LOSSLESS_DPCM

#if CONFIG_ENABLE_MHCCP
  CUMULATIVE_AVERAGE_CDF(ctx_left->filter_dir_cdf, ctx_tr->filter_dir_cdf,
                         MHCCP_MODE_NUM);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf,
                         CFL_TYPE_COUNT - 1);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf,
                         CFL_TYPE_COUNT);
#endif  // CONFIG_ENABLE_MHCCP
#if CONFIG_AIMC
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_set_cdf, ctx_tr->y_mode_set_cdf,
                         INTRA_MODE_SETS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_idx_cdf_0, ctx_tr->y_mode_idx_cdf_0,
                         FIRST_MODE_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_idx_cdf_1, ctx_tr->y_mode_idx_cdf_1,
                         SECOND_MODE_COUNT);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_cdf, ctx_tr->y_mode_cdf, INTRA_MODES);
#endif  // CONFIG_AIMC
#if CONFIG_AIMC
  CUMULATIVE_AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf,
                         UV_INTRA_MODES - 1);
#else
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->uv_mode_cdf[0], ctx_tr->uv_mode_cdf[0],
                            UV_INTRA_MODES - 1, CDF_SIZE(UV_INTRA_MODES));
  CUMULATIVE_AVERAGE_CDF(ctx_left->uv_mode_cdf[1], ctx_tr->uv_mode_cdf[1],
                         UV_INTRA_MODES);
#endif  // CONFIG_AIMC

#if CONFIG_EXTENDED_SDP
  CUMULATIVE_AVERAGE_CDF(ctx_left->region_type_cdf, ctx_tr->region_type_cdf,
                         REGION_TYPES);
#endif  // CONFIG_EXTENDED_SDP

#if CONFIG_EXT_RECUR_PARTITIONS
  CUMULATIVE_AVERAGE_CDF(ctx_left->do_split_cdf, ctx_tr->do_split_cdf, 2);
#if CONFIG_EXT_RECUR_PARTITIONS
  CUMULATIVE_AVERAGE_CDF(ctx_left->do_square_split_cdf,
                         ctx_tr->do_square_split_cdf, 2);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
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
#else
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      if (i < 4) {
        CUMULATIVE_AVG_CDF_STRIDE(ctx_left->partition_cdf[plane_index][i],
                                  ctx_tr->partition_cdf[plane_index][i], 4,
                                  CDF_SIZE(10));
      } else if (i < 16) {
        CUMULATIVE_AVERAGE_CDF(ctx_left->partition_cdf[plane_index][i],
                               ctx_tr->partition_cdf[plane_index][i], 10);
      } else {
        CUMULATIVE_AVG_CDF_STRIDE(ctx_left->partition_cdf[plane_index][i],
                                  ctx_tr->partition_cdf[plane_index][i], 8,
                                  CDF_SIZE(10));
      }
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  CUMULATIVE_AVERAGE_CDF(ctx_left->switchable_interp_cdf,
                         ctx_tr->switchable_interp_cdf, SWITCHABLE_FILTERS);
#if !CONFIG_AIMC
  CUMULATIVE_AVERAGE_CDF(ctx_left->kf_y_cdf, ctx_tr->kf_y_cdf, INTRA_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->angle_delta_cdf, ctx_tr->angle_delta_cdf,
                         2 * MAX_ANGLE_DELTA + 1);
#endif  // !CONFIG_AIMC

#if CONFIG_NEW_TX_PARTITION
#if !CONFIG_TX_PARTITION_CTX
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_4way_txfm_partition_cdf,
                         ctx_tr->intra_4way_txfm_partition_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_2way_txfm_partition_cdf,
                         ctx_tr->intra_2way_txfm_partition_cdf, 2);
#endif  // !CONFIG_TX_PARTITION_CTX
#else
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->tx_size_cdf[0], ctx_tr->tx_size_cdf[0],
                            MAX_TX_DEPTH, CDF_SIZE(MAX_TX_DEPTH + 1));
  CUMULATIVE_AVERAGE_CDF(ctx_left->tx_size_cdf[1], ctx_tr->tx_size_cdf[1],
                         MAX_TX_DEPTH + 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tx_size_cdf[2], ctx_tr->tx_size_cdf[2],
                         MAX_TX_DEPTH + 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tx_size_cdf[3], ctx_tr->tx_size_cdf[3],
                         MAX_TX_DEPTH + 1);
#endif  // CONFIG_NEW_TX_PARTITION
  CUMULATIVE_AVERAGE_CDF(ctx_left->delta_q_cdf, ctx_tr->delta_q_cdf,
                         DELTA_Q_PROBS + 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->delta_lf_cdf, ctx_tr->delta_lf_cdf,
                         DELTA_LF_PROBS + 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->delta_lf_multi_cdf,
                         ctx_tr->delta_lf_multi_cdf, DELTA_LF_PROBS + 1);
#if CONFIG_TX_TYPE_FLEX_IMPROVE
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_ext_tx_short_side_cdf,
                         ctx_tr->inter_ext_tx_short_side_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_ext_tx_short_side_cdf,
                         ctx_tr->intra_ext_tx_short_side_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tx_ext_32_cdf, ctx_tr->tx_ext_32_cdf, 2);
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
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
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_sign_cdf, ctx_tr->cfl_sign_cdf,
                         CFL_JOINT_SIGNS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_alpha_cdf, ctx_tr->cfl_alpha_cdf,
                         CFL_ALPHABET_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->stx_cdf, ctx_tr->stx_cdf, STX_TYPES);
#if CONFIG_IST_SET_FLAG
#if CONFIG_INTRA_TX_IST_PARSE
  CUMULATIVE_AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf,
                         ctx_tr->most_probable_stx_set_cdf, IST_DIR_SIZE);
#if CONFIG_F105_IST_MEM_REDUCE
  CUMULATIVE_AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf_ADST_ADST,
                         ctx_tr->most_probable_stx_set_cdf_ADST_ADST,
                         IST_REDUCE_SET_SIZE_ADST_ADST);
#endif  // CONFIG_F105_IST_MEM_REDUCE
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->stx_set_cdf, ctx_tr->stx_set_cdf,
                         IST_DIR_SIZE);
#endif  // CONFIG_INTRA_TX_IST_PARSE
#endif  // CONFIG_IST_SET_FLAG

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
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_ph_cdf, ctx_tr->coeff_br_ph_cdf, 4);
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
    SHIFT_CDF(nmv_ptr->joint_shell_class_cdf_1[prec], num_mv_class_1);
  }
#else
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    int num_mv_class = get_default_num_shell_class(prec);
    SHIFT_CDF(nmv_ptr->joint_shell_class_cdf[prec], num_mv_class);
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  SHIFT_CDF(nmv_ptr->shell_offset_low_class_cdf, 2);
  SHIFT_CDF(nmv_ptr->shell_offset_class2_cdf, 2);
#if !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
  SHIFT_CDF(nmv_ptr->shell_offset_other_class_cdf, 2);
#endif  // !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
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

    SHIFT_CDF(nmv_ptr->comps[i].sign_cdf, 2);

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
#if CONFIG_CONTEXT_DERIVATION
  SHIFT_CDF(ctx_ptr->v_txb_skip_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION
  SHIFT_CDF(ctx_ptr->eob_extra_cdf, 2);
  SHIFT_CDF(ctx_ptr->dc_sign_cdf, 2);
#if CONFIG_CONTEXT_DERIVATION
  SHIFT_CDF(ctx_ptr->v_dc_sign_cdf, 2);
#if !CONFIG_CTX_V_AC_SIGN
  SHIFT_CDF(ctx_ptr->v_ac_sign_cdf, 2);
#endif  // !CONFIG_CTX_V_AC_SIGN
#endif  // CONFIG_CONTEXT_DERIVATION
  SHIFT_CDF(ctx_ptr->eob_flag_cdf16, EOB_MAX_SYMS - 6);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf32, EOB_MAX_SYMS - 5);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf64, EOB_MAX_SYMS - 4);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf128, EOB_MAX_SYMS - 3);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf256, EOB_MAX_SYMS - 2);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf512, EOB_MAX_SYMS - 1);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf1024, EOB_MAX_SYMS);
  SHIFT_CDF(ctx_ptr->coeff_base_eob_cdf, 3);
  SHIFT_CDF(ctx_ptr->coeff_base_bob_cdf, 3);
#if CONFIG_DIP
  SHIFT_CDF(ctx_ptr->intra_dip_cdf, 2);
  SHIFT_CDF(ctx_ptr->intra_dip_mode_n6_cdf, 6);
#endif  // CONFIG_DIP
  SHIFT_CDF(ctx_ptr->coeff_base_lf_cdf, LF_BASE_SYMBOLS);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_eob_cdf, LF_BASE_SYMBOLS - 1);
  SHIFT_CDF(ctx_ptr->coeff_br_lf_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_base_cdf, 4);
  SHIFT_CDF(ctx_ptr->idtx_sign_cdf, 2);
  SHIFT_CDF(ctx_ptr->coeff_base_cdf_idtx, 4);
  SHIFT_CDF(ctx_ptr->coeff_br_cdf_idtx, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_br_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->inter_single_mode_cdf, INTER_SINGLE_MODES);
#if CONFIG_CHROMA_CODING
  SHIFT_CDF(ctx_ptr->coeff_base_uv_cdf, 4);
  SHIFT_CDF(ctx_ptr->coeff_br_uv_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_base_eob_uv_cdf, 3);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_uv_cdf, LF_BASE_SYMBOLS);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);
  SHIFT_CDF(ctx_ptr->coeff_br_lf_uv_cdf, BR_CDF_SIZE);
#endif  // CONFIG_CHROMA_CODING

  SHIFT_CDF(ctx_ptr->inter_warp_mode_cdf, 2);
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  SHIFT_CDF(ctx_ptr->is_warpmv_or_warp_newmv_cdf, 2);
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

#if CONFIG_REFINEMV
  SHIFT_CDF(ctx_ptr->refinemv_flag_cdf, REFINEMV_NUM_MODES);
#endif  // CONFIG_REFINEMV

  SHIFT_CDF(ctx_ptr->drl_cdf, 2);
#if CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
  SHIFT_CDF(ctx_ptr->skip_drl_cdf, 2);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
  SHIFT_CDF(ctx_ptr->use_optflow_cdf, 2);

#if CONFIG_INTER_COMPOUND_BY_JOINT
  SHIFT_CDF(ctx_ptr->inter_compound_mode_is_joint_cdf, NUM_OPTIONS_IS_JOINT);
  SHIFT_CDF(ctx_ptr->inter_compound_mode_non_joint_type_cdf,
            NUM_OPTIONS_NON_JOINT_TYPE);
  SHIFT_CDF(ctx_ptr->inter_compound_mode_joint_type_cdf,
            NUM_OPTIONS_JOINT_TYPE);
#else
  SHIFT_CDF(ctx_ptr->inter_compound_mode_cdf, INTER_COMPOUND_REF_TYPES);
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT

#if CONFIG_OPT_INTER_MODE_CTX
  SHIFT_CDF(ctx_ptr->inter_compound_mode_same_refs_cdf,
            INTER_COMPOUND_SAME_REFS_TYPES);
#endif  // CONFIG_OPT_INTER_MODE_CTX
  SHIFT_CDF(ctx_ptr->cwp_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->jmvd_scale_mode_cdf, JOINT_NEWMV_SCALE_FACTOR_CNT);
  SHIFT_CDF(ctx_ptr->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
  SHIFT_CDF(ctx_ptr->compound_type_cdf, MASKED_COMPOUND_TYPES);
#if CONFIG_WEDGE_MOD_EXT
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
#else
  SHIFT_CDF(ctx_ptr->wedge_idx_cdf, 16);
#endif
#if CONFIG_WARP_INTER_INTRA
  SHIFT_CDF(ctx_ptr->warp_interintra_cdf, 2);
#endif  // CONFIG_WARP_INTER_INTRA
  SHIFT_CDF(ctx_ptr->interintra_cdf, 2);
  SHIFT_CDF(ctx_ptr->wedge_interintra_cdf, 2);
  SHIFT_CDF(ctx_ptr->interintra_mode_cdf, INTERINTRA_MODES);
  SHIFT_CDF(ctx_ptr->warp_causal_cdf, 2);
#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  SHIFT_CDF(ctx_ptr->warp_delta_cdf, 2);
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  SHIFT_CDF(ctx_ptr->warp_delta_param_cdf, WARP_DELTA_NUMSYMBOLS_LOW);

#if CONFIG_WARP_PRECISION
  SHIFT_CDF(ctx_ptr->warp_precision_idx_cdf, NUM_WARP_PRECISION_MODES);
  SHIFT_CDF(ctx_ptr->warp_delta_param_high_cdf, WARP_DELTA_NUMSYMBOLS_HIGH);
  SHIFT_CDF(ctx_ptr->warp_param_sign_cdf, 2);
#endif  // CONFIG_WARP_PRECISION

  SHIFT_CDF(ctx_ptr->warp_causal_warpmv_cdf, 2);
  SHIFT_CDF(ctx_ptr->warp_ref_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->warpmv_with_mvd_flag_cdf, 2);
  SHIFT_CDF(ctx_ptr->warp_extend_cdf, 2);

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  SHIFT_CDF(ctx_ptr->bawp_cdf, 2);
#else
  SHIFT_CDF(ctx_ptr->bawp_cdf, 2);
#endif  // CONFIG_BAWP_CHROMA
#if CONFIG_EXPLICIT_BAWP
  SHIFT_CDF(ctx_ptr->explicit_bawp_cdf, 2);
  SHIFT_CDF(ctx_ptr->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT);
#endif  // CONFIG_EXPLICIT_BAWP
#endif
  SHIFT_CDF(ctx_ptr->tip_cdf, 2);
#if CONFIG_OPTIMIZE_CTX_TIP_WARP
  SHIFT_CDF(ctx_ptr->tip_pred_mode_cdf, TIP_PRED_MODES);
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
#if CONFIG_PALETTE_IMPROVEMENTS
#if CONFIG_PALETTE_LINE_COPY
  SHIFT_CDF(ctx_ptr->identity_row_cdf_y, 3);
  SHIFT_CDF(ctx_ptr->identity_row_cdf_uv, 3);
  SHIFT_CDF(ctx_ptr->palette_direction_cdf, 2);
#else
  SHIFT_CDF(ctx_ptr->identity_row_cdf_y, 2);
  SHIFT_CDF(ctx_ptr->identity_row_cdf_uv, 2);
#endif  // CONFIG_PALETTE_LINE_COPY
#endif  // CONFIG_PALETTE_IMPROVEMENTS
  SHIFT_CDF(ctx_ptr->cfl_cdf, 2);
  SHIFT_CDF(ctx_ptr->palette_y_size_cdf, PALETTE_SIZES);
  SHIFT_CDF(ctx_ptr->palette_uv_size_cdf, PALETTE_SIZES);

  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    SHIFT_CDF_STRIDE(ctx_ptr->palette_y_color_index_cdf[j], nsymbs,
                     CDF_SIZE(PALETTE_COLORS));
    SHIFT_CDF_STRIDE(ctx_ptr->palette_uv_color_index_cdf[j], nsymbs,
                     CDF_SIZE(PALETTE_COLORS));
  }
  SHIFT_CDF(ctx_ptr->palette_y_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->palette_uv_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_inter_cdf, 2);
  SHIFT_CDF(ctx_ptr->single_ref_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_ref0_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_ref1_cdf, 2);
#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
  SHIFT_CDF(ctx_ptr->txfm_do_partition_cdf, 2);
  SHIFT_CDF(ctx_ptr->txfm_4way_partition_type_cdf, TX_PARTITION_TYPE_NUM);
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  SHIFT_CDF(ctx_ptr->txfm_2or3_way_partition_type_cdf, 2);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#else
  SHIFT_CDF(ctx_ptr->inter_4way_txfm_partition_cdf, 4);
  SHIFT_CDF(ctx_ptr->inter_2way_txfm_partition_cdf, 2);
#endif  // CONFIG_TX_PARTITION_CTX
#else   // CONFIG_NEW_TX_PARTITION
  SHIFT_CDF(ctx_ptr->txfm_partition_cdf, 2);
#endif  // CONFIG_NEW_TX_PARTITION
  SHIFT_CDF(ctx_ptr->comp_group_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->skip_mode_cdfs, 2);
  SHIFT_CDF(ctx_ptr->skip_txfm_cdfs, 2);
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  SHIFT_CDF(ctx_ptr->intra_inter_cdf, 2);
#else
  SHIFT_CDF(ctx_ptr->intra_inter_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  shift_nmv(&ctx_ptr->nmvc, total_tiles_log2);
  shift_nmv(&ctx_ptr->ndvc, total_tiles_log2);
  SHIFT_CDF(ctx_ptr->intrabc_cdf, 2);
#if CONFIG_IBC_BV_IMPROVEMENT
  SHIFT_CDF(ctx_ptr->intrabc_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->intrabc_drl_idx_cdf, 2);
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_SUBPEL_PRECISION
  SHIFT_CDF(ctx_ptr->intrabc_bv_precision_cdf, NUM_ALLOWED_BV_PRECISIONS);
#endif  // CONFIG_IBC_SUBPEL_PRECISION
#if CONFIG_MORPH_PRED
  SHIFT_CDF(ctx_ptr->morph_pred_cdf, 2);
#endif  // CONFIG_MORPH_PRED
  SHIFT_CDF(ctx_ptr->seg.tree_cdf, MAX_SEGMENTS);
  SHIFT_CDF(ctx_ptr->seg.pred_cdf, 2);
  SHIFT_CDF(ctx_ptr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  SHIFT_CDF(ctx_ptr->filter_intra_cdfs, 2);
  SHIFT_CDF(ctx_ptr->filter_intra_mode_cdf, FILTER_INTRA_MODES);
  SHIFT_CDF(ctx_ptr->switchable_flex_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->wiener_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->ccso_cdf, 2);
#if CONFIG_CDEF_ENHANCEMENTS
  SHIFT_CDF(ctx_ptr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    SHIFT_CDF_STRIDE(ctx_ptr->cdef_cdf[j], j + 2, CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
#endif  // CONFIG_CDEF_ENHANCEMENTS
  SHIFT_CDF(ctx_ptr->sgrproj_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_length_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_uv_sym_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_4part_cdf, 4);
  SHIFT_CDF(ctx_ptr->pc_wiener_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->merged_param_cdf, 2);
  SHIFT_CDF(ctx_ptr->fsc_mode_cdf, FSC_MODES);
  SHIFT_CDF(ctx_ptr->mrl_index_cdf, MRL_LINE_NUMBER);
#if CONFIG_MRLS_IMPROVE
  SHIFT_CDF(ctx_ptr->multi_line_mrl_cdf, 2);
#endif
#if CONFIG_LOSSLESS_DPCM
  SHIFT_CDF(ctx_ptr->dpcm_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_vert_horz_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_uv_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_uv_vert_horz_cdf, 2);
#endif  // CONFIG_LOSSLESS_DPCM

#if CONFIG_ENABLE_MHCCP
  SHIFT_CDF(ctx_ptr->filter_dir_cdf, MHCCP_MODE_NUM);
  SHIFT_CDF(ctx_ptr->cfl_index_cdf, CFL_TYPE_COUNT - 1);
#else
  SHIFT_CDF(ctx_ptr->cfl_index_cdf, CFL_TYPE_COUNT);
#endif  // CONFIG_ENABLE_MHCCP
#if CONFIG_AIMC
  SHIFT_CDF(ctx_ptr->y_mode_set_cdf, INTRA_MODE_SETS);
  SHIFT_CDF(ctx_ptr->y_mode_idx_cdf_0, FIRST_MODE_COUNT);
  SHIFT_CDF(ctx_ptr->y_mode_idx_cdf_1, SECOND_MODE_COUNT);
#else
  SHIFT_CDF(ctx_ptr->y_mode_cdf, INTRA_MODES);
#endif  // CONFIG_AIMC
#if CONFIG_AIMC
  SHIFT_CDF(ctx_ptr->uv_mode_cdf, UV_INTRA_MODES - 1);
#else
  SHIFT_CDF_STRIDE(ctx_ptr->uv_mode_cdf[0], UV_INTRA_MODES - 1,
                   CDF_SIZE(UV_INTRA_MODES));
  SHIFT_CDF(ctx_ptr->uv_mode_cdf[1], UV_INTRA_MODES);
#endif  // CONFIG_AIMC

#if CONFIG_EXTENDED_SDP
  SHIFT_CDF(ctx_ptr->region_type_cdf, REGION_TYPES);
#endif  // CONFIG_EXTENDED_SDP

#if CONFIG_EXT_RECUR_PARTITIONS
  SHIFT_CDF(ctx_ptr->do_split_cdf, 2);
#if CONFIG_EXT_RECUR_PARTITIONS
  SHIFT_CDF(ctx_ptr->do_square_split_cdf, 2);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  SHIFT_CDF(ctx_ptr->rect_type_cdf, 2);
  SHIFT_CDF(ctx_ptr->do_ext_partition_cdf, 2);
  SHIFT_CDF(ctx_ptr->do_uneven_4way_partition_cdf, 2);
#if !CONFIG_NEW_PART_CTX
  SHIFT_CDF(ctx_ptr->uneven_4way_partition_type_cdf, NUM_UNEVEN_4WAY_PARTS);
#endif  // !CONFIG_NEW_PART_CTX
#else
  SHIFT_CDF(ctx_ptr->partition_cdf, EXT_PARTITION_TYPES);

  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      if (i < 4) {
        SHIFT_CDF_STRIDE(ctx_ptr->partition_cdf[plane_index][i], 4,
                         CDF_SIZE(10));
      } else if (i < 16) {
        SHIFT_CDF(ctx_ptr->partition_cdf[plane_index][i], 10);
      } else {
        SHIFT_CDF_STRIDE(ctx_ptr->partition_cdf[plane_index][i], 8,
                         CDF_SIZE(10));
      }
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  SHIFT_CDF(ctx_ptr->switchable_interp_cdf, SWITCHABLE_FILTERS);
#if !CONFIG_AIMC
  SHIFT_CDF(ctx_ptr->kf_y_cdf, INTRA_MODES);
  SHIFT_CDF(ctx_ptr->angle_delta_cdf, 2 * MAX_ANGLE_DELTA + 1);
#endif  // !CONFIG_AIMC

#if CONFIG_NEW_TX_PARTITION
#if !CONFIG_TX_PARTITION_CTX
  SHIFT_CDF(ctx_ptr->intra_4way_txfm_partition_cdf, 4);
  SHIFT_CDF(ctx_ptr->intra_2way_txfm_partition_cdf, 2);
#endif  // !CONFIG_TX_PARTITION_CTX
#else
  SHIFT_CDF_STRIDE(ctx_ptr->tx_size_cdf[0], MAX_TX_DEPTH,
                   CDF_SIZE(MAX_TX_DEPTH + 1));
  SHIFT_CDF(ctx_ptr->tx_size_cdf[1], MAX_TX_DEPTH + 1);
  SHIFT_CDF(ctx_ptr->tx_size_cdf[2], MAX_TX_DEPTH + 1);
  SHIFT_CDF(ctx_ptr->tx_size_cdf[3], MAX_TX_DEPTH + 1);
#endif  // CONFIG_NEW_TX_PARTITION
  SHIFT_CDF(ctx_ptr->delta_q_cdf, DELTA_Q_PROBS + 1);
  SHIFT_CDF(ctx_ptr->delta_lf_cdf, DELTA_LF_PROBS + 1);
  SHIFT_CDF(ctx_ptr->delta_lf_multi_cdf, DELTA_LF_PROBS + 1);
#if CONFIG_TX_TYPE_FLEX_IMPROVE
  SHIFT_CDF(ctx_ptr->inter_ext_tx_short_side_cdf, 4);
  SHIFT_CDF(ctx_ptr->intra_ext_tx_short_side_cdf, 4);
  SHIFT_CDF(ctx_ptr->tx_ext_32_cdf, 2);
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
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
  SHIFT_CDF(ctx_ptr->cfl_sign_cdf, CFL_JOINT_SIGNS);
  SHIFT_CDF(ctx_ptr->cfl_alpha_cdf, CFL_ALPHABET_SIZE);
  SHIFT_CDF(ctx_ptr->stx_cdf, STX_TYPES);
#if CONFIG_IST_SET_FLAG
#if CONFIG_INTRA_TX_IST_PARSE
  SHIFT_CDF(ctx_ptr->most_probable_stx_set_cdf, IST_DIR_SIZE);
#if CONFIG_F105_IST_MEM_REDUCE
  SHIFT_CDF(ctx_ptr->most_probable_stx_set_cdf_ADST_ADST,
            IST_REDUCE_SET_SIZE_ADST_ADST);
#endif  // CONFIG_F105_IST_MEM_REDUCE
#else
  SHIFT_CDF(ctx_ptr->stx_set_cdf, IST_DIR_SIZE);
#endif  // CONFIG_INTRA_TX_IST_PARSE
#endif  // CONFIG_IST_SET_FLAG

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
  SHIFT_CDF(ctx_ptr->coeff_br_ph_cdf, 4);
  SHIFT_CDF(ctx_ptr->cctx_type_cdf, CCTX_TYPES);
}
#endif  // CONFIG_TILE_CDFS_AVG_TO_FRAME

#if CONFIG_ENHANCED_FRAME_CONTEXT_INIT
static void avg_cdf_symbol(aom_cdf_prob *cdf_ptr_left, aom_cdf_prob *cdf_ptr_tr,
                           int num_cdfs, int cdf_stride, int nsymbs,
                           int wt_left, int wt_tr) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      cdf_ptr_left[i * cdf_stride + j] =
          (aom_cdf_prob)(((int)cdf_ptr_left[i * cdf_stride + j] * wt_left +
                          (int)cdf_ptr_tr[i * cdf_stride + j] * wt_tr +
                          ((wt_left + wt_tr) / 2)) /
                         (wt_left + wt_tr));
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
                   wt_left, wt_tr);                                        \
  } while (0)

static void avg_nmv(nmv_context *nmv_left, nmv_context *nmv_tr, int wt_left,
                    int wt_tr) {
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
    AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                nmv_tr->joint_shell_class_cdf_1[prec], num_mv_class_1);
  }
#else
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    int num_mv_class = get_default_num_shell_class(prec);
    AVERAGE_CDF(nmv_left->joint_shell_class_cdf[prec],
                nmv_tr->joint_shell_class_cdf[prec], num_mv_class);
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  AVERAGE_CDF(nmv_left->shell_offset_low_class_cdf,
              nmv_tr->shell_offset_low_class_cdf, 2);
  AVERAGE_CDF(nmv_left->shell_offset_class2_cdf,
              nmv_tr->shell_offset_class2_cdf, 2);
#if !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
  for (int i = 0; i < NUM_CTX_CLASS_OFFSETS; i++) {
    AVERAGE_CDF(nmv_left->shell_offset_other_class_cdf[i],
                nmv_tr->shell_offset_other_class_cdf[i], 2);
  }
#endif  // !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
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

    AVERAGE_CDF(nmv_left->comps[i].sign_cdf, nmv_tr->comps[i].sign_cdf, 2);

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
  AVERAGE_CDF(ctx_left->txb_skip_cdf, ctx_tr->txb_skip_cdf, 2);
#if CONFIG_CONTEXT_DERIVATION
  AVERAGE_CDF(ctx_left->v_txb_skip_cdf, ctx_tr->v_txb_skip_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION
  AVERAGE_CDF(ctx_left->eob_extra_cdf, ctx_tr->eob_extra_cdf, 2);
  AVERAGE_CDF(ctx_left->dc_sign_cdf, ctx_tr->dc_sign_cdf, 2);
#if CONFIG_CONTEXT_DERIVATION
  AVERAGE_CDF(ctx_left->v_dc_sign_cdf, ctx_tr->v_dc_sign_cdf, 2);
#if !CONFIG_CTX_V_AC_SIGN
  AVERAGE_CDF(ctx_left->v_ac_sign_cdf, ctx_tr->v_ac_sign_cdf, 2);
#endif  // !CONFIG_CTX_V_AC_SIGN
#endif  // CONFIG_CONTEXT_DERIVATION
  AVERAGE_CDF(ctx_left->eob_flag_cdf16, ctx_tr->eob_flag_cdf16,
              EOB_MAX_SYMS - 6);
  AVERAGE_CDF(ctx_left->eob_flag_cdf32, ctx_tr->eob_flag_cdf32,
              EOB_MAX_SYMS - 5);
  AVERAGE_CDF(ctx_left->eob_flag_cdf64, ctx_tr->eob_flag_cdf64,
              EOB_MAX_SYMS - 4);
  AVERAGE_CDF(ctx_left->eob_flag_cdf128, ctx_tr->eob_flag_cdf128,
              EOB_MAX_SYMS - 3);
  AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
              EOB_MAX_SYMS - 2);
  AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
              EOB_MAX_SYMS - 1);
  AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
              EOB_MAX_SYMS);
  AVERAGE_CDF(ctx_left->coeff_base_eob_cdf, ctx_tr->coeff_base_eob_cdf, 3);
  AVERAGE_CDF(ctx_left->coeff_base_bob_cdf, ctx_tr->coeff_base_bob_cdf, 3);
#if CONFIG_DIP
  AVERAGE_CDF(ctx_left->intra_dip_cdf, ctx_tr->intra_dip_cdf, 2);
  AVERAGE_CDF(ctx_left->intra_dip_mode_n6_cdf, ctx_tr->intra_dip_mode_n6_cdf,
              6);
#endif  // CONFIG_DIP
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
#if CONFIG_CHROMA_CODING
  AVERAGE_CDF(ctx_left->coeff_base_uv_cdf, ctx_tr->coeff_base_uv_cdf, 4);
  AVERAGE_CDF(ctx_left->coeff_br_uv_cdf, ctx_tr->coeff_br_uv_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_base_eob_uv_cdf, ctx_tr->coeff_base_eob_uv_cdf,
              3);
  AVERAGE_CDF(ctx_left->coeff_base_lf_uv_cdf, ctx_tr->coeff_base_lf_uv_cdf,
              LF_BASE_SYMBOLS);
  AVERAGE_CDF(ctx_left->coeff_base_lf_eob_uv_cdf,
              ctx_tr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);
  AVERAGE_CDF(ctx_left->coeff_br_lf_uv_cdf, ctx_tr->coeff_br_lf_uv_cdf,
              BR_CDF_SIZE);
#endif  // CONFIG_CHROMA_CODING

  AVERAGE_CDF(ctx_left->inter_warp_mode_cdf, ctx_tr->inter_warp_mode_cdf, 2);
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  AVERAGE_CDF(ctx_left->is_warpmv_or_warp_newmv_cdf,
              ctx_tr->is_warpmv_or_warp_newmv_cdf, 2);
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

#if CONFIG_REFINEMV
  AVERAGE_CDF(ctx_left->refinemv_flag_cdf, ctx_tr->refinemv_flag_cdf,
              REFINEMV_NUM_MODES);
#endif  // CONFIG_REFINEMV

  AVERAGE_CDF(ctx_left->drl_cdf, ctx_tr->drl_cdf, 2);
#if CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
  AVERAGE_CDF(ctx_left->skip_drl_cdf, ctx_tr->skip_drl_cdf, 2);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP

  AVERAGE_CDF(ctx_left->use_optflow_cdf, ctx_tr->use_optflow_cdf, 2);

#if CONFIG_INTER_COMPOUND_BY_JOINT
  AVERAGE_CDF(ctx_left->inter_compound_mode_is_joint_cdf,
              ctx_tr->inter_compound_mode_is_joint_cdf, NUM_OPTIONS_IS_JOINT);
  AVERAGE_CDF(ctx_left->inter_compound_mode_non_joint_type_cdf,
              ctx_tr->inter_compound_mode_non_joint_type_cdf,
              NUM_OPTIONS_NON_JOINT_TYPE);
  AVERAGE_CDF(ctx_left->inter_compound_mode_joint_type_cdf,
              ctx_tr->inter_compound_mode_joint_type_cdf,
              NUM_OPTIONS_JOINT_TYPE);
#else
  AVERAGE_CDF(ctx_left->inter_compound_mode_cdf,
              ctx_tr->inter_compound_mode_cdf, INTER_COMPOUND_REF_TYPES);
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT

#if CONFIG_OPT_INTER_MODE_CTX
  AVERAGE_CDF(ctx_left->inter_compound_mode_same_refs_cdf,
              ctx_tr->inter_compound_mode_same_refs_cdf,
              INTER_COMPOUND_SAME_REFS_TYPES);
#endif  // CONFIG_OPT_INTER_MODE_CTX
  AVERAGE_CDF(ctx_left->cwp_idx_cdf, ctx_tr->cwp_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->jmvd_scale_mode_cdf, ctx_tr->jmvd_scale_mode_cdf,
              JOINT_NEWMV_SCALE_FACTOR_CNT);
  AVERAGE_CDF(ctx_left->jmvd_amvd_scale_mode_cdf,
              ctx_tr->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
  AVERAGE_CDF(ctx_left->compound_type_cdf, ctx_tr->compound_type_cdf,
              MASKED_COMPOUND_TYPES);
#if CONFIG_WEDGE_MOD_EXT
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
#else
  AVERAGE_CDF(ctx_left->wedge_idx_cdf, ctx_tr->wedge_idx_cdf, 16);
#endif
#if CONFIG_WARP_INTER_INTRA
  AVERAGE_CDF(ctx_left->warp_interintra_cdf, ctx_tr->warp_interintra_cdf, 2);
#endif  // CONFIG_WARP_INTER_INTRA
  AVERAGE_CDF(ctx_left->interintra_cdf, ctx_tr->interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->wedge_interintra_cdf, ctx_tr->wedge_interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->interintra_mode_cdf, ctx_tr->interintra_mode_cdf,
              INTERINTRA_MODES);
  AVERAGE_CDF(ctx_left->warp_causal_cdf, ctx_tr->warp_causal_cdf, 2);
#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  AVERAGE_CDF(ctx_left->warp_delta_cdf, ctx_tr->warp_delta_cdf, 2);
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  AVERAGE_CDF(ctx_left->warp_delta_param_cdf, ctx_tr->warp_delta_param_cdf,
              WARP_DELTA_NUMSYMBOLS_LOW);
#if CONFIG_WARP_PRECISION
  AVERAGE_CDF(ctx_left->warp_delta_param_high_cdf,
              ctx_tr->warp_delta_param_high_cdf, WARP_DELTA_NUMSYMBOLS_HIGH);
  AVERAGE_CDF(ctx_left->warp_param_sign_cdf, ctx_tr->warp_param_sign_cdf, 2);
#endif  // CONFIG_WARP_PRECISION

  AVERAGE_CDF(ctx_left->warp_causal_warpmv_cdf, ctx_tr->warp_causal_warpmv_cdf,
              2);
  AVERAGE_CDF(ctx_left->warp_ref_idx_cdf, ctx_tr->warp_ref_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->warpmv_with_mvd_flag_cdf,
              ctx_tr->warpmv_with_mvd_flag_cdf, 2);

#if CONFIG_WARP_PRECISION
  AVERAGE_CDF(ctx_left->warp_precision_idx_cdf, ctx_tr->warp_precision_idx_cdf,
              NUM_WARP_PRECISION_MODES);
#endif  // CONFIG_WARP_PRECISION

  AVERAGE_CDF(ctx_left->warp_extend_cdf, ctx_tr->warp_extend_cdf, 2);

#if CONFIG_BAWP
  AVERAGE_CDF(ctx_left->bawp_cdf, ctx_tr->bawp_cdf, 2);
#if CONFIG_EXPLICIT_BAWP
  AVERAGE_CDF(ctx_left->explicit_bawp_cdf, ctx_tr->explicit_bawp_cdf, 2);
  AVERAGE_CDF(ctx_left->explicit_bawp_scale_cdf,
              ctx_tr->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT);
#endif  // CONFIG_EXPLICIT_BAWP
#endif  // CONFIG_BAWP
  AVERAGE_CDF(ctx_left->tip_cdf, ctx_tr->tip_cdf, 2);
#if CONFIG_OPTIMIZE_CTX_TIP_WARP
  AVERAGE_CDF(ctx_left->tip_pred_mode_cdf, ctx_tr->tip_pred_mode_cdf,
              TIP_PRED_MODES);
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
  AVERAGE_CDF(ctx_left->palette_y_size_cdf, ctx_tr->palette_y_size_cdf,
              PALETTE_SIZES);
  AVERAGE_CDF(ctx_left->palette_uv_size_cdf, ctx_tr->palette_uv_size_cdf,
              PALETTE_SIZES);
#if CONFIG_PALETTE_IMPROVEMENTS
#if CONFIG_PALETTE_LINE_COPY
  AVERAGE_CDF(ctx_left->identity_row_cdf_y, ctx_tr->identity_row_cdf_y, 3);
  AVERAGE_CDF(ctx_left->identity_row_cdf_uv, ctx_tr->identity_row_cdf_uv, 3);
  AVERAGE_CDF(ctx_left->palette_direction_cdf, ctx_tr->palette_direction_cdf,
              2);
#else
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_y,
                         ctx_tr->identity_row_cdf_y, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_uv,
                         ctx_tr->identity_row_cdf_uv, 2);
#endif  // CONFIG_PALETTE_LINE_COPY
#endif  // CONFIG_PALETTE_IMPROVEMENTS
  AVERAGE_CDF(ctx_left->cfl_cdf, ctx_tr->cfl_cdf, 2);
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    AVG_CDF_STRIDE(ctx_left->palette_y_color_index_cdf[j],
                   ctx_tr->palette_y_color_index_cdf[j], nsymbs,
                   CDF_SIZE(PALETTE_COLORS));
    AVG_CDF_STRIDE(ctx_left->palette_uv_color_index_cdf[j],
                   ctx_tr->palette_uv_color_index_cdf[j], nsymbs,
                   CDF_SIZE(PALETTE_COLORS));
  }
  AVERAGE_CDF(ctx_left->palette_y_mode_cdf, ctx_tr->palette_y_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->palette_uv_mode_cdf, ctx_tr->palette_uv_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_inter_cdf, ctx_tr->comp_inter_cdf, 2);
  AVERAGE_CDF(ctx_left->single_ref_cdf, ctx_tr->single_ref_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref0_cdf, ctx_tr->comp_ref0_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref1_cdf, ctx_tr->comp_ref1_cdf, 2);
#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
  AVERAGE_CDF(ctx_left->txfm_do_partition_cdf, ctx_tr->txfm_do_partition_cdf,
              2);
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  AVERAGE_CDF(ctx_left->txfm_2or3_way_partition_type_cdf,
              ctx_tr->txfm_2or3_way_partition_type_cdf, 2);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  AVERAGE_CDF(ctx_left->txfm_4way_partition_type_cdf,
              ctx_tr->txfm_4way_partition_type_cdf, TX_PARTITION_TYPE_NUM);
#else
  AVERAGE_CDF(ctx_left->inter_4way_txfm_partition_cdf,
              ctx_tr->inter_4way_txfm_partition_cdf, 4);
  AVERAGE_CDF(ctx_left->inter_2way_txfm_partition_cdf,
              ctx_tr->inter_2way_txfm_partition_cdf, 2);
#endif  // CONFIG_TX_PARTITION_CTX
#else   // CONFIG_NEW_TX_PARTITION
  AVERAGE_CDF(ctx_left->txfm_partition_cdf, ctx_tr->txfm_partition_cdf, 2);
#endif  // CONFIG_NEW_TX_PARTITION
  AVERAGE_CDF(ctx_left->comp_group_idx_cdf, ctx_tr->comp_group_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->skip_mode_cdfs, ctx_tr->skip_mode_cdfs, 2);
  AVERAGE_CDF(ctx_left->skip_txfm_cdfs, ctx_tr->skip_txfm_cdfs, 2);
  AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
  avg_nmv(&ctx_left->nmvc, &ctx_tr->nmvc, wt_left, wt_tr);
  avg_nmv(&ctx_left->ndvc, &ctx_tr->ndvc, wt_left, wt_tr);
  AVERAGE_CDF(ctx_left->intrabc_cdf, ctx_tr->intrabc_cdf, 2);
#if CONFIG_IBC_BV_IMPROVEMENT
  AVERAGE_CDF(ctx_left->intrabc_mode_cdf, ctx_tr->intrabc_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->intrabc_drl_idx_cdf, ctx_tr->intrabc_drl_idx_cdf, 2);
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_SUBPEL_PRECISION
  AVERAGE_CDF(ctx_left->intrabc_bv_precision_cdf,
              ctx_tr->intrabc_bv_precision_cdf, NUM_ALLOWED_BV_PRECISIONS);
#endif  // CONFIG_IBC_SUBPEL_PRECISION
#if CONFIG_MORPH_PRED
  AVERAGE_CDF(ctx_left->morph_pred_cdf, ctx_tr->morph_pred_cdf, 2);
#endif  // CONFIG_MORPH_PRED
  AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf, MAX_SEGMENTS);
  AVERAGE_CDF(ctx_left->seg.pred_cdf, ctx_tr->seg.pred_cdf, 2);
  AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
              ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  AVERAGE_CDF(ctx_left->filter_intra_cdfs, ctx_tr->filter_intra_cdfs, 2);
  AVERAGE_CDF(ctx_left->filter_intra_mode_cdf, ctx_tr->filter_intra_mode_cdf,
              FILTER_INTRA_MODES);
  AVERAGE_CDF(ctx_left->switchable_flex_restore_cdf,
              ctx_tr->switchable_flex_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->wiener_restore_cdf, ctx_tr->wiener_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->ccso_cdf, ctx_tr->ccso_cdf, 2);
#if CONFIG_CDEF_ENHANCEMENTS
  AVERAGE_CDF(ctx_left->cdef_strength_index0_cdf,
              ctx_tr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    AVG_CDF_STRIDE(ctx_left->cdef_cdf[j], ctx_tr->cdef_cdf[j], j + 2,
                   CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
#endif  // CONFIG_CDEF_ENHANCEMENTS
  AVERAGE_CDF(ctx_left->sgrproj_restore_cdf, ctx_tr->sgrproj_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_restore_cdf, ctx_tr->wienerns_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_length_cdf, ctx_tr->wienerns_length_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_uv_sym_cdf, ctx_tr->wienerns_uv_sym_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_4part_cdf, ctx_tr->wienerns_4part_cdf, 4);
  AVERAGE_CDF(ctx_left->pc_wiener_restore_cdf, ctx_tr->pc_wiener_restore_cdf,
              2);
  AVERAGE_CDF(ctx_left->merged_param_cdf, ctx_tr->merged_param_cdf, 2);
  AVERAGE_CDF(ctx_left->fsc_mode_cdf, ctx_tr->fsc_mode_cdf, FSC_MODES);
  AVERAGE_CDF(ctx_left->mrl_index_cdf, ctx_tr->mrl_index_cdf, MRL_LINE_NUMBER);
#if CONFIG_MRLS_IMPROVE
  AVERAGE_CDF(ctx_left->multi_line_mrl_cdf, ctx_tr->multi_line_mrl_cdf, 2);
#endif

#if CONFIG_LOSSLESS_DPCM
  AVERAGE_CDF(ctx_left->dpcm_cdf, ctx_tr->dpcm_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_vert_horz_cdf, ctx_tr->dpcm_vert_horz_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_uv_cdf, ctx_tr->dpcm_uv_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_uv_vert_horz_cdf, ctx_tr->dpcm_uv_vert_horz_cdf,
              2);
#endif  // CONFIG_LOSSLESS_DPCM

#if CONFIG_ENABLE_MHCCP
  AVERAGE_CDF(ctx_left->filter_dir_cdf, ctx_tr->filter_dir_cdf, MHCCP_MODE_NUM);
  AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf,
              CFL_TYPE_COUNT - 1);
#else
  AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf, CFL_TYPE_COUNT);
#endif  // CONFIG_ENABLE_MHCCP
#if CONFIG_AIMC
  AVERAGE_CDF(ctx_left->y_mode_set_cdf, ctx_tr->y_mode_set_cdf,
              INTRA_MODE_SETS);
  AVERAGE_CDF(ctx_left->y_mode_idx_cdf_0, ctx_tr->y_mode_idx_cdf_0,
              FIRST_MODE_COUNT);
  AVERAGE_CDF(ctx_left->y_mode_idx_cdf_1, ctx_tr->y_mode_idx_cdf_1,
              SECOND_MODE_COUNT);
#else
  AVERAGE_CDF(ctx_left->y_mode_cdf, ctx_tr->y_mode_cdf, INTRA_MODES);
#endif  // CONFIG_AIMC
#if CONFIG_AIMC
  AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf, UV_INTRA_MODES - 1);
#else
  AVG_CDF_STRIDE(ctx_left->uv_mode_cdf[0], ctx_tr->uv_mode_cdf[0],
                 UV_INTRA_MODES - 1, CDF_SIZE(UV_INTRA_MODES));
  AVERAGE_CDF(ctx_left->uv_mode_cdf[1], ctx_tr->uv_mode_cdf[1], UV_INTRA_MODES);
#endif  // CONFIG_AIMC

#if CONFIG_EXTENDED_SDP
  AVERAGE_CDF(ctx_left->region_type_cdf, ctx_tr->region_type_cdf, REGION_TYPES);
#endif  // CONFIG_EXTENDED_SDP

#if CONFIG_EXT_RECUR_PARTITIONS
  AVERAGE_CDF(ctx_left->do_split_cdf, ctx_tr->do_split_cdf, 2);
#if CONFIG_EXT_RECUR_PARTITIONS
  AVERAGE_CDF(ctx_left->do_square_split_cdf, ctx_tr->do_square_split_cdf, 2);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  AVERAGE_CDF(ctx_left->rect_type_cdf, ctx_tr->rect_type_cdf, 2);
  AVERAGE_CDF(ctx_left->do_ext_partition_cdf, ctx_tr->do_ext_partition_cdf, 2);
  AVERAGE_CDF(ctx_left->do_uneven_4way_partition_cdf,
              ctx_tr->do_uneven_4way_partition_cdf, 2);
#if !CONFIG_NEW_PART_CTX
  AVERAGE_CDF(ctx_left->uneven_4way_partition_type_cdf,
              ctx_tr->uneven_4way_partition_type_cdf, NUM_UNEVEN_4WAY_PARTS);
#endif  // !CONFIG_NEW_PART_CTX
#else
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      if (i < 4) {
        AVG_CDF_STRIDE(ctx_left->partition_cdf[plane_index][i],
                       ctx_tr->partition_cdf[plane_index][i], 4, CDF_SIZE(10));
      } else if (i < 16) {
        AVERAGE_CDF(ctx_left->partition_cdf[plane_index][i],
                    ctx_tr->partition_cdf[plane_index][i], 10);
      } else {
        AVG_CDF_STRIDE(ctx_left->partition_cdf[plane_index][i],
                       ctx_tr->partition_cdf[plane_index][i], 8, CDF_SIZE(10));
      }
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  AVERAGE_CDF(ctx_left->switchable_interp_cdf, ctx_tr->switchable_interp_cdf,
              SWITCHABLE_FILTERS);
#if !CONFIG_AIMC
  AVERAGE_CDF(ctx_left->kf_y_cdf, ctx_tr->kf_y_cdf, INTRA_MODES);
  AVERAGE_CDF(ctx_left->angle_delta_cdf, ctx_tr->angle_delta_cdf,
              2 * MAX_ANGLE_DELTA + 1);
#endif  // !CONFIG_AIMC

#if CONFIG_NEW_TX_PARTITION
#if !CONFIG_TX_PARTITION_CTX
  AVERAGE_CDF(ctx_left->intra_4way_txfm_partition_cdf,
              ctx_tr->intra_4way_txfm_partition_cdf, 4);
  AVERAGE_CDF(ctx_left->intra_2way_txfm_partition_cdf,
              ctx_tr->intra_2way_txfm_partition_cdf, 2);
#endif  // !CONFIG_TX_PARTITION_CTX
#else
  AVG_CDF_STRIDE(ctx_left->tx_size_cdf[0], ctx_tr->tx_size_cdf[0], MAX_TX_DEPTH,
                 CDF_SIZE(MAX_TX_DEPTH + 1));
  AVERAGE_CDF(ctx_left->tx_size_cdf[1], ctx_tr->tx_size_cdf[1],
              MAX_TX_DEPTH + 1);
  AVERAGE_CDF(ctx_left->tx_size_cdf[2], ctx_tr->tx_size_cdf[2],
              MAX_TX_DEPTH + 1);
  AVERAGE_CDF(ctx_left->tx_size_cdf[3], ctx_tr->tx_size_cdf[3],
              MAX_TX_DEPTH + 1);
#endif  // CONFIG_NEW_TX_PARTITION
  AVERAGE_CDF(ctx_left->delta_q_cdf, ctx_tr->delta_q_cdf, DELTA_Q_PROBS + 1);
  AVERAGE_CDF(ctx_left->delta_lf_cdf, ctx_tr->delta_lf_cdf, DELTA_LF_PROBS + 1);
  AVERAGE_CDF(ctx_left->delta_lf_multi_cdf, ctx_tr->delta_lf_multi_cdf,
              DELTA_LF_PROBS + 1);
#if CONFIG_TX_TYPE_FLEX_IMPROVE
  AVERAGE_CDF(ctx_left->inter_ext_tx_short_side_cdf,
              ctx_tr->inter_ext_tx_short_side_cdf, 4);
  AVERAGE_CDF(ctx_left->intra_ext_tx_short_side_cdf,
              ctx_tr->intra_ext_tx_short_side_cdf, 4);
  AVERAGE_CDF(ctx_left->tx_ext_32_cdf, ctx_tr->tx_ext_32_cdf, 2);
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
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
  AVERAGE_CDF(ctx_left->cfl_sign_cdf, ctx_tr->cfl_sign_cdf, CFL_JOINT_SIGNS);
  AVERAGE_CDF(ctx_left->cfl_alpha_cdf, ctx_tr->cfl_alpha_cdf,
              CFL_ALPHABET_SIZE);
  AVG_CDF_STRIDE(ctx_left->stx_cdf, ctx_tr->stx_cdf, STX_TYPES,
                 CDF_SIZE(STX_TYPES));
#if CONFIG_IST_SET_FLAG
#if CONFIG_INTRA_TX_IST_PARSE
  AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf,
              ctx_tr->most_probable_stx_set_cdf, IST_DIR_SIZE);
#if CONFIG_F105_IST_MEM_REDUCE
  AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf_ADST_ADST,
              ctx_tr->most_probable_stx_set_cdf_ADST_ADST,
              IST_REDUCE_SET_SIZE_ADST_ADST);
#endif  // CONFIG_F105_IST_MEM_REDUCE
#else
  AVERAGE_CDF(ctx_left->stx_set_cdf, ctx_tr->stx_set_cdf, IST_DIR_SIZE);
#endif  // CONFIG_INTRA_TX_IST_PARSE
#endif  // CONFIG_IST_SET_FLAG

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
  AVERAGE_CDF(ctx_left->coeff_br_ph_cdf, ctx_tr->coeff_br_ph_cdf, 4);
  AVERAGE_CDF(ctx_left->cctx_type_cdf, ctx_tr->cctx_type_cdf, CCTX_TYPES);
}
#endif  // CONFIG_ENHANCED_FRAME_CONTEXT_INIT

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
