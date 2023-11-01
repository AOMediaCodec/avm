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

#if CONFIG_FLEX_MVRES
static const nmv_context default_nmv_context = {
#if CONFIG_ENTROPY_PARA
  { AOM_CDF4(1126, 6354, 9638), 0 },  // joints_cdf
#else
  { AOM_CDF4(1126, 6354, 9638) },  // joints_cdf
#endif  // CONFIG_ENTROPY_PARA
#if CONFIG_ADAPTIVE_MVD
#if CONFIG_ENTROPY_PARA
  { AOM_CDF4(4, 18825, 32748), 1 },  // amvd_joints_cdf
#else
  { AOM_CDF4(4, 18825, 32748) },  // amvd_joints_cdf
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_ADAPTIVE_MVD
  {
      {
#if CONFIG_ENTROPY_PARA
          {
              { AOM_CDF9(21158, 25976, 29130, 31210, 32237, 32636, 32712,
                         32716),
                1 },
              { AOM_CDF10(20546, 25501, 29187, 31196, 32175, 32597, 32708,
                          32712, 32716),
                1 },
              { AOM_CDF11(2979, 5958, 8937, 11916, 14895, 17873, 20852, 23831,
                          26810, 29789),
                0 },
              { AOM_CDF11(22643, 27104, 29724, 31229, 32115, 32523, 32692,
                          32700, 32704, 32708),
                0 },
              { AOM_CDF11(26781, 29925, 31300, 32056, 32465, 32650, 32704,
                          32708, 32712, 32716),
                75 },
              { AOM_CDF11(26807, 30081, 31455, 32131, 32503, 32658, 32704,
                          32708, 32712, 32716),
                0 },
              { AOM_CDF11(30184, 31733, 32301, 32550, 32685, 32708, 32712,
                          32716, 32720, 32724),
                75 },
          },
#else
          // Vertical component
          { { AOM_CDF9(21158, 25976, 29130, 31210, 32237, 32636, 32712,
                       32716) },
            { AOM_CDF10(20546, 25501, 29187, 31196, 32175, 32597, 32708, 32712,
                        32716) },
            { AOM_CDF11(2979, 5958, 8937, 11916, 14895, 17873, 20852, 23831,
                        26810, 29789) },
            { AOM_CDF11(22643, 27104, 29724, 31229, 32115, 32523, 32692, 32700,
                        32704, 32708) },
            { AOM_CDF11(26781, 29925, 31300, 32056, 32465, 32650, 32704, 32708,
                        32712, 32716) },
            { AOM_CDF11(26807, 30081, 31455, 32131, 32503, 32658, 32704, 32708,
                        32712, 32716) },
            { AOM_CDF11(30184, 31733, 32301, 32550, 32685, 32708, 32712, 32716,
                        32720, 32724) } },  // class_cdf // fp
#endif  // CONFIG_ENTROPY_PARA

#if CONFIG_ADAPTIVE_MVD
#if CONFIG_ENTROPY_PARA
          { AOM_CDF11(29390, 31689, 32431, 32665, 32712, 32716, 32720, 32724,
                      32728, 32732),
            0 },
#else
          { AOM_CDF11(29390, 31689, 32431, 32665, 32712, 32716, 32720, 32724,
                      32728, 32732) },  // class_cdf // fp
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_ENTROPY_PARA
          {
              {
                  { AOM_CDF2(23476), 0 },
                  { AOM_CDF2(22382), 0 },
                  { AOM_CDF2(10351), 0 },
              },
              {
                  { AOM_CDF2(21865), 0 },
                  { AOM_CDF2(16937), 0 },
                  { AOM_CDF2(13425), 0 },
              },
          },
          {
              { AOM_CDF2(16528), 75 },
              { AOM_CDF2(11848), 0 },
              { AOM_CDF2(7635), 0 },
          },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(4654), 0 },
          { AOM_CDF2(12899), 24 },
          { AOM_CDF2(26486), 0 },
          {
              { AOM_CDF2(20370), 118 },
              { AOM_CDF2(19352), 119 },
              { AOM_CDF2(20184), 123 },
              { AOM_CDF2(19290), 118 },
              { AOM_CDF2(20751), 90 },
              { AOM_CDF2(23123), 118 },
              { AOM_CDF2(25179), 75 },
              { AOM_CDF2(27939), 6 },
              { AOM_CDF2(31466), 35 },
              { AOM_CDF2(16384), 50 },
          },
      },
#else
          { { { AOM_CDF2(23476) }, { AOM_CDF2(22382) }, { AOM_CDF2(10351) } },
            { { AOM_CDF2(21865) },
              { AOM_CDF2(16937) },
              { AOM_CDF2(13425) } } },  // class0_fp_cdf
          { { AOM_CDF2(16528) },
            { AOM_CDF2(11848) },
            { AOM_CDF2(7635) } },  // fp_cdf

          // CONFIG_FLEX_MVRES
          { AOM_CDF2(128 * 128) },  // sign_cdf
          { AOM_CDF2(4654) },       // class0_hp_cdf
          { AOM_CDF2(12899) },      // hp_cdf
          { AOM_CDF2(26486) },      // class0_cdf
          { { AOM_CDF2(20370) },
            { AOM_CDF2(19352) },
            { AOM_CDF2(20184) },
            { AOM_CDF2(19290) },
            { AOM_CDF2(20751) },
            { AOM_CDF2(23123) },
            { AOM_CDF2(25179) },
            { AOM_CDF2(27939) },
            { AOM_CDF2(31466) },
            { AOM_CDF2(16384) } },  // bits_cdf
      },
