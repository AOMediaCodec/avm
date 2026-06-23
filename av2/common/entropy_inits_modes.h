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

#include "av2/common/entropymode.h"

#ifdef __cplusplus
extern "C" {
#endif

static const avm_cdf_prob
    default_inter_single_mode_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(3)] = {
      { AVM_CDF3(10043, 11100), AVM_PARA3(0, -1, -1) },
      { AVM_CDF3(21561, 21758), AVM_PARA3(0, 0, -1) },
      { AVM_CDF3(25411, 25714), AVM_PARA3(0, 0, 0) },
      { AVM_CDF3(14117, 14341), AVM_PARA3(0, 0, 0) },
      { AVM_CDF3(18288, 18577), AVM_PARA3(0, 0, 0) },
    };

static const avm_cdf_prob
    default_inter_warp_mode_cdf[WARPMV_MODE_CONTEXT][CDF_SIZE(2)] = {
      { AVM_CDF2(25999), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(14478), AVM_PARA2(0, -1, -2) },
      { AVM_CDF2(10868), AVM_PARA2(0, -1, -1) },
      { AVM_CDF2(5256), AVM_PARA2(-1, -1, -1) },
      { AVM_CDF2(2722), AVM_PARA2(-1, -1, -1) },
    };

static const avm_cdf_prob default_is_warpmv_or_warp_newmv_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(15095), AVM_PARA2(0, 0, -1)
};

static const avm_cdf_prob
    default_refinemv_flag_cdf[NUM_REFINEMV_CTX][CDF_SIZE(2)] = {
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(20204), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(17614), AVM_PARA2(-1, 1, 1) },
      { AVM_CDF2(24443), AVM_PARA2(-1, -1, -2) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
    };

static const avm_cdf_prob
    default_drl_cdf[DRL_CDF_TYPES][DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      {
          { AVM_CDF2(15721), AVM_PARA2(1, 1, 0) },
          { AVM_CDF2(21115), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(19567), AVM_PARA2(0, 0, -1) },
          { AVM_CDF2(17602), AVM_PARA2(1, 1, 1) },
          { AVM_CDF2(13319), AVM_PARA2(1, 1, 1) },
      },
      {
          { AVM_CDF2(18692), AVM_PARA2(1, 1, 1) },
          { AVM_CDF2(19343), AVM_PARA2(1, 1, 0) },
          { AVM_CDF2(18207), AVM_PARA2(1, 1, 0) },
          { AVM_CDF2(17908), AVM_PARA2(1, 1, 1) },
          { AVM_CDF2(18304), AVM_PARA2(1, 1, 1) },
      },
      {
          { AVM_CDF2(22157), AVM_PARA2(1, 1, 0) },
          { AVM_CDF2(23233), AVM_PARA2(1, 1, 0) },
          { AVM_CDF2(22782), AVM_PARA2(1, 1, 0) },
          { AVM_CDF2(22353), AVM_PARA2(1, 1, 1) },
          { AVM_CDF2(22457), AVM_PARA2(1, 1, 1) },
      },
    };

static const avm_cdf_prob default_tip_drl_cdf[DRL_CDF_TYPES][CDF_SIZE(2)] = {
  { AVM_CDF2(30662), AVM_PARA2(0, 0, 0) },
  { AVM_CDF2(23823), AVM_PARA2(0, -1, -1) },
  { AVM_CDF2(21676), AVM_PARA2(0, -1, -1) },
};

static const avm_cdf_prob default_skip_drl_cdf[DRL_CDF_TYPES][CDF_SIZE(2)] = {
  { AVM_CDF2(21634), AVM_PARA2(0, 0, 0) },
  { AVM_CDF2(17376), AVM_PARA2(0, 0, 0) },
  { AVM_CDF2(18432), AVM_PARA2(1, 0, 0) },
};

static const avm_cdf_prob
    default_use_optflow_cdf[OPFL_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(11582), AVM_PARA2(-1, -1, -1) },
      { AVM_CDF2(24076), AVM_PARA2(-1, 0, -1) },
    };

static const avm_cdf_prob
    default_inter_compound_mode_is_joint_cdf[NUM_CTX_IS_JOINT][CDF_SIZE(2)] = {
      { AVM_CDF2(24720), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(32764), AVM_PARA2(0, 0, 0) },
    };

static const avm_cdf_prob default_inter_compound_mode_non_joint_type_cdf
    [NUM_CTX_NON_JOINT_TYPE][CDF_SIZE(5)] = {
      { AVM_CDF5(12177, 20001, 23193, 24448), AVM_PARA5(-1, 0, -1) },
      { AVM_CDF5(21192, 25117, 27806, 27948), AVM_PARA5(-1, -1, -1) },
      { AVM_CDF5(26779, 28724, 30192, 30249), AVM_PARA5(-1, 0, 0) },
      { AVM_CDF5(12506, 17871, 21295, 21389), AVM_PARA5(-1, -1, -1) },
      { AVM_CDF5(16948, 20335, 22582, 22617), AVM_PARA5(-1, -1, -1) },
    };

static const avm_cdf_prob
    default_inter_compound_mode_same_refs_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(
        4)] = {
      { AVM_CDF4(6386, 23344, 23348), AVM_PARA4(-1, -1, -1) },
      { AVM_CDF4(10945, 24709, 24714), AVM_PARA4(-1, -1, -1) },
      { AVM_CDF4(11517, 25230, 25258), AVM_PARA4(-1, -1, -1) },
      { AVM_CDF4(7563, 22176, 22180), AVM_PARA4(-1, -1, -1) },
      { AVM_CDF4(6629, 20955, 20966), AVM_PARA4(0, -1, -1) },
    };

static const avm_cdf_prob
    default_cwp_idx_cdf[MAX_CWP_CONTEXTS][MAX_CWP_NUM - 1][CDF_SIZE(2)] = {
      {
          { AVM_CDF2(21704), AVM_PARA2(-2, -1, -1) },
          { AVM_CDF2(15990), AVM_PARA2(-1, -1, -1) },
          { AVM_CDF2(12544), AVM_PARA2(-2, -1, -2) },
          { AVM_CDF2(25638), AVM_PARA2(-2, -2, -2) },
      },
      {
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      },
    };

static const avm_cdf_prob default_jmvd_scale_mode_cdf[CDF_SIZE(5)] = {
  AVM_CDF5(23180, 24894, 26548, 29872), AVM_PARA5(0, 0, -1)
};

static const avm_cdf_prob default_jmvd_amvd_scale_mode_cdf[CDF_SIZE(3)] = {
  AVM_CDF3(23178, 26812), AVM_PARA3(0, 0, 0)
};

static const avm_cdf_prob default_compound_type_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(16894), AVM_PARA2(-1, -1, -1)
};

static const avm_cdf_prob
    default_amvd_mode_cdf[NUM_AMVD_MODES][AMVD_MODE_CONTEXTS][CDF_SIZE(2)] = {
      {
          { AVM_CDF2(5980), AVM_PARA2(0, 1, 0) },
          { AVM_CDF2(6091), AVM_PARA2(0, 1, 0) },
          { AVM_CDF2(6237), AVM_PARA2(1, 1, 1) },
      },
      {
          { AVM_CDF2(861), AVM_PARA2(-1, -1, 0) },
          { AVM_CDF2(847), AVM_PARA2(-2, 0, 0) },
          { AVM_CDF2(1198), AVM_PARA2(-2, 1, -1) },
      },
      {
          { AVM_CDF2(456), AVM_PARA2(-2, 0, 1) },
          { AVM_CDF2(431), AVM_PARA2(-2, 1, 1) },
          { AVM_CDF2(849), AVM_PARA2(-2, 1, 1) },
      },
      {
          { AVM_CDF2(409), AVM_PARA2(-2, 1, 1) },
          { AVM_CDF2(385), AVM_PARA2(-2, 1, 1) },
          { AVM_CDF2(581), AVM_PARA2(-2, 1, 1) },
      },
      {
          { AVM_CDF2(16246), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(9696), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(8791), AVM_PARA2(0, 0, 0) },
      },
      {
          { AVM_CDF2(13199), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(10624), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(8586), AVM_PARA2(0, 0, -1) },
      },
      {
          { AVM_CDF2(5112), AVM_PARA2(0, 0, -1) },
          { AVM_CDF2(3920), AVM_PARA2(0, 0, 1) },
          { AVM_CDF2(3668), AVM_PARA2(0, 0, 1) },
      },
      {
          { AVM_CDF2(12017), AVM_PARA2(1, 0, -1) },
          { AVM_CDF2(10177), AVM_PARA2(1, 0, 0) },
          { AVM_CDF2(9184), AVM_PARA2(1, 0, 0) },
      },
      {
          { AVM_CDF2(12111), AVM_PARA2(0, -1, -1) },
          { AVM_CDF2(8056), AVM_PARA2(0, -1, 0) },
          { AVM_CDF2(6641), AVM_PARA2(-1, 0, -2) },
      },
    };

static const avm_cdf_prob default_wedge_quad_cdf[CDF_SIZE(4)] = {
  AVM_CDF4(6511, 18144, 27374), AVM_PARA4(0, 0, -1)
};

static const avm_cdf_prob default_wedge_angle_cdf[WEDGE_QUADS][CDF_SIZE(5)] = {
  { AVM_CDF5(10258, 15276, 19997, 26561), AVM_PARA5(0, -1, -1) },
  { AVM_CDF5(14039, 19183, 26143, 30047), AVM_PARA5(0, -1, -1) },
  { AVM_CDF5(19564, 22099, 25104, 29960), AVM_PARA5(0, 0, -1) },
  { AVM_CDF5(13808, 17950, 25715, 29008), AVM_PARA5(0, -1, -2) },
};

