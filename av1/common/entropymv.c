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

#include "av1/common/av1_common_int.h"
#include "av1/common/entropymv.h"

static const nmv_context default_nmv_context = {
#if !CONFIG_VQ_MVD_CODING
  { AOM_CDF4(671, 5207, 9061), 75 },  // joints_cdf
#else
#if CONFIG_REDUCE_SYMBOL_SIZE
  { AOM_CDF2(24576), 0 },  // joint_shell_set_cdf
  {
      { AOM_CDF4(6847, 15990, 24873), 0 },
      { AOM_CDF5(8452, 19730, 26138, 30154), 0 },
      { AOM_CDF5(6553, 13106, 19659, 26212), 0 },
      { AOM_CDF6(5062, 12676, 19127, 24565, 29511), 0 },
      { AOM_CDF6(4553, 16572, 24700, 28964, 31428), 0 },
      { AOM_CDF7(2750, 12194, 20615, 25661, 28862, 31157), 0 },
      { AOM_CDF7(7886, 19300, 26400, 29900, 31400, 32100), 0 },
  },  // joint_shell_class_cdf_0
  {
      { AOM_CDF5(17356, 28590, 32415, 32740), 0 },
      { AOM_CDF5(21505, 30000, 31700, 31819), 0 },
      { AOM_CDF6(5461, 10922, 16383, 21844, 27305), 0 },
      { AOM_CDF6(21567, 30194, 32730, 32755, 32760), 0 },
      { AOM_CDF7(20234, 28560, 30530, 31246, 31694, 32141), 0 },
      { AOM_CDF7(18126, 26500, 30750, 32100, 32185, 32400), 0 },
      { AOM_CDF8(16384, 24576, 28672, 29696, 29970, 30244, 30518), 0 },
  },  // joint_shell_class_cdf_1
#else
  {
      { AOM_CDF9(4820, 11253, 17504, 23064, 28204, 31531, 32664, 32760), 30 },
      { AOM_CDF10(7955, 18569, 24600, 28379, 30839, 32105, 32619, 32753, 32760),
        7 },
      { AOM_CDF11(2978, 5956, 8934, 11912, 14890, 17868, 20846, 23824, 26802,
                  29780),
        0 },
      { AOM_CDF12(4710, 11795, 17797, 22857, 27459, 30489, 31939, 32543, 32730,
                  32755, 32760),
        0 },
      { AOM_CDF13(4452, 16202, 24148, 28317, 30726, 32036, 32494, 32680, 32724,
                  32740, 32750, 32760),
        1 },
      { AOM_CDF14(2621, 11620, 19645, 24454, 27504, 29691, 31226, 32079, 32497,
                  32684, 32750, 32754, 32760),
        1 },
      { AOM_CDF15(7771, 19161, 26258, 29752, 31259, 31926, 32289, 32539, 32668,
                  32738, 32752, 32756, 32760, 32764),
        75 },
  },  // joint_shell_class_cdf
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  {
      { AOM_CDF2(3268), 1 },
      { AOM_CDF2(17309), 75 },
  },  // shell_offset_low_class_cdf

#if CONFIG_MVD_CDF_REDUCTION
  { AOM_CDF2(16384), 75 },  //// shell_offset_class2_cdf
#else
  {
      { AOM_CDF2(16384), 75 },
      { AOM_CDF2(16384), 75 },
      { AOM_CDF2(16384), 0 },
  },  // shell_offset_class2_cdf
#endif  // CONFIG_MVD_CDF_REDUCTION
#if !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
  { {
      { AOM_CDF2(16786), 75 },
      { AOM_CDF2(19319), 78 },
      { AOM_CDF2(18504), 93 },
      { AOM_CDF2(18606), 93 },
      { AOM_CDF2(19609), 93 },
      { AOM_CDF2(20222), 93 },
      { AOM_CDF2(20715), 93 },
      { AOM_CDF2(22309), 93 },
      { AOM_CDF2(22194), 93 },
      { AOM_CDF2(23081), 95 },
      { AOM_CDF2(25072), 1 },
      { AOM_CDF2(29343), 50 },
      { AOM_CDF2(16384), 0 },
      { AOM_CDF2(16384), 0 },
  } },  // shell_offset_other_class_cdf
#endif  // !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
  {
      { AOM_CDF2(3371), 78 },
      { AOM_CDF2(5706), 93 },
  },  // col_mv_greater_flags_cdf
  {
      { AOM_CDF2(13012), 75 },
      { AOM_CDF2(13771), 0 },
      { AOM_CDF2(13429), 1 },
      { AOM_CDF2(14771), 1 },
  },  // col_mv_index_cdf
#endif  // !CONFIG_VQ_MVD_CODING

  { AOM_CDF4(4, 19409, 32748), 1 },  // amvd_joints_cdf
  {
      {

#if !CONFIG_VQ_MVD_CODING
          {
              { AOM_CDF9(9045, 14234, 20059, 25670, 29656, 31856, 32661, 32708),
                76 },
              { AOM_CDF10(13873, 20198, 26490, 29945, 31547, 32216, 32659,
                          32704, 32708),
                1 },
              { AOM_CDF11(2979, 5958, 8937, 11916, 14895, 17873, 20852, 23831,
                          26810, 29789),
                0 },
              { AOM_CDF11(13705, 18604, 23447, 27806, 30775, 32116, 32589,
                          32700, 32704, 32708),
                75 },
              { AOM_CDF11(26824, 30545, 31965, 32526, 32676, 32708, 32712,
                          32716, 32720, 32724),
                75 },
              { AOM_CDF11(25936, 28131, 29757, 31161, 32142, 32545, 32698,
                          32702, 32706, 32710),
                75 },
              { AOM_CDF11(32029, 32523, 32665, 32716, 32720, 32724, 32728,
                          32732, 32736, 32740),
                75 },
          },
          { AOM_CDF11(28615, 31027, 32182, 32608, 32712, 32716, 32720, 32724,
                      32728, 32732),
            0 },
#else
          { AOM_CDF8(7804, 11354, 12626, 18581, 24598, 29144, 31608), 1 },
#endif  // !CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
          {
              {
                  { AOM_CDF2(23273), 75 },
                  { AOM_CDF2(21594), 75 },
                  { AOM_CDF2(8749), 75 },
              },
              {
                  { AOM_CDF2(22311), 75 },
                  { AOM_CDF2(11921), 1 },
                  { AOM_CDF2(12406), 1 },
              },
          },
          {
              { AOM_CDF2(18429), 90 },
              { AOM_CDF2(15625), 0 },
              { AOM_CDF2(17117), 75 },
          },
#endif  // !CONFIG_VQ_MVD_CODING
#if !CONFIG_MVD_CDF_REDUCTION
          { AOM_CDF2(16024), 0 },
#endif  //! CONFIG_MVD_CDF_REDUCTION
#if !CONFIG_VQ_MVD_CODING
          { AOM_CDF2(25929), 90 },
          { AOM_CDF2(11557), 84 },
          { AOM_CDF2(26908), 75 },
          {
              { AOM_CDF2(18078), 124 },
              { AOM_CDF2(18254), 124 },
              { AOM_CDF2(20021), 124 },
              { AOM_CDF2(19635), 124 },
              { AOM_CDF2(21095), 123 },
              { AOM_CDF2(22306), 124 },
              { AOM_CDF2(22670), 100 },
              { AOM_CDF2(26291), 5 },
              { AOM_CDF2(30118), 100 },
              { AOM_CDF2(16384), 0 },
          },
#endif  // !CONFIG_VQ_MVD_CODING
      },
      {

#if !CONFIG_VQ_MVD_CODING
          {
              { AOM_CDF9(8910, 13492, 19259, 24751, 28899, 31567, 32600, 32708),
                76 },
              { AOM_CDF10(15552, 21454, 26682, 29649, 31333, 32161, 32591,
                          32704, 32708),
                76 },
              { AOM_CDF11(2979, 5958, 8937, 11916, 14895, 17873, 20852, 23831,
                          26810, 29789),
                0 },
              { AOM_CDF11(12301, 18138, 23549, 27708, 30501, 31883, 32463,
                          32682, 32696, 32700),
                75 },
              { AOM_CDF11(26132, 29614, 31375, 32280, 32639, 32708, 32712,
                          32716, 32720, 32724),
                75 },
              { AOM_CDF11(25359, 28443, 30284, 31515, 32242, 32565, 32693,
                          32700, 32704, 32708),
                75 },
              { AOM_CDF11(31842, 32400, 32592, 32694, 32712, 32716, 32720,
                          32724, 32728, 32732),
                75 },
          },
          { AOM_CDF11(29563, 31499, 32361, 32658, 32712, 32716, 32720, 32724,
                      32728, 32732),
            0 },
#else
          { AOM_CDF8(7392, 11106, 12422, 18167, 24480, 29230, 31714), 1 },
#endif  // !CONFIG_VQ_MVD_CODING
#if !CONFIG_VQ_MVD_CODING
          {
              {
                  { AOM_CDF2(22190), 75 },
                  { AOM_CDF2(19821), 75 },
                  { AOM_CDF2(7239), 75 },
              },
              {
                  { AOM_CDF2(20697), 90 },
                  { AOM_CDF2(12278), 0 },
                  { AOM_CDF2(11913), 1 },
              },
          },
          {
              { AOM_CDF2(14462), 75 },
              { AOM_CDF2(11379), 75 },
              { AOM_CDF2(6857), 0 },
          },
#endif  // !CONFIG_VQ_MVD_CODING
#if !CONFIG_MVD_CDF_REDUCTION
          { AOM_CDF2(16302), 75 },
#endif  //! CONFIG_MVD_CDF_REDUCTION
#if !CONFIG_VQ_MVD_CODING
          { AOM_CDF2(24896), 75 },
          { AOM_CDF2(16355), 119 },
          { AOM_CDF2(26968), 75 },
          {
              { AOM_CDF2(19196), 124 },
              { AOM_CDF2(17877), 124 },
              { AOM_CDF2(19770), 124 },
              { AOM_CDF2(18740), 124 },
              { AOM_CDF2(20175), 124 },
              { AOM_CDF2(21902), 124 },
              { AOM_CDF2(21461), 115 },
              { AOM_CDF2(23432), 77 },
              { AOM_CDF2(29155), 0 },
              { AOM_CDF2(16384), 0 },
          },
#endif  // !CONFIG_VQ_MVD_CODING
      },
  },
};

void av1_init_mv_probs(AV1_COMMON *cm) {
  // NB: this sets CDFs too
  cm->fc->nmvc = default_nmv_context;
  cm->fc->ndvc = default_nmv_context;
}