#endif  // CONFIG_ENTROPY_PARA
      {
#if CONFIG_ENTROPY_PARA
          {
              { AOM_CDF9(19297, 23907, 27450, 30145, 31606, 32456, 32712,
                         32716),
                1 },
              { AOM_CDF10(18861, 23816, 27819, 30238, 31643, 32355, 32697,
                          32704, 32708),
                1 },
              { AOM_CDF11(2979, 5958, 8937, 11916, 14895, 17873, 20852, 23831,
                          26810, 29789),
                0 },
              { AOM_CDF11(20444, 25375, 28587, 30567, 31750, 32345, 32628,
                          32700, 32704, 32708),
                75 },
              { AOM_CDF11(25106, 29051, 30835, 31758, 32302, 32574, 32703,
                          32707, 32711, 32715),
                75 },
              { AOM_CDF11(24435, 28901, 30875, 31825, 32348, 32583, 32702,
                          32706, 32710, 32714),
                75 },
              { AOM_CDF11(29338, 31380, 32155, 32475, 32654, 32708, 32712,
                          32716, 32720, 32724),
                75 },
          },
#else
          // Horizontal component
          { { AOM_CDF9(19297, 23907, 27450, 30145, 31606, 32456, 32712,
                       32716) },  // class_cdf
            { AOM_CDF10(18861, 23816, 27819, 30238, 31643, 32355, 32697, 32704,
                        32708) },  // class_cdf
            { AOM_CDF11(2979, 5958, 8937, 11916, 14895, 17873, 20852, 23831,
                        26810, 29789) },
            { AOM_CDF11(20444, 25375, 28587, 30567, 31750, 32345, 32628, 32700,
                        32704, 32708) },
            { AOM_CDF11(25106, 29051, 30835, 31758, 32302, 32574, 32703, 32707,
                        32711, 32715) },
            { AOM_CDF11(24435, 28901, 30875, 31825, 32348, 32583, 32702, 32706,
                        32710, 32714) },
            { AOM_CDF11(29338, 31380, 32155, 32475, 32654, 32708, 32712, 32716,
                        32720, 32724) } },
#endif  // CONFIG_ENTROPY_PARA
#if CONFIG_ADAPTIVE_MVD
#if CONFIG_ENTROPY_PARA
          { AOM_CDF11(28341, 31295, 32320, 32640, 32712, 32716, 32720, 32724,
                      32728, 32732),
            1 },
#else
          { AOM_CDF11(28341, 31295, 32320, 32640, 32712, 32716, 32720, 32724,
                      32728, 32732) },  // class_cdf // fp
#endif  // CONFIG_ENTROPY_PARA
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_ENTROPY_PARA
          {
              {
                  { AOM_CDF2(21083), 0 },
                  { AOM_CDF2(21153), 0 },
                  { AOM_CDF2(7888), 0 },
              },
              {
                  { AOM_CDF2(22423), 75 },
                  { AOM_CDF2(16285), 0 },
                  { AOM_CDF2(14031), 0 },
              },
          },
          {
              { AOM_CDF2(16600), 0 },
              { AOM_CDF2(12569), 0 },
              { AOM_CDF2(8367), 0 },
          },
          { AOM_CDF2(16384), 0 },
          { AOM_CDF2(3238), 0 },
          { AOM_CDF2(15376), 17 },
          { AOM_CDF2(24569), 0 },
          {
              { AOM_CDF2(20048), 118 },
              { AOM_CDF2(19425), 124 },
              { AOM_CDF2(19816), 124 },
              { AOM_CDF2(19138), 124 },
              { AOM_CDF2(20583), 123 },
              { AOM_CDF2(23446), 118 },
              { AOM_CDF2(23440), 90 },
              { AOM_CDF2(26025), 90 },
              { AOM_CDF2(29968), 12 },
              { AOM_CDF2(16384), 50 },
          },
      },
  },