static const avm_cdf_prob default_wedge_dist_cdf[CDF_SIZE(4)] = {
  AVM_CDF4(8203, 16994, 21032), AVM_PARA4(1, 0, 0)
};

static const avm_cdf_prob default_wedge_dist_cdf2[CDF_SIZE(3)] = {
  AVM_CDF3(14463, 19115), AVM_PARA3(1, 0, 0)
};

static const avm_cdf_prob
    default_warp_interintra_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(2)] = {
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(27980), AVM_PARA2(-2, -1, -1) },
      { AVM_CDF2(29163), AVM_PARA2(-2, -1, -1) },
      { AVM_CDF2(30658), AVM_PARA2(-2, -2, -2) },
    };

static const avm_cdf_prob
    default_interintra_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(2)] = {
      { AVM_CDF2(20569), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(17106), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(20948), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(25796), AVM_PARA2(-1, -1, -2) },
    };

static const avm_cdf_prob default_wedge_interintra_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(16758), AVM_PARA2(0, 0, 0)
};

static const avm_cdf_prob
    default_interintra_mode_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(4)] = {
      { AVM_CDF4(1819, 16131, 26802), AVM_PARA4(-1, -1, -2) },
      { AVM_CDF4(1442, 15840, 28441), AVM_PARA4(0, 0, -1) },
      { AVM_CDF4(1995, 15814, 28221), AVM_PARA4(0, -1, -2) },
      { AVM_CDF4(3564, 15440, 28048), AVM_PARA4(-1, -1, -2) },
    };

static const avm_cdf_prob
    default_warp_ref_idx0_cdf[WARP_REF_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(18903), AVM_PARA2(0, 0, 0) },
    };

static const avm_cdf_prob
    default_warp_ref_idx1_cdf[WARP_REF_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(24500), AVM_PARA2(1, 1, 0) },
    };

static const avm_cdf_prob
    default_warp_ref_idx2_cdf[WARP_REF_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(25360), AVM_PARA2(1, 0, 0) },
    };

static const avm_cdf_prob
    default_warp_causal_cdf[WARP_CAUSAL_MODE_CTX][CDF_SIZE(2)] = {
      { AVM_CDF2(14877), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(12801), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(6885), AVM_PARA2(0, -1, -1) },
      { AVM_CDF2(2987), AVM_PARA2(-1, -1, 0) },
    };

static const avm_cdf_prob
    default_warp_precision_idx_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(31107), AVM_PARA2(-2, -1, -1) },
      { AVM_CDF2(27357), AVM_PARA2(-1, -1, -1) },
      { AVM_CDF2(26435), AVM_PARA2(-1, 0, -1) },
      { AVM_CDF2(22585), AVM_PARA2(-1, 0, -1) },
      { AVM_CDF2(20146), AVM_PARA2(-1, 0, -2) },
      { AVM_CDF2(18671), AVM_PARA2(-1, -1, -2) },
      { AVM_CDF2(18210), AVM_PARA2(-1, -1, -2) },
      { AVM_CDF2(17968), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(17451), AVM_PARA2(-1, 1, 0) },
      { AVM_CDF2(16736), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(16040), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(15633), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(13952), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(14893), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(13323), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(11830), AVM_PARA2(-2, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(21783), AVM_PARA2(-1, 0, 1) },
      { AVM_CDF2(20345), AVM_PARA2(0, 1, 0) },
      { AVM_CDF2(19398), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(17823), AVM_PARA2(-1, -2, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(20066), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(18893), AVM_PARA2(0, -1, 0) },
    };

static const avm_cdf_prob
    default_warp_delta_param_cdf[WARP_DELTA_PARAM_TYPES][CDF_SIZE(8)] = {
      { AVM_CDF8(8995, 22970, 25406, 29673, 30295, 31670, 31863),
        AVM_PARA8(0, 0, 0) },
      { AVM_CDF8(13333, 24012, 26545, 30183, 30839, 31958, 32139),
        AVM_PARA8(0, 0, 0) },
    };

static const avm_cdf_prob
    default_warp_delta_param_high_cdf[WARP_DELTA_PARAM_TYPES][CDF_SIZE(8)] = {
      { AVM_CDF8(8959, 14388, 19825, 21810, 25035, 28077, 29469),
        AVM_PARA8(0, 1, 0) },
      { AVM_CDF8(9199, 14146, 19484, 21591, 24614, 28015, 29538),
        AVM_PARA8(0, 0, 0) },
    };

static const avm_cdf_prob default_warp_param_sign_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(14285), AVM_PARA2(1, 1, 1)
};

static const avm_cdf_prob
    default_warp_extend_cdf[WARP_EXTEND_CTX][CDF_SIZE(2)] = {
      { AVM_CDF2(20359), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(20310), AVM_PARA2(1, 0, 0) },
      { AVM_CDF2(21759), AVM_PARA2(1, 0, 0) },
    };

static const avm_cdf_prob default_warpmv_with_mvd_flag_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(18452), AVM_PARA2(0, 0, -1)
};

static const avm_cdf_prob default_bawp_cdf[BAWP_FLAG_CTXS][CDF_SIZE(2)] = {
  { AVM_CDF2(26456), AVM_PARA2(-1, -1, -1) },
  { AVM_CDF2(5121), AVM_PARA2(-1, -1, -1) },
};

static const avm_cdf_prob
    default_explicit_bawp_cdf[BAWP_SCALES_CTX_COUNT][CDF_SIZE(2)] = {
      { AVM_CDF2(26966), AVM_PARA2(-1, -1, -1) },
      { AVM_CDF2(15275), AVM_PARA2(0, -1, -1) },
      { AVM_CDF2(14613), AVM_PARA2(-1, -1, -1) },
    };

static const avm_cdf_prob default_explicit_bawp_scale_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(21998), AVM_PARA2(-1, -1, -2)
};

static const avm_cdf_prob default_tip_cdf[TIP_CONTEXTS][CDF_SIZE(2)] = {
  { AVM_CDF2(30898), AVM_PARA2(1, 1, 1) },
  { AVM_CDF2(19665), AVM_PARA2(0, 0, 0) },
  { AVM_CDF2(9477), AVM_PARA2(0, 1, 0) },
};

static const avm_cdf_prob default_tip_pred_mode_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(22129), AVM_PARA2(-1, -1, -1)
};

static const avm_cdf_prob default_palette_y_size_cdf[CDF_SIZE(7)] = {
  AVM_CDF7(8779, 15095, 20777, 24903, 27923, 30403), AVM_PARA7(-1, -1, -2)
};

static const avm_cdf_prob default_palette_uv_size_cdf[CDF_SIZE(7)] = {
  AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087), AVM_PARA7(0, 0, 0)
};

static const avm_cdf_prob
    default_identity_row_cdf_y[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(3)] = {
      { AVM_CDF3(22515, 25751), AVM_PARA3(-1, 0, 0) },
      { AVM_CDF3(4014, 5233), AVM_PARA3(-1, -1, -1) },
      { AVM_CDF3(3548, 4163), AVM_PARA3(-1, -1, 1) },
      { AVM_CDF3(12999, 32756), AVM_PARA3(-2, -1, -1) },
    };

static const avm_cdf_prob
    default_identity_row_cdf_uv[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(3)] = {
      { AVM_CDF3(10923, 21845), AVM_PARA3(0, 0, 0) },
      { AVM_CDF3(10923, 21845), AVM_PARA3(0, 0, 0) },
      { AVM_CDF3(10923, 21845), AVM_PARA3(0, 0, 0) },
      { AVM_CDF3(10923, 21845), AVM_PARA3(0, 0, 0) },
    };