#else
          { { { AOM_CDF2(21083) }, { AOM_CDF2(21153) }, { AOM_CDF2(7888) } },
            { { AOM_CDF2(22423) },
              { AOM_CDF2(16285) },
              { AOM_CDF2(14031) } } },  // class0_fp_cdf
          { { AOM_CDF2(16600) },
            { AOM_CDF2(12569) },
            { AOM_CDF2(8367) } },  // fp_cdf

          { AOM_CDF2(128 * 128) },  // sign_cdf
          { AOM_CDF2(3238) },       // class0_hp_cdf
          { AOM_CDF2(15376) },      // hp_cdf
          { AOM_CDF2(24569) },      // class0_cdf
          { { AOM_CDF2(20048) },
            { AOM_CDF2(19425) },
            { AOM_CDF2(19816) },
            { AOM_CDF2(19138) },
            { AOM_CDF2(20583) },
            { AOM_CDF2(23446) },
            { AOM_CDF2(23440) },
            { AOM_CDF2(26025) },
            { AOM_CDF2(29968) },
            { AOM_CDF2(16384) } },  // bits_cdf
      } },
#endif  // CONFIG_ENTROPY_PARA
};
#else
static const nmv_context default_nmv_context = {
  { AOM_CDF4(4096, 11264, 19328) },  // joints_cdf
#if CONFIG_ADAPTIVE_MVD
  { AOM_CDF4(1024, 19328, 32740) },  // amvd_joints_cdf
#endif  // CONFIG_ADAPTIVE_MVD
  { {
// Vertical component
#if CONFIG_FLEX_MVRES
        { { AOM_CDF9(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757) },
          { AOM_CDF10(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                      32762) },
          { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                      32762, 32767) },
          { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                      32762, 32767) },
          { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                      32762, 32767) },
          { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                      32762, 32767) },
          { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                      32762, 32767) } },  // class_cdf // fp
#else
        { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                    32762, 32767) },  // class_cdf // fp
#endif
#if CONFIG_ADAPTIVE_MVD
        { AOM_CDF11(24672, 27976, 29858, 31320, 32758, 32759, 32760, 32762,
                    32764, 32767) },  // class_cdf // fp
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_FLEX_MVRES
        { { { AOM_CDF2(24576) }, { AOM_CDF2(21845) }, { AOM_CDF2(8192) } },
          { { AOM_CDF2(21248) },
            { AOM_CDF2(18950) },
            { AOM_CDF2(8192) } } },  // class0_fp_cdf
        { { AOM_CDF2(17408) },
          { AOM_CDF2(15420) },
          { AOM_CDF2(8192) } },   // fp_cdf
#else
        { { AOM_CDF4(16384, 24576, 26624) },
          { AOM_CDF4(12288, 21248, 24128) } },  // class0_fp_cdf
        { AOM_CDF4(8192, 17408, 21248) },       // fp_cdf
#endif  // CONFIG_FLEX_MVRES
        { AOM_CDF2(128 * 128) },  // sign_cdf
        { AOM_CDF2(160 * 128) },  // class0_hp_cdf
        { AOM_CDF2(128 * 128) },  // hp_cdf
        { AOM_CDF2(216 * 128) },  // class0_cdf
        { { AOM_CDF2(128 * 136) },
          { AOM_CDF2(128 * 140) },
          { AOM_CDF2(128 * 148) },
          { AOM_CDF2(128 * 160) },
          { AOM_CDF2(128 * 176) },
          { AOM_CDF2(128 * 192) },
          { AOM_CDF2(128 * 224) },
          { AOM_CDF2(128 * 234) },
          { AOM_CDF2(128 * 234) },
          { AOM_CDF2(128 * 240) } },  // bits_cdf
    },
    {
#if CONFIG_FLEX_MVRES
        // Horizontal component
        {
            { AOM_CDF9(28672, 30976, 31858, 32320, 32551, 32656, 32740,
                       32757) },  // class_cdf
            { AOM_CDF10(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                        32762) },  // class_cdf
            { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                        32762, 32767) },  // class_cdf
            { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                        32762, 32767) },  // class_cdf
            { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                        32762, 32767) },  // class_cdf
            { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                        32762, 32767) },  // class_cdf
            { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                        32762, 32767) },  // class_cdf
        },
#else
        // Horizontal component
        { AOM_CDF11(28672, 30976, 31858, 32320, 32551, 32656, 32740, 32757,
                    32762, 32767) },  // class_cdf // fp
#endif
#if CONFIG_ADAPTIVE_MVD
        { AOM_CDF11(24672, 27976, 29858, 31320, 32758, 32759, 32760, 32762,
                    32764, 32767) },  // class_cdf // fp
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_FLEX_MVRES
        { { { AOM_CDF2(24576) }, { AOM_CDF2(21845) }, { AOM_CDF2(8192) } },
          { { AOM_CDF2(21248) },
            { AOM_CDF2(18950) },
            { AOM_CDF2(8192) } } },  // class0_fp_cdf
        { { AOM_CDF2(17408) },
          { AOM_CDF2(15420) },
          { AOM_CDF2(8192) } },   // fp_cdf
#else
        { { AOM_CDF4(16384, 24576, 26624) },
          { AOM_CDF4(12288, 21248, 24128) } },  // class0_fp_cdf
        { AOM_CDF4(8192, 17408, 21248) },       // fp_cdf
#endif  // CONFIG_FLEX_MVRES
        { AOM_CDF2(128 * 128) },  // sign_cdf
        { AOM_CDF2(160 * 128) },  // class0_hp_cdf
        { AOM_CDF2(128 * 128) },  // hp_cdf
        { AOM_CDF2(216 * 128) },  // class0_cdf
        { { AOM_CDF2(128 * 136) },
          { AOM_CDF2(128 * 140) },
          { AOM_CDF2(128 * 148) },
          { AOM_CDF2(128 * 160) },
          { AOM_CDF2(128 * 176) },
          { AOM_CDF2(128 * 192) },
          { AOM_CDF2(128 * 224) },
          { AOM_CDF2(128 * 234) },
          { AOM_CDF2(128 * 234) },
          { AOM_CDF2(128 * 240) } },  // bits_cdf
    } },
};
#endif
void av1_init_mv_probs(AV1_COMMON *cm) {
  // NB: this sets CDFs too
  cm->fc->nmvc = default_nmv_context;
  cm->fc->ndvc = default_nmv_context;
}