static const avm_cdf_prob default_palette_y_color_index_cdf
    [PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(8)] = {
      {
          { AVM_CDF2(28140), AVM_PARA2(1, 1, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(8582), AVM_PARA2(0, -1, -1) },
          { AVM_CDF2(27413), AVM_PARA2(-1, -1, -2) },
          { AVM_CDF2(30429), AVM_PARA2(1, 1, 1) },
      },
      {
          { AVM_CDF3(25350, 29026), AVM_PARA3(1, 1, 0) },
          { AVM_CDF3(11363, 25273), AVM_PARA3(0, -1, -2) },
          { AVM_CDF3(6841, 28579), AVM_PARA3(0, 0, -1) },
          { AVM_CDF3(21350, 26012), AVM_PARA3(0, -1, -1) },
          { AVM_CDF3(30573, 31646), AVM_PARA3(1, 1, 1) },
      },
      {
          { AVM_CDF4(23706, 26962, 29060), AVM_PARA4(0, 0, 0) },
          { AVM_CDF4(9976, 22516, 27382), AVM_PARA4(0, 0, -1) },
          { AVM_CDF4(6691, 25460, 29234), AVM_PARA4(0, -1, -1) },
          { AVM_CDF4(18909, 23925, 28403), AVM_PARA4(-1, -1, -1) },
          { AVM_CDF4(30308, 31076, 31818), AVM_PARA4(1, 1, 1) },
      },
      {
          { AVM_CDF5(24116, 26957, 28486, 29941), AVM_PARA5(0, 0, 0) },
          { AVM_CDF5(9568, 20472, 24294, 28942), AVM_PARA5(1, -1, -1) },
          { AVM_CDF5(5706, 25243, 28040, 30406), AVM_PARA5(1, 0, -1) },
          { AVM_CDF5(20105, 22982, 27024, 28911), AVM_PARA5(-1, -1, -1) },
          { AVM_CDF5(30897, 31342, 31766, 32199), AVM_PARA5(1, 1, 1) },
      },
      {
          { AVM_CDF6(20824, 24227, 25926, 27459, 29266), AVM_PARA6(1, 0, 0) },
          { AVM_CDF6(8141, 18989, 21599, 26182, 28576), AVM_PARA6(1, 0, 0) },
          { AVM_CDF6(5252, 24340, 26450, 28438, 30625), AVM_PARA6(1, 0, 0) },
          { AVM_CDF6(19519, 22695, 25587, 26972, 28423), AVM_PARA6(0, -1, -1) },
          { AVM_CDF6(30383, 30890, 31247, 31653, 32150), AVM_PARA6(1, 0, 1) },
      },
      {
          { AVM_CDF7(21628, 24512, 25873, 27054, 28131, 29539),
            AVM_PARA7(1, -1, 0) },
          { AVM_CDF7(8028, 18264, 20613, 25424, 27112, 28906),
            AVM_PARA7(1, 1, 0) },
          { AVM_CDF7(6489, 22242, 24461, 26394, 28350, 30510),
            AVM_PARA7(1, 0, 0) },
          { AVM_CDF7(22048, 24429, 26990, 27944, 28417, 29574),
            AVM_PARA7(1, 0, -1) },
          { AVM_CDF7(30801, 31205, 31472, 31728, 32005, 32305),
            AVM_PARA7(1, 1, 1) },
      },
      {
          { AVM_CDF8(22471, 25083, 25984, 26893, 27654, 28750, 29903),
            AVM_PARA8(1, 1, 1) },
          { AVM_CDF8(7542, 17057, 19151, 23550, 25459, 27066, 28804),
            AVM_PARA8(1, 1, 0) },
          { AVM_CDF8(7582, 20437, 22728, 24622, 26515, 28579, 30632),
            AVM_PARA8(1, 1, 0) },
          { AVM_CDF8(22102, 24144, 26916, 28151, 28846, 29212, 30153),
            AVM_PARA8(0, 0, 0) },
          { AVM_CDF8(30524, 30887, 31156, 31393, 31626, 31911, 32281),
            AVM_PARA8(1, 1, 1) },
      },
    };

static const avm_cdf_prob default_palette_y_mode_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(30045), AVM_PARA2(-2, -2, -2)
};

static const avm_cdf_prob default_palette_uv_mode_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(16384), AVM_PARA2(0, 0, 0)
};

static const avm_cdf_prob
    default_comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(26924), AVM_PARA2(0, 1, 0) },
      { AVM_CDF2(25000), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(17949), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(13581), AVM_PARA2(0, -1, -1) },
      { AVM_CDF2(7034), AVM_PARA2(0, 0, 0) },
    };

static const avm_cdf_prob
    default_single_ref_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
                          [CDF_SIZE(2)] = {
                            {
                                { AVM_CDF2(26469), AVM_PARA2(0, 0, 0) },
                                { AVM_CDF2(28870), AVM_PARA2(-1, -1, 0) },
                                { AVM_CDF2(29662), AVM_PARA2(0, 0, -1) },
                                { AVM_CDF2(29867), AVM_PARA2(0, -1, -1) },
                                { AVM_CDF2(29772), AVM_PARA2(0, -1, -1) },
                                { AVM_CDF2(29776), AVM_PARA2(-1, 0, -1) },
                            },
                            {
                                { AVM_CDF2(13631), AVM_PARA2(0, -1, -1) },
                                { AVM_CDF2(18185), AVM_PARA2(-1, -2, -2) },
                                { AVM_CDF2(19992), AVM_PARA2(-1, -1, -2) },
                                { AVM_CDF2(18462), AVM_PARA2(-2, -2, -2) },
                                { AVM_CDF2(17451), AVM_PARA2(-1, -2, -2) },
                                { AVM_CDF2(11578), AVM_PARA2(-2, -2, -2) },
                            },
                            {
                                { AVM_CDF2(2599), AVM_PARA2(0, 0, 0) },
                                { AVM_CDF2(5203), AVM_PARA2(-1, -1, -1) },
                                { AVM_CDF2(5185), AVM_PARA2(-1, -1, -1) },
                                { AVM_CDF2(3671), AVM_PARA2(-1, -1, -1) },
                                { AVM_CDF2(3954), AVM_PARA2(0, -1, -1) },
                                { AVM_CDF2(1633), AVM_PARA2(0, -1, 0) },
                            },
                          };

static const avm_cdf_prob
    default_comp_ref0_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
                         [CDF_SIZE(2)] = {
                           {
                               { AVM_CDF2(9272), AVM_PARA2(-1, -1, -2) },
                               { AVM_CDF2(17175), AVM_PARA2(-2, -2, -2) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                           },
                           {
                               { AVM_CDF2(1385), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(4439), AVM_PARA2(-1, -1, -1) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                           },
                           {
                               { AVM_CDF2(521), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(1854), AVM_PARA2(-1, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                           },
                         };

static const avm_cdf_prob
    default_comp_ref1_cdf[REF_CONTEXTS][COMPREF_BIT_TYPES]
                         [INTER_REFS_PER_FRAME - 1][CDF_SIZE(2)] = {
                           {
                               {
                                   { AVM_CDF2(30729), AVM_PARA2(1, 0, 0) },
                                   { AVM_CDF2(29403), AVM_PARA2(0, -1, 0) },
                                   { AVM_CDF2(29037), AVM_PARA2(0, -1, -1) },
                                   { AVM_CDF2(29355), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(28573), AVM_PARA2(0, -1, 0) },
                                   { AVM_CDF2(27396), AVM_PARA2(0, -1, -2) },
                               },
                               {
                                   { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                   { AVM_CDF2(19315), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(27821), AVM_PARA2(0, -1, -1) },
                                   { AVM_CDF2(27892), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(28695), AVM_PARA2(-1, -1, 0) },
                                   { AVM_CDF2(29637), AVM_PARA2(-2, 0, -1) },
                               },
                           },
                           {
                               {
                                   { AVM_CDF2(30432), AVM_PARA2(0, 0, 0) },
                                   { AVM_CDF2(20290), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(19855), AVM_PARA2(-1, -2, -2) },
                                   { AVM_CDF2(18567), AVM_PARA2(-2, -2, -2) },
                                   { AVM_CDF2(18331), AVM_PARA2(-1, -2, -2) },
                                   { AVM_CDF2(14241), AVM_PARA2(-2, -2, -2) },
                               },
                               {
                                   { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                   { AVM_CDF2(5725), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(13420), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(12780), AVM_PARA2(-1, -1, -2) },
                                   { AVM_CDF2(10781), AVM_PARA2(-2, -2, -2) },
                                   { AVM_CDF2(6424), AVM_PARA2(-2, -2, -2) },
                               },
                           },
                           {
                               {
                                   { AVM_CDF2(11634), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(10093), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(6065), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(5408), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(6411), AVM_PARA2(-1, -1, -1) },
                                   { AVM_CDF2(4075), AVM_PARA2(-1, -1, 0) },
                               },
                               {
                                   { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                   { AVM_CDF2(898), AVM_PARA2(0, 1, 0) },
                                   { AVM_CDF2(3127), AVM_PARA2(0, 0, -1) },
                                   { AVM_CDF2(1775), AVM_PARA2(0, -1, 0) },
                                   { AVM_CDF2(1217), AVM_PARA2(0, -1, -1) },
                                   { AVM_CDF2(591), AVM_PARA2(0, -1, 0) },
                               },
                           },
                         };

static const avm_cdf_prob
    default_comp_group_idx_cdfs[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(17510), AVM_PARA2(-1, 0, -1) },
      { AVM_CDF2(10382), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(8336), AVM_PARA2(-1, -1, -1) },
      { AVM_CDF2(6054), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(5764), AVM_PARA2(0, -1, 0) },
      { AVM_CDF2(7154), AVM_PARA2(0, -2, 0) },
      { AVM_CDF2(15013), AVM_PARA2(-1, 0, -1) },
      { AVM_CDF2(8426), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(8278), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(4856), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(3464), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(5295), AVM_PARA2(0, 0, 0) },
    };

static const avm_cdf_prob
    default_skip_mode_cdfs[SKIP_MODE_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(30964), AVM_PARA2(1, 1, 1) },
      { AVM_CDF2(21769), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(12484), AVM_PARA2(0, 1, 0) },
    };

static const avm_cdf_prob
    default_lossless_tx_size_cdf[BLOCK_SIZE_GROUPS][LOSSLESS_TX_SIZE_PLANE_CTXS]
                                [CDF_SIZE(2)] = {
                                  {
                                      { AVM_CDF2(16384), AVM_PARA2(0, 0, -1) },
                                      { AVM_CDF2(16384), AVM_PARA2(0, 0, -1) },
                                  },
                                  {
                                      { AVM_CDF2(16384), AVM_PARA2(1, 0, 0) },
                                      { AVM_CDF2(16384), AVM_PARA2(1, 0, 0) },
                                  },
                                  {
                                      { AVM_CDF2(16384), AVM_PARA2(1, 0, 0) },
                                      { AVM_CDF2(16384), AVM_PARA2(1, 0, 0) },
                                  },
                                  {
                                      { AVM_CDF2(16384), AVM_PARA2(1, 0, 0) },
                                      { AVM_CDF2(16384), AVM_PARA2(1, 0, 0) },
                                  },
                                };

static const avm_cdf_prob default_lossless_inter_tx_type_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(16384), AVM_PARA2(0, 0, 0)
};

static const avm_cdf_prob default_skip_txfm_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)] = {
  { AVM_CDF2(25865), AVM_PARA2(-1, 0, 0) },
  { AVM_CDF2(14316), AVM_PARA2(0, 0, 0) },
  { AVM_CDF2(4598), AVM_PARA2(0, 0, 0) },
  { AVM_CDF2(25612), AVM_PARA2(0, -1, -1) },
  { AVM_CDF2(12366), AVM_PARA2(0, 0, -1) },
  { AVM_CDF2(3320), AVM_PARA2(1, 1, 0) },
};

static const avm_cdf_prob default_bru_mode_cdf[CDF_SIZE(3)] = {
  AVM_CDF3(4124, 16615), AVM_PARA3(0, 0, 0)
};

static const avm_cdf_prob
    default_intra_inter_cdf[INTRA_INTER_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(1522), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(14381), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(10455), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(27796), AVM_PARA2(0, 0, 0) },
    };

static const avm_cdf_prob default_intrabc_cdf[INTRABC_CONTEXTS][CDF_SIZE(2)] = {
  { AVM_CDF2(32085), AVM_PARA2(0, -1, 0) },
  { AVM_CDF2(15172), AVM_PARA2(-1, -1, 0) },
  { AVM_CDF2(4503), AVM_PARA2(0, 0, 0) },
};

static const avm_cdf_prob default_intrabc_mode_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(29993), AVM_PARA2(0, -1, -1)
};

static const avm_cdf_prob
    default_intrabc_bv_precision_cdf[NUM_BV_PRECISION_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(19778), AVM_PARA2(-1, -1, -1) },
    };

static const avm_cdf_prob default_switchable_flex_restore_cdf
    [MAX_LR_FLEX_SWITCHABLE_BITS][MAX_LR_FLEX_MB_PLANE][CDF_SIZE(2)] = {
      {
          { AVM_CDF2(25542), AVM_PARA2(-2, -2, -2) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      },
      {
          { AVM_CDF2(25550), AVM_PARA2(-1, -2, -2) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      },
      {
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      },
    };

static const avm_cdf_prob default_wienerns_restore_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(9051), AVM_PARA2(-1, -1, -2)
};

static const avm_cdf_prob
    default_wienerns_length_cdf[WIENERNS_LENGTH_CTXS][CDF_SIZE(2)] = {
      { AVM_CDF2(4898), AVM_PARA2(-2, -1, -1) },
      { AVM_CDF2(6088), AVM_PARA2(-1, 0, -1) },
    };

static const avm_cdf_prob default_wienerns_uv_sym_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(29286), AVM_PARA2(-2, 1, 0)
};

static const avm_cdf_prob
    default_wienerns_4part_cdf[WIENERNS_4PART_CTX_MAX][CDF_SIZE(4)] = {
      { AVM_CDF4(16557, 24352, 29677), AVM_PARA4(0, -1, -1) },
    };

static const avm_cdf_prob default_pc_wiener_restore_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(12799), AVM_PARA2(-1, 0, 0)
};

static const avm_cdf_prob
    default_ccso_cdf[CCSO_PLANES][CCSO_CONTEXT][CDF_SIZE(2)] = {
      {
          { AVM_CDF2(18469), AVM_PARA2(-2, -2, -2) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(4949), AVM_PARA2(-1, -2, -2) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      },
      {
          { AVM_CDF2(23470), AVM_PARA2(-1, -2, -2) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(6666), AVM_PARA2(-1, -2, -2) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      },
      {
          { AVM_CDF2(22914), AVM_PARA2(-1, -2, -2) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(6993), AVM_PARA2(-1, -2, -2) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      },
    };

static const avm_cdf_prob
    default_cdef_strength_index0_cdf[CDEF_STRENGTH_INDEX0_CTX][CDF_SIZE(2)] = {
      { AVM_CDF2(29034), AVM_PARA2(-1, -1, -2) },
      { AVM_CDF2(16472), AVM_PARA2(-1, -1, -2) },
      { AVM_CDF2(5751), AVM_PARA2(-1, -1, -2) },
      { AVM_CDF2(3115), AVM_PARA2(-1, -1, -1) },
    };

static const avm_cdf_prob default_cdef_cdf[CDEF_STRENGTHS_NUM -
                                           1][CDF_SIZE(7)] = {
  { AVM_CDF2(17708), AVM_PARA2(-1, -1, -2) },
  { AVM_CDF3(13413, 24899), AVM_PARA3(-1, -1, -2) },
  { AVM_CDF4(10588, 19866, 26664), AVM_PARA4(-1, -2, -2) },
  { AVM_CDF5(10131, 17874, 23876, 28766), AVM_PARA5(-1, -2, -2) },
  { AVM_CDF6(8363, 15451, 20811, 25453, 29393), AVM_PARA6(-1, -1, -2) },
  { AVM_CDF7(7372, 13867, 18969, 23278, 26977, 30156), AVM_PARA7(-1, -1, -2) },
};

static const avm_cdf_prob default_gdf_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(14593), AVM_PARA2(-1, -1, -2)
};

static const avm_cdf_prob
    default_mrl_index_cdf[MRL_INDEX_CONTEXTS][CDF_SIZE(4)] = {
      { AVM_CDF4(29573, 31193, 32023), AVM_PARA4(1, 0, 1) },
      { AVM_CDF4(21812, 27066, 30279), AVM_PARA4(1, 0, 0) },
      { AVM_CDF4(16076, 23806, 28762), AVM_PARA4(0, 0, -1) },
    };

static const avm_cdf_prob
    default_multi_line_mrl_cdf[MRL_INDEX_CONTEXTS][CDF_SIZE(2)] = {
      { AVM_CDF2(19678), AVM_PARA2(0, -1, -1) },
      { AVM_CDF2(12287), AVM_PARA2(0, -1, -1) },
      { AVM_CDF2(9574), AVM_PARA2(0, -1, -1) },
    };

static const avm_cdf_prob default_dpcm_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(16384), AVM_PARA2(0, 0, 0)
};

static const avm_cdf_prob default_dpcm_vert_horz_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(16384), AVM_PARA2(0, 0, 0)
};

static const avm_cdf_prob default_dpcm_uv_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(16384), AVM_PARA2(0, 0, 0)
};

static const avm_cdf_prob default_dpcm_uv_vert_horz_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(16384), AVM_PARA2(0, 0, 0)
};

static const avm_cdf_prob
    default_fsc_mode_cdf[FSC_MODE_CONTEXTS][FSC_BSIZE_CONTEXTS][CDF_SIZE(2)] = {
      {
          { AVM_CDF2(30503), AVM_PARA2(0, 0, 1) },
          { AVM_CDF2(31244), AVM_PARA2(0, 0, 1) },
          { AVM_CDF2(32254), AVM_PARA2(1, 0, 1) },
          { AVM_CDF2(32324), AVM_PARA2(1, 1, 1) },
          { AVM_CDF2(32582), AVM_PARA2(1, 1, 1) },
          { AVM_CDF2(32691), AVM_PARA2(1, 1, 1) },
      },
      {
          { AVM_CDF2(27437), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(27242), AVM_PARA2(1, 1, 0) },
          { AVM_CDF2(28040), AVM_PARA2(1, 0, -1) },
          { AVM_CDF2(27589), AVM_PARA2(1, 0, -1) },
          { AVM_CDF2(27234), AVM_PARA2(0, -1, -2) },
          { AVM_CDF2(23583), AVM_PARA2(-2, -2, -2) },
      },
      {
          { AVM_CDF2(26068), AVM_PARA2(1, 0, 0) },
          { AVM_CDF2(22635), AVM_PARA2(1, 0, 0) },
          { AVM_CDF2(22069), AVM_PARA2(0, -1, -1) },
          { AVM_CDF2(19218), AVM_PARA2(-1, -1, -2) },
          { AVM_CDF2(13701), AVM_PARA2(-1, -1, -1) },
          { AVM_CDF2(4636), AVM_PARA2(-1, -2, 1) },
      },
      {
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(32016), AVM_PARA2(0, 1, 0) },
          { AVM_CDF2(32403), AVM_PARA2(1, 1, 1) },
          { AVM_CDF2(32583), AVM_PARA2(0, 1, 0) },
          { AVM_CDF2(32683), AVM_PARA2(1, 0, -1) },
      },
    };

static const avm_cdf_prob default_cfl_index_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(12507), AVM_PARA2(-1, -1, 0)
};

static const avm_cdf_prob default_cfl_mhccp_switch_cdf[CDF_SIZE(2)] = {
  AVM_CDF2(15499), AVM_PARA2(-1, -1, 0)
};

static const avm_cdf_prob default_cfl_sign_cdf[CDF_SIZE(8)] = {
  AVM_CDF8(2421, 4332, 11256, 12766, 21386, 28725, 32087), AVM_PARA8(-2, -2, -2)
};

static const avm_cdf_prob
    default_cfl_alpha_cdf[CFL_ALPHA_CONTEXTS][CDF_SIZE(8)] = {
      { AVM_CDF8(21679, 25305, 30646, 31512, 32537, 32646, 32696),
        AVM_PARA8(-2, -2, -2) },
      { AVM_CDF8(8262, 16302, 24082, 29422, 31398, 32286, 32525),
        AVM_PARA8(-2, -2, -2) },
      { AVM_CDF8(17235, 26166, 30378, 31305, 32373, 32549, 32668),
        AVM_PARA8(-2, -2, -2) },
      { AVM_CDF8(17618, 25732, 27865, 30338, 31125, 31522, 32238),
        AVM_PARA8(-2, -2, -2) },
      { AVM_CDF8(17542, 23066, 27907, 28728, 30702, 31165, 31435),
        AVM_PARA8(-2, -2, -2) },
      { AVM_CDF8(17675, 24802, 30468, 30783, 31841, 32264, 32422),
        AVM_PARA8(-2, -2, -2) },
    };

static const avm_cdf_prob default_y_mode_set_cdf[CDF_SIZE(4)] = {
  AVM_CDF4(28863, 31022, 31724), AVM_PARA4(1, 1, 1)
};

static const avm_cdf_prob
    default_y_mode_idx_cdf[Y_MODE_CONTEXTS][CDF_SIZE(8)] = {
      { AVM_CDF8(15175, 20075, 21728, 24098, 26405, 27655, 28860),
        AVM_PARA8(0, -1, 0) },
      { AVM_CDF8(10114, 14957, 16815, 19127, 20147, 25583, 27169),
        AVM_PARA8(0, 0, 0) },
      { AVM_CDF8(5636, 9004, 10456, 12122, 12744, 20325, 25607),
        AVM_PARA8(0, 0, 0) },
    };

static const avm_cdf_prob
    default_y_mode_idx_offset_cdf[Y_MODE_CONTEXTS][CDF_SIZE(6)] = {
      { AVM_CDF6(12743, 18172, 20194, 23648, 26419), AVM_PARA6(0, -1, -1) },
      { AVM_CDF6(8976, 16084, 20827, 24595, 28496), AVM_PARA6(1, 0, 0) },
      { AVM_CDF6(8784, 14556, 19710, 24903, 28724), AVM_PARA6(1, 0, 0) },
    };

static const avm_cdf_prob default_uv_mode_cdf[UV_MODE_CONTEXTS][CDF_SIZE(8)] = {
  { AVM_CDF8(9363, 20957, 22865, 24753, 26411, 27983, 30428),
    AVM_PARA8(-1, -1, -1) },
  { AVM_CDF8(21282, 23610, 28208, 29311, 30348, 31158, 31491),
    AVM_PARA8(-1, -1, 0) },
};

static const avm_cdf_prob
    default_switchable_interp_cdf[SWITCHABLE_FILTER_CONTEXTS][CDF_SIZE(3)] = {
      { AVM_CDF3(29975, 32748), AVM_PARA3(-1, 0, 0) },
      { AVM_CDF3(2076, 32703), AVM_PARA3(1, 0, 0) },
      { AVM_CDF3(19, 1768), AVM_PARA3(0, 1, 0) },
      { AVM_CDF3(17314, 27415), AVM_PARA3(-1, -1, -1) },
      { AVM_CDF3(31286, 31994), AVM_PARA3(0, 0, 0) },
      { AVM_CDF3(9581, 32608), AVM_PARA3(-1, -1, -1) },
      { AVM_CDF3(535, 1036), AVM_PARA3(0, 0, 0) },
      { AVM_CDF3(24819, 27722), AVM_PARA3(-1, -1, -1) },
    };

static const avm_cdf_prob default_delta_q_cdf[CDF_SIZE(8)] = {
  AVM_CDF8(16594, 23325, 26424, 28225, 29358, 30099, 30613),
  AVM_PARA8(-2, -1, -1)
};

static const avm_cdf_prob
    default_intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES][CDF_SIZE(16)] = {
      {
          { 0 },
          { 0 },
          { 0 },
          { 0 },
      },
      {
          { AVM_CDF7(5026, 16816, 19974, 23404, 26845, 30499),
            AVM_PARA7(0, -1, 1) },
          { AVM_CDF7(14910, 25257, 26964, 29323, 30237, 31535),
            AVM_PARA7(0, 0, 0) },
          { AVM_CDF7(13759, 26108, 27688, 29793, 30265, 31576),
            AVM_PARA7(-1, -2, 0) },
          { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
            AVM_PARA7(0, 0, 0) },
      },
      {
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      },
    };

static const avm_cdf_prob
    default_intra_ext_tx_short_side_cdf[EXT_TX_SIZES][CDF_SIZE(4)] = {
      { AVM_CDF4(10692, 26586, 29231), AVM_PARA4(-1, -1, -1) },
      { AVM_CDF4(26700, 32160, 32748), AVM_PARA4(0, -1, -1) },
      { AVM_CDF4(26915, 32411, 32748), AVM_PARA4(0, -1, -1) },
      { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
    };

static const avm_cdf_prob default_inter_tx_type_set_cdf
    [INTER_TX_TYPE_SIGNALING_SETS][EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(2)] = {
      {
          {
              { AVM_CDF2(14122), AVM_PARA2(-1, 0, 0) },
              { AVM_CDF2(8962), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
          {
              { AVM_CDF2(16423), AVM_PARA2(-1, 0, -1) },
              { AVM_CDF2(23446), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
          {
              { AVM_CDF2(23352), AVM_PARA2(-1, 0, 0) },
              { AVM_CDF2(17069), AVM_PARA2(-2, -1, -1) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
      },
      {
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(20835), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(30720), AVM_PARA2(0, 0, 1) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(28857), AVM_PARA2(0, 1, -2) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
      },
    };

static const avm_cdf_prob default_inter_tx_type_idx_cdf
    [INTER_TX_TYPE_SIGNALING_SETS][EOB_TX_CTXS][CDF_SIZE(8)] = {
      {
          { AVM_CDF8(8914, 10732, 12270, 14822, 17128, 19586, 21964),
            AVM_PARA8(-1, 0, 0) },
          { AVM_CDF8(1160, 1555, 1972, 3414, 3962, 5151, 5908),
            AVM_PARA8(-1, -1, -1) },
          { AVM_CDF8(22819, 24338, 25592, 27001, 28395, 29648, 30990),
            AVM_PARA8(-1, 0, 0) },
      },
      {
          { AVM_CDF8(1140, 1725, 2324, 14653, 19072, 23618, 28109),
            AVM_PARA8(-1, -1, -1) },
          { AVM_CDF8(58, 261, 587, 32317, 32556, 32626, 32708),
            AVM_PARA8(0, 0, -2) },
          { AVM_CDF8(17404, 17669, 18403, 24052, 26393, 28506, 30676),
            AVM_PARA8(-2, -1, -2) },
      },
    };

static const avm_cdf_prob
    default_inter_tx_type_offset_1_cdf[EOB_TX_CTXS][CDF_SIZE(8)] = {
      { AVM_CDF8(3121, 6470, 9191, 12280, 17811, 22588, 27697),
        AVM_PARA8(1, 1, 1) },
      { AVM_CDF8(338, 377, 571, 743, 7932, 11860, 17524), AVM_PARA8(0, 0, -1) },
      { AVM_CDF8(3314, 7625, 10639, 14206, 19363, 23456, 28033),
        AVM_PARA8(1, 1, 1) },
    };

static const avm_cdf_prob
    default_inter_tx_type_offset_2_cdf[EOB_TX_CTXS][CDF_SIZE(4)] = {
      { AVM_CDF4(8669, 16533, 24855), AVM_PARA4(1, 1, 1) },
      { AVM_CDF4(9441, 16413, 25276), AVM_PARA4(1, 1, 0) },
      { AVM_CDF4(8767, 17611, 24876), AVM_PARA4(0, -2, 0) },
    };

static const avm_cdf_prob default_inter_ext_tx_cdf
    [EXT_TX_SETS_INTER][EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(16)] = {
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
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
      },
      {
          {
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
          },
          {
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
          },
          {
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
              { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
          },
      },
    };

static const avm_cdf_prob
    default_inter_ext_tx_short_side_cdf[EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(
        4)] = {
      {
          { AVM_CDF4(8347, 20254, 24536), AVM_PARA4(-1, -1, -1) },
          { AVM_CDF4(15994, 26294, 32748), AVM_PARA4(0, 0, -1) },
          { AVM_CDF4(21212, 27810, 32748), AVM_PARA4(0, 0, -1) },
          { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
      },
      {
          { AVM_CDF4(21815, 25084, 26230), AVM_PARA4(-2, -2, -2) },
          { AVM_CDF4(29354, 31747, 32748), AVM_PARA4(-1, -2, -2) },
          { AVM_CDF4(31614, 32529, 32748), AVM_PARA4(-1, -2, -1) },
          { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
      },
      {
          { AVM_CDF4(10501, 20590, 24181), AVM_PARA4(-1, -1, -1) },
          { AVM_CDF4(17596, 26388, 32748), AVM_PARA4(-1, -2, -2) },
          { AVM_CDF4(15407, 26475, 32732), AVM_PARA4(-2, -2, 0) },
          { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
      },
    };

static const avm_cdf_prob default_tx_ext_32_cdf[TX_EXT_32_CTXS][CDF_SIZE(2)] = {
  { AVM_CDF2(36), AVM_PARA2(0, 0, 0) },
  { AVM_CDF2(128), AVM_PARA2(-1, -1, -1) },
};

static const avm_cdf_prob
    default_stx_cdf[STX_PRED_CTXS][TX_SIZES][CDF_SIZE(4)] = {
      {
          { AVM_CDF4(4486, 15589, 26440), AVM_PARA4(1, 0, 0) },
          { AVM_CDF4(2357, 9504, 16641), AVM_PARA4(0, -1, 0) },
          { AVM_CDF4(1364, 8034, 14431), AVM_PARA4(0, 0, 0) },
          { AVM_CDF4(2472, 8725, 13853), AVM_PARA4(1, 0, -1) },
          { AVM_CDF4(7523, 11681, 14783), AVM_PARA4(0, 0, -1) },
      },
      {
          { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
          { AVM_CDF4(8192, 16384, 24576), AVM_PARA4(0, 0, 0) },
          { AVM_CDF4(10490, 15394, 22206), AVM_PARA4(-1, -1, -1) },
          { AVM_CDF4(13967, 16953, 23109), AVM_PARA4(-1, -1, -1) },
          { AVM_CDF4(20191, 21727, 25818), AVM_PARA4(-1, -1, -2) },
      },
    };

static const avm_cdf_prob default_cctx_type_cdf[CDF_SIZE(7)] = {
  AVM_CDF7(14350, 14836, 16054, 17075, 19408, 28530), AVM_PARA7(-2, -2, -2)
};

static const avm_cdf_prob
    default_pb_mv_most_probable_precision_cdf[NUM_MV_PREC_MPP_CONTEXT][CDF_SIZE(
        2)] = {
      { AVM_CDF2(20650), AVM_PARA2(-1, -1, -1) },
      { AVM_CDF2(15758), AVM_PARA2(-1, -1, -1) },
      { AVM_CDF2(4571), AVM_PARA2(-1, -1, -1) },
    };

static const avm_cdf_prob default_pb_mv_precision_cdf
    [MV_PREC_DOWN_CONTEXTS][NUM_PB_FLEX_QUALIFIED_MAX_PREC][CDF_SIZE(3)] = {
      {
          { AVM_CDF3(10923, 21845), AVM_PARA3(0, 0, 0) },
          { AVM_CDF3(31340, 32505), AVM_PARA3(1, 0, 1) },
          { AVM_CDF3(26039, 32175), AVM_PARA3(0, 0, 0) },
      },
      {
          { AVM_CDF3(10923, 21845), AVM_PARA3(0, 0, 0) },
          { AVM_CDF3(32197, 32676), AVM_PARA3(1, 0, 0) },
          { AVM_CDF3(28679, 32626), AVM_PARA3(0, 0, 0) },
      },
    };

static const avm_cdf_prob
    default_do_split_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS]
                        [CDF_SIZE(2)] = {
                          {
                              { AVM_CDF2(28084), AVM_PARA2(0, 0, 1) },
                              { AVM_CDF2(23755), AVM_PARA2(1, 1, 1) },
                              { AVM_CDF2(23634), AVM_PARA2(1, 1, 1) },
                              { AVM_CDF2(19368), AVM_PARA2(0, 0, 1) },
                              { AVM_CDF2(24961), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(14941), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(16154), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(5905), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(21934), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(10440), AVM_PARA2(-1, 0, -1) },
                              { AVM_CDF2(11984), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(3474), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(20492), AVM_PARA2(0, 1, -1) },
                              { AVM_CDF2(6963), AVM_PARA2(0, -1, -1) },
                              { AVM_CDF2(8099), AVM_PARA2(-1, 0, -1) },
                              { AVM_CDF2(1529), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(24117), AVM_PARA2(1, 1, -2) },
                              { AVM_CDF2(7871), AVM_PARA2(0, -2, 0) },
                              { AVM_CDF2(23604), AVM_PARA2(0, 0, -2) },
                              { AVM_CDF2(8429), AVM_PARA2(-1, -1, 0) },
                              { AVM_CDF2(27356), AVM_PARA2(0, 0, -2) },
                              { AVM_CDF2(22441), AVM_PARA2(0, -1, -2) },
                              { AVM_CDF2(8897), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(6811), AVM_PARA2(-2, -2, -1) },
                              { AVM_CDF2(17592), AVM_PARA2(0, 1, -1) },
                              { AVM_CDF2(5648), AVM_PARA2(-1, -1, -2) },
                              { AVM_CDF2(5339), AVM_PARA2(-1, 0, -1) },
                              { AVM_CDF2(1082), AVM_PARA2(-1, 0, -1) },
                              { AVM_CDF2(26143), AVM_PARA2(1, 0, -2) },
                              { AVM_CDF2(11379), AVM_PARA2(1, -2, 0) },
                              { AVM_CDF2(20142), AVM_PARA2(1, 1, 1) },
                              { AVM_CDF2(7401), AVM_PARA2(0, -1, 1) },
                              { AVM_CDF2(26235), AVM_PARA2(1, -1, -2) },
                              { AVM_CDF2(23674), AVM_PARA2(1, 0, 1) },
                              { AVM_CDF2(12441), AVM_PARA2(1, 0, -2) },
                              { AVM_CDF2(10482), AVM_PARA2(1, 0, 0) },
                              { AVM_CDF2(20663), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(4192), AVM_PARA2(-1, 0, -2) },
                              { AVM_CDF2(5274), AVM_PARA2(-1, -1, 1) },
                              { AVM_CDF2(713), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(28255), AVM_PARA2(1, 0, 0) },
                              { AVM_CDF2(27370), AVM_PARA2(1, 0, 0) },
                              { AVM_CDF2(23527), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(20990), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(26727), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(21187), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(25324), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(17838), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(26136), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16591), AVM_PARA2(0, -1, -1) },
                              { AVM_CDF2(19838), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(10605), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(22914), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(12609), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(11341), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(4556), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(24218), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(13059), AVM_PARA2(0, -1, -2) },
                              { AVM_CDF2(15378), AVM_PARA2(-1, -1, -2) },
                              { AVM_CDF2(5858), AVM_PARA2(-1, -1, -2) },
                              { AVM_CDF2(21644), AVM_PARA2(-1, -1, -2) },
                              { AVM_CDF2(7767), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(8309), AVM_PARA2(0, -1, -1) },
                              { AVM_CDF2(1687), AVM_PARA2(0, 0, 0) },
                          },
                          {
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(25554), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(18892), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(18530), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(10806), AVM_PARA2(0, -1, -1) },
                              { AVM_CDF2(22504), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(12140), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(11966), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(4984), AVM_PARA2(-1, -1, 0) },
                              { AVM_CDF2(24460), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(8698), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(9655), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(2563), AVM_PARA2(-1, -1, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(26227), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(17669), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(26179), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(17889), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                              { AVM_CDF2(23500), AVM_PARA2(0, 0, -1) },
                              { AVM_CDF2(13115), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(15253), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(6458), AVM_PARA2(-2, -1, 0) },
                              { AVM_CDF2(22566), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(11497), AVM_PARA2(-1, -1, -2) },
                              { AVM_CDF2(10045), AVM_PARA2(-1, -1, -1) },
                              { AVM_CDF2(3750), AVM_PARA2(-1, 0, -1) },
                          },
                        };

static const avm_cdf_prob
    default_do_square_split_cdf[PARTITION_STRUCTURE_NUM][SQUARE_SPLIT_CONTEXTS]
                               [CDF_SIZE(2)] = {
                                 {
                                     { AVM_CDF2(18000), AVM_PARA2(0, -1, -2) },
                                     { AVM_CDF2(10521), AVM_PARA2(-1, -2, -2) },
                                     { AVM_CDF2(11395), AVM_PARA2(-2, -2, -2) },
                                     { AVM_CDF2(4419), AVM_PARA2(-1, -1, -2) },
                                     { AVM_CDF2(12996), AVM_PARA2(1, -2, 0) },
                                     { AVM_CDF2(8185), AVM_PARA2(-2, -1, 0) },
                                     { AVM_CDF2(10979), AVM_PARA2(-1, -2, -1) },
                                     { AVM_CDF2(5010), AVM_PARA2(-1, -1, -2) },
                                 },
                                 {
                                     { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                     { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                     { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                     { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                     { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                     { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                     { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                     { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                                 },
                               };

static const avm_cdf_prob
    default_rect_type_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS]
                         [CDF_SIZE(2)] = {
                           {
                               { AVM_CDF2(14644), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(10173), AVM_PARA2(1, 0, 0) },
                               { AVM_CDF2(18529), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16071), AVM_PARA2(1, 1, 0) },
                               { AVM_CDF2(20263), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(12813), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(26612), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(23277), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(10594), AVM_PARA2(1, 0, -1) },
                               { AVM_CDF2(7000), AVM_PARA2(1, 0, 0) },
                               { AVM_CDF2(20002), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(12889), AVM_PARA2(0, 0, -2) },
                               { AVM_CDF2(13854), AVM_PARA2(1, 0, -1) },
                               { AVM_CDF2(10750), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(18380), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(17505), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(14430), AVM_PARA2(0, -1, -2) },
                               { AVM_CDF2(11554), AVM_PARA2(0, 0, -2) },
                               { AVM_CDF2(20078), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(19097), AVM_PARA2(1, 0, -1) },
                               { AVM_CDF2(15278), AVM_PARA2(0, 0, -2) },
                               { AVM_CDF2(10137), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(21921), AVM_PARA2(0, -1, -2) },
                               { AVM_CDF2(14621), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(19330), AVM_PARA2(0, 0, -2) },
                               { AVM_CDF2(15921), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(26218), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(24318), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16066), AVM_PARA2(1, 0, 1) },
                               { AVM_CDF2(9225), AVM_PARA2(0, 0, -2) },
                               { AVM_CDF2(22849), AVM_PARA2(-1, -1, -1) },
                               { AVM_CDF2(14817), AVM_PARA2(0, -2, -1) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(18543), AVM_PARA2(1, 0, 0) },
                               { AVM_CDF2(13210), AVM_PARA2(0, -2, 0) },
                               { AVM_CDF2(24367), AVM_PARA2(-1, -1, -2) },
                               { AVM_CDF2(18417), AVM_PARA2(-1, 0, 0) },
                               { AVM_CDF2(24701), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(18911), AVM_PARA2(0, -1, -2) },
                               { AVM_CDF2(29590), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(27778), AVM_PARA2(0, -1, -2) },
                               { AVM_CDF2(3400), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(935), AVM_PARA2(1, 1, 0) },
                               { AVM_CDF2(10365), AVM_PARA2(-1, -1, -2) },
                               { AVM_CDF2(1723), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                           },
                           {
                               { AVM_CDF2(15654), AVM_PARA2(1, 0, -1) },
                               { AVM_CDF2(13500), AVM_PARA2(1, 0, 0) },
                               { AVM_CDF2(19177), AVM_PARA2(1, 1, -1) },
                               { AVM_CDF2(14739), AVM_PARA2(1, 1, 0) },
                               { AVM_CDF2(18769), AVM_PARA2(1, 0, 0) },
                               { AVM_CDF2(13500), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(23583), AVM_PARA2(1, 0, 0) },
                               { AVM_CDF2(20927), AVM_PARA2(1, 0, 0) },
                               { AVM_CDF2(15045), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(10528), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(22474), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(14250), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16561), AVM_PARA2(1, 0, 0) },
                               { AVM_CDF2(11427), AVM_PARA2(1, 0, -1) },
                               { AVM_CDF2(21874), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(16344), AVM_PARA2(1, 1, 0) },
                               { AVM_CDF2(21566), AVM_PARA2(-1, -1, -1) },
                               { AVM_CDF2(13357), AVM_PARA2(0, 0, -2) },
                               { AVM_CDF2(27355), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(24117), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(10901), AVM_PARA2(1, 0, -2) },
                               { AVM_CDF2(5780), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(19056), AVM_PARA2(-1, -2, -2) },
                               { AVM_CDF2(9141), AVM_PARA2(0, 0, -1) },
                               { AVM_CDF2(20436), AVM_PARA2(0, -1, -2) },
                               { AVM_CDF2(15693), AVM_PARA2(-1, -1, -2) },
                               { AVM_CDF2(26536), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(23667), AVM_PARA2(-1, -1, -1) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(19241), AVM_PARA2(0, -1, -2) },
                               { AVM_CDF2(13038), AVM_PARA2(-1, -1, -2) },
                               { AVM_CDF2(28903), AVM_PARA2(-1, -1, -2) },
                               { AVM_CDF2(24802), AVM_PARA2(-1, -1, -2) },
                               { AVM_CDF2(9097), AVM_PARA2(-1, -1, -2) },
                               { AVM_CDF2(2749), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(15201), AVM_PARA2(-1, 0, -2) },
                               { AVM_CDF2(4449), AVM_PARA2(0, -1, -1) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                               { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
                           },
                         };

static const avm_cdf_prob default_do_ext_partition_cdf
    [PARTITION_STRUCTURE_NUM][NUM_RECT_CONTEXTS][PARTITION_CONTEXTS]
    [CDF_SIZE(2)] = {
      {
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(31091), AVM_PARA2(0, 0, 1) },
              { AVM_CDF2(29638), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(28924), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(28653), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(30349), AVM_PARA2(1, 1, 1) },
              { AVM_CDF2(28265), AVM_PARA2(1, 0, 0) },
              { AVM_CDF2(27287), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(27721), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(29960), AVM_PARA2(1, 1, 1) },
              { AVM_CDF2(28345), AVM_PARA2(1, 1, 0) },
              { AVM_CDF2(27302), AVM_PARA2(1, 1, 0) },
              { AVM_CDF2(27252), AVM_PARA2(1, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(30342), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(27563), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(26168), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(23089), AVM_PARA2(-1, -1, -2) },
              { AVM_CDF2(30643), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(28683), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(28009), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(26186), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(29222), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(25740), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(24079), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(19806), AVM_PARA2(1, 0, -1) },
              { AVM_CDF2(29409), AVM_PARA2(1, 1, 0) },
              { AVM_CDF2(26825), AVM_PARA2(1, 1, 1) },
              { AVM_CDF2(25919), AVM_PARA2(1, 1, 1) },
              { AVM_CDF2(24417), AVM_PARA2(1, 1, 1) },
          },
      },
      {
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(26866), AVM_PARA2(-1, -1, 0) },
              { AVM_CDF2(24499), AVM_PARA2(-1, 0, 0) },
              { AVM_CDF2(24732), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(23387), AVM_PARA2(-1, 0, 0) },
              { AVM_CDF2(27477), AVM_PARA2(-1, -1, 0) },
              { AVM_CDF2(25837), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(24621), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(23604), AVM_PARA2(0, -1, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(24384), AVM_PARA2(-1, 0, -1) },
              { AVM_CDF2(22113), AVM_PARA2(-1, -1, -2) },
              { AVM_CDF2(21798), AVM_PARA2(0, 0, -2) },
              { AVM_CDF2(20067), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(26220), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(22997), AVM_PARA2(-1, -1, 0) },
              { AVM_CDF2(22249), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(20091), AVM_PARA2(0, 0, 0) },
          },
      },
    };

static const avm_cdf_prob default_do_uneven_4way_partition_cdf
    [PARTITION_STRUCTURE_NUM][NUM_RECT_CONTEXTS][PARTITION_CONTEXTS]
    [CDF_SIZE(2)] = {
      {
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(25789), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(25290), AVM_PARA2(-1, -1, -2) },
              { AVM_CDF2(24270), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(22994), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(25801), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(25260), AVM_PARA2(0, -1, -2) },
              { AVM_CDF2(24041), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(24281), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(23038), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(19972), AVM_PARA2(-1, 1, 0) },
              { AVM_CDF2(19723), AVM_PARA2(0, -1, 1) },
              { AVM_CDF2(17939), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(15574), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(13761), AVM_PARA2(1, 0, -1) },
              { AVM_CDF2(12917), AVM_PARA2(1, 1, -1) },
              { AVM_CDF2(11328), AVM_PARA2(1, 0, 0) },
              { AVM_CDF2(17295), AVM_PARA2(-1, -1, -2) },
              { AVM_CDF2(14463), AVM_PARA2(-1, -2, 1) },
              { AVM_CDF2(14724), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(11653), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(13202), AVM_PARA2(-1, -1, -2) },
              { AVM_CDF2(10929), AVM_PARA2(0, -1, -2) },
              { AVM_CDF2(10348), AVM_PARA2(0, -1, -2) },
              { AVM_CDF2(8276), AVM_PARA2(0, 0, -1) },
          },
      },
      {
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(24728), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(22673), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(21033), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(20321), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(24631), AVM_PARA2(-2, 1, -2) },
              { AVM_CDF2(21363), AVM_PARA2(0, 1, 0) },
              { AVM_CDF2(20201), AVM_PARA2(0, 1, 0) },
              { AVM_CDF2(17529), AVM_PARA2(1, -1, -2) },
              { AVM_CDF2(21042), AVM_PARA2(0, 0, 1) },
              { AVM_CDF2(18640), AVM_PARA2(1, -1, 0) },
              { AVM_CDF2(18183), AVM_PARA2(1, 1, -1) },
              { AVM_CDF2(15590), AVM_PARA2(1, 0, -1) },
          },
      },
    };

static const avm_cdf_prob default_txfm_4way_partition_type_reduced_cdf
    [FSC_MODES][TX_PARTITION_DIRS][TX_PARTITION_TYPE_NUM_VERT_AND_HORZ]
    [CDF_SIZE(7)] = {
      {
          {
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
          },
          {
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
          },
      },
      {
          {
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
          },
          {
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
          },
      },
    };

static const avm_cdf_prob default_txfm_2or3_way_partition_type_cdf
    [FSC_MODES][TX_PARTITION_DIRS][TX_PARTITION_TYPE_NUM_VERT_OR_HORZ - 1]
    [CDF_SIZE(2)] = {
      {
          {
              { AVM_CDF2(25131), AVM_PARA2(-1, -1, 0) },
              { AVM_CDF2(24514), AVM_PARA2(-1, -1, 0) },
          },
          {
              { AVM_CDF2(19534), AVM_PARA2(0, -1, -1) },
              { AVM_CDF2(18637), AVM_PARA2(0, 0, 0) },
          },
      },
      {
          {
              { AVM_CDF2(30226), AVM_PARA2(-2, 0, 0) },
              { AVM_CDF2(30703), AVM_PARA2(-1, -1, 1) },
          },
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
      },
    };

static const avm_cdf_prob default_most_probable_stx_set_cdf[CDF_SIZE(7)] = {
  AVM_CDF7(20712, 26263, 30623, 32732, 32736, 32740), AVM_PARA7(-2, -1, 0)
};

static const avm_cdf_prob default_most_probable_stx_set_cdf_ADST_ADST[CDF_SIZE(
    4)] = { AVM_CDF4(15897, 26144, 30010), AVM_PARA4(0, -1, 0) };

static const avm_cdf_prob default_txfm_do_partition_cdf
    [FSC_MODES][TX_PARTITION_DIRS][TXFM_SPLIT_GROUP][CDF_SIZE(2)] = {
      {
          {
              { AVM_CDF2(26330), AVM_PARA2(-1, -1, 0) },
              { AVM_CDF2(29620), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(20420), AVM_PARA2(-1, -1, 0) },
              { AVM_CDF2(21694), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(13317), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(15391), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(15952), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(14736), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(13810), AVM_PARA2(0, -1, 0) },
          },
          {
              { AVM_CDF2(31646), AVM_PARA2(-2, -1, 0) },
              { AVM_CDF2(32393), AVM_PARA2(-1, 0, 0) },
              { AVM_CDF2(30802), AVM_PARA2(-1, 0, -1) },
              { AVM_CDF2(30485), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(20759), AVM_PARA2(-1, -1, -2) },
              { AVM_CDF2(22159), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(26832), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(27351), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(24696), AVM_PARA2(0, 0, -1) },
          },
      },
      {
          {
              { AVM_CDF2(29308), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(32550), AVM_PARA2(1, 1, 1) },
              { AVM_CDF2(27963), AVM_PARA2(0, 0, -1) },
              { AVM_CDF2(27618), AVM_PARA2(-1, -1, -1) },
              { AVM_CDF2(22367), AVM_PARA2(0, -1, -2) },
              { AVM_CDF2(23478), AVM_PARA2(-1, -2, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(27990), AVM_PARA2(0, 0, 0) },
          },
          {
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
              { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
          },
      },
    };

static const avm_cdf_prob default_txfm_4way_partition_type_cdf
    [FSC_MODES][TX_PARTITION_DIRS][TX_PARTITION_TYPE_NUM_VERT_AND_HORZ]
    [CDF_SIZE(7)] = {
      {
          {
              { AVM_CDF7(32744, 32748, 32752, 32756, 32760, 32764),
                AVM_PARA7(-2, -2, 0) },
              { AVM_CDF7(3006, 22888, 27132, 29972, 29976, 32724),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(3673, 8849, 27652, 27656, 29944, 29948),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(5219, 19419, 28052, 28836, 29528, 31296),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(3055, 19649, 22157, 27038, 27466, 31646),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(6044, 11255, 26170, 26493, 28585, 29584),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(5896, 20361, 25685, 27552, 28695, 31097),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(2355, 17601, 21703, 26050, 27881, 31397),
                AVM_PARA7(-1, -1, -2) },
              { AVM_CDF7(4701, 13502, 24958, 26413, 28166, 30129),
                AVM_PARA7(-1, -1, -2) },
              { AVM_CDF7(3319, 16449, 21641, 27154, 29260, 31512),
                AVM_PARA7(-1, -1, -2) },
              { AVM_CDF7(2526, 17088, 19643, 29378, 29382, 32724),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(2298, 4406, 23886, 23890, 30148, 30152),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(1553, 16160, 18679, 27983, 29592, 32139),
                AVM_PARA7(-1, -1, -2) },
              { AVM_CDF7(2316, 4714, 22731, 23797, 29514, 30077),
                AVM_PARA7(-1, -2, -2) },
          },
          {
              { AVM_CDF7(10790, 22602, 32736, 32740, 32744, 32748),
                AVM_PARA7(-1, -1, -2) },
              { AVM_CDF7(14384, 19116, 26545, 28687, 28691, 32724),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(13910, 21690, 26343, 26347, 28432, 28436),
                AVM_PARA7(-1, -1, -1) },
              { AVM_CDF7(15891, 21712, 28890, 29571, 30307, 31363),
                AVM_PARA7(-1, -1, -2) },
              { AVM_CDF7(15946, 19447, 20270, 23423, 23858, 31148),
                AVM_PARA7(-1, -1, -2) },
              { AVM_CDF7(16053, 16957, 20312, 20924, 24770, 25959),
                AVM_PARA7(-1, -1, -2) },
              { AVM_CDF7(18479, 23053, 26582, 26966, 27567, 29836),
                AVM_PARA7(-2, -2, -2) },
              { AVM_CDF7(9312, 11882, 14354, 17792, 19827, 29477),
                AVM_PARA7(-2, -2, -2) },
              { AVM_CDF7(8490, 10668, 13295, 15353, 19952, 22575),
                AVM_PARA7(-2, -2, -2) },
              { AVM_CDF7(6404, 12066, 16173, 20041, 24512, 28421),
                AVM_PARA7(-2, -2, -2) },
              { AVM_CDF7(10019, 14455, 17658, 27012, 27016, 32724),
                AVM_PARA7(-1, -2, -2) },
              { AVM_CDF7(9479, 14904, 19374, 19378, 28027, 28031),
                AVM_PARA7(-1, -2, -2) },
              { AVM_CDF7(3717, 7198, 8103, 20546, 23558, 31447),
                AVM_PARA7(-2, -2, -2) },
              { AVM_CDF7(4058, 5429, 8987, 13978, 25126, 26655),
                AVM_PARA7(-2, -2, -2) },
          },
      },
      {
          {
              { AVM_CDF7(32434, 32490, 32545, 32601, 32657, 32712),
                AVM_PARA7(-2, 0, 0) },
              { AVM_CDF7(1491, 14241, 29930, 32517, 32524, 32724),
                AVM_PARA7(-2, -1, -2) },
              { AVM_CDF7(1719, 16525, 31000, 31004, 32575, 32579),
                AVM_PARA7(-1, -1, 0) },
              { AVM_CDF7(1645, 16749, 29324, 30425, 32016, 32485),
                AVM_PARA7(-2, -2, -2) },
              { AVM_CDF7(2908, 15802, 24689, 28470, 32122, 32542),
                AVM_PARA7(-1, 0, 0) },
              { AVM_CDF7(3470, 17931, 25841, 29589, 31907, 32465),
                AVM_PARA7(-1, 1, 1) },
              { AVM_CDF7(5638, 19594, 28693, 29977, 30703, 32154),
                AVM_PARA7(-2, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(2734, 12129, 30342, 31805, 31844, 32724),
                AVM_PARA7(-2, 0, 0) },
              { AVM_CDF7(3849, 21783, 31043, 31056, 32181, 32193),
                AVM_PARA7(-2, 1, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
          },
          {
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
              { AVM_CDF7(4681, 9362, 14043, 18725, 23406, 28087),
                AVM_PARA7(0, 0, 0) },
          },
      },
    };

static const avm_cdf_prob
    default_filter_dir_cdf[MHCCP_CONTEXT_GROUP_SIZE][CDF_SIZE(3)] = {
      { AVM_CDF3(10923, 21845), AVM_PARA3(0, 0, 0) },
      { AVM_CDF3(8795, 15105), AVM_PARA3(0, -1, -1) },
      { AVM_CDF3(10433, 15974), AVM_PARA3(-1, -1, -2) },
      { AVM_CDF3(17085, 21689), AVM_PARA3(-1, -1, -2) },
    };

static const avm_cdf_prob default_cfl_cdf[CFL_CONTEXTS][CDF_SIZE(2)] = {
  { AVM_CDF2(20441), AVM_PARA2(-1, -1, 0) },
  { AVM_CDF2(11610), AVM_PARA2(0, -1, -1) },
  { AVM_CDF2(4643), AVM_PARA2(0, 0, 0) },
};

static const avm_cdf_prob
    default_region_type_cdf[INTER_SDP_BSIZE_GROUP][CDF_SIZE(2)] = {
      { AVM_CDF2(2635), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(883), AVM_PARA2(-1, 0, 0) },
      { AVM_CDF2(503), AVM_PARA2(-2, 0, 0) },
      { AVM_CDF2(279), AVM_PARA2(-2, -1, 0) },
    };

static const avm_cdf_prob
    default_morph_pred_cdf[MORPH_PRED_CTXS][CDF_SIZE(2)] = {
      { AVM_CDF2(31715), AVM_PARA2(-2, -1, 0) },
      { AVM_CDF2(19667), AVM_PARA2(0, 0, -1) },
      { AVM_CDF2(10555), AVM_PARA2(1, 1, -1) },
    };

static const avm_cdf_prob default_seg_tree_cdf[CDF_SIZE(8)] = {
  AVM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672), AVM_PARA8(0, 0, 0)
};

static const avm_cdf_prob default_seg_tree_cdf1[CDF_SIZE(8)] = {
  AVM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672), AVM_PARA8(0, 0, 0)
};

static const avm_cdf_prob
    default_segment_pred_cdf[SEG_TEMPORAL_PRED_CTXS][CDF_SIZE(2)] = {
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
    };

static const avm_cdf_prob
    default_spatial_pred_seg_tree_cdf[SPATIAL_PREDICTION_PROBS][CDF_SIZE(8)] = {
      { AVM_CDF8(5622, 7893, 16093, 18233, 27809, 28373, 32533),
        AVM_PARA8(0, 0, 0) },
      { AVM_CDF8(14274, 18230, 22557, 24935, 29980, 30851, 32344),
        AVM_PARA8(0, 0, 0) },
      { AVM_CDF8(27527, 28487, 28723, 28890, 32397, 32647, 32679),
        AVM_PARA8(0, 0, 0) },
    };

static const avm_cdf_prob
    default_spatial_pred_seg_tree_cdf1[SPATIAL_PREDICTION_PROBS][CDF_SIZE(
        8)] = {
      { AVM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672),
        AVM_PARA8(0, 0, 0) },
      { AVM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672),
        AVM_PARA8(0, 0, 0) },
      { AVM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672),
        AVM_PARA8(0, 0, 0) },
    };

static const avm_cdf_prob
    default_seg_id_ext_flag_cdf[SPATIAL_PREDICTION_PROBS][CDF_SIZE(2)] = {
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
      { AVM_CDF2(16384), AVM_PARA2(0, 0, 0) },
    };

static const avm_cdf_prob default_intra_dip_cdf[DIP_CTXS][CDF_SIZE(2)] = {
  { AVM_CDF2(11914), AVM_PARA2(-1, -1, -1) },
  { AVM_CDF2(5128), AVM_PARA2(-1, -1, -1) },
  { AVM_CDF2(3555), AVM_PARA2(-1, -1, -1) },
};

static const avm_cdf_prob default_intra_dip_mode_n6_cdf[CDF_SIZE(6)] = {
  AVM_CDF6(5753, 15408, 19070, 22631, 27578), AVM_PARA6(0, 0, 0)
};

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_COMMON_ENTROPY_INITS_MODES_H_
