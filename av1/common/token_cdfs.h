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

#ifndef AOM_AV1_COMMON_TOKEN_CDFS_H_
#define AOM_AV1_COMMON_TOKEN_CDFS_H_

#include "config/aom_config.h"

#include "av1/common/entropy.h"

#if CONFIG_CONTEXT_DERIVATION
#if !CONFIG_BY_PASS_V_SIGN
static const aom_cdf_prob
    av1_default_v_dc_sign_cdfs[TOKEN_CDF_Q_CTXS][CROSS_COMPONENT_CONTEXTS]
                              [DC_SIGN_CONTEXTS][CDF_SIZE(2)] = {
                                {
                                    {
                                        { AOM_CDF2(15826), 76 },
                                        { AOM_CDF2(13107), 75 },
                                        { AOM_CDF2(17009), 0 },
                                    },
                                    {
                                        { AOM_CDF2(13983), 76 },
                                        { AOM_CDF2(12535), 75 },
                                        { AOM_CDF2(15103), 75 },
                                    },
                                    {
                                        { AOM_CDF2(19211), 81 },
                                        { AOM_CDF2(16784), 105 },
                                        { AOM_CDF2(20053), 6 },
                                    },
                                },
                                {
                                    {
                                        { AOM_CDF2(16745), 1 },
                                        { AOM_CDF2(9669), 75 },
                                        { AOM_CDF2(19809), 75 },
                                    },
                                    {
                                        { AOM_CDF2(12786), 1 },
                                        { AOM_CDF2(12319), 1 },
                                        { AOM_CDF2(17262), 75 },
                                    },
                                    {
                                        { AOM_CDF2(21304), 6 },
                                        { AOM_CDF2(15352), 0 },
                                        { AOM_CDF2(21947), 0 },
                                    },
                                },
                                {
                                    {
                                        { AOM_CDF2(17599), 6 },
                                        { AOM_CDF2(9423), 76 },
                                        { AOM_CDF2(23792), 76 },
                                    },
                                    {
                                        { AOM_CDF2(8460), 6 },
                                        { AOM_CDF2(5237), 1 },
                                        { AOM_CDF2(14155), 1 },
                                    },
                                    {
                                        { AOM_CDF2(25149), 6 },
                                        { AOM_CDF2(18855), 1 },
                                        { AOM_CDF2(28051), 1 },
                                    },
                                },
                                {
                                    {
                                        { AOM_CDF2(17862), 7 },
                                        { AOM_CDF2(10009), 1 },
                                        { AOM_CDF2(23660), 76 },
                                    },
                                    {
                                        { AOM_CDF2(7038), 32 },
                                        { AOM_CDF2(3021), 1 },
                                        { AOM_CDF2(11487), 7 },
                                    },
                                    {
                                        { AOM_CDF2(27384), 32 },
                                        { AOM_CDF2(24008), 1 },
                                        { AOM_CDF2(28690), 7 },
                                    },
                                },
                              };
#endif  // !CONFIG_BY_PASS_V_SIGN
#if !CONFIG_CTX_V_AC_SIGN
static const aom_cdf_prob
    av1_default_v_ac_sign_cdfs[TOKEN_CDF_Q_CTXS][CROSS_COMPONENT_CONTEXTS]
                              [CDF_SIZE(2)] = {
                                {
                                    { AOM_CDF2(16396), 124 },
                                    { AOM_CDF2(12826), 7 },
                                    { AOM_CDF2(19847), 37 },
                                },
                                {
                                    { AOM_CDF2(16264), 124 },
                                    { AOM_CDF2(16264), 75 },
                                    { AOM_CDF2(16743), 75 },
                                },
                                {
                                    { AOM_CDF2(16106), 124 },
                                    { AOM_CDF2(9850), 6 },
                                    { AOM_CDF2(22827), 1 },
                                },
                                {
                                    { AOM_CDF2(16944), 123 },
                                    { AOM_CDF2(3045), 32 },
                                    { AOM_CDF2(28761), 37 },
                                },
                              };
#endif  // !CONFIG_CTX_V_AC_SIGN
#endif  // CONFIG_CONTEXT_DERIVATION

static const aom_cdf_prob
    av1_default_idtx_sign_cdfs[TOKEN_CDF_Q_CTXS][TX_SIZES][IDTX_SIGN_CONTEXTS]
                              [CDF_SIZE(2)] = {
                                {
                                    {
                                        { AOM_CDF2(15400), 103 },
                                        { AOM_CDF2(25752), 103 },
                                        { AOM_CDF2(6411), 103 },
                                        { AOM_CDF2(29907), 0 },
                                        { AOM_CDF2(1735), 80 },
                                        { AOM_CDF2(29773), 94 },
                                        { AOM_CDF2(3878), 75 },
                                        { AOM_CDF2(32660), 1 },
                                        { AOM_CDF2(180), 91 },
                                    },
                                    {
                                        { AOM_CDF2(15290), 0 },
                                        { AOM_CDF2(28210), 90 },
                                        { AOM_CDF2(3662), 90 },
                                        { AOM_CDF2(30884), 0 },
                                        { AOM_CDF2(626), 78 },
                                        { AOM_CDF2(30964), 76 },
                                        { AOM_CDF2(1522), 75 },
                                        { AOM_CDF2(32648), 75 },
                                        { AOM_CDF2(56), 120 },
                                    },
                                    {
                                        { AOM_CDF2(10694), 31 },
                                        { AOM_CDF2(29934), 81 },
                                        { AOM_CDF2(1212), 76 },
                                        { AOM_CDF2(32053), 15 },
                                        { AOM_CDF2(97), 90 },
                                        { AOM_CDF2(32356), 100 },
                                        { AOM_CDF2(136), 78 },
                                        { AOM_CDF2(32751), 106 },
                                        { AOM_CDF2(8), 108 },
                                    },
                                    {
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                    },
                                    {
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                    },
                                },
                                {
                                    {
                                        { AOM_CDF2(18210), 78 },
                                        { AOM_CDF2(25918), 93 },
                                        { AOM_CDF2(9621), 93 },
                                        { AOM_CDF2(29491), 0 },
                                        { AOM_CDF2(9368), 31 },
                                        { AOM_CDF2(31030), 90 },
                                        { AOM_CDF2(3815), 103 },
                                        { AOM_CDF2(32439), 100 },
                                        { AOM_CDF2(2455), 0 },
                                    },
                                    {
                                        { AOM_CDF2(18967), 0 },
                                        { AOM_CDF2(27730), 75 },
                                        { AOM_CDF2(7456), 75 },
                                        { AOM_CDF2(30015), 1 },
                                        { AOM_CDF2(2882), 6 },
                                        { AOM_CDF2(31783), 90 },
                                        { AOM_CDF2(2087), 75 },
                                        { AOM_CDF2(32543), 91 },
                                        { AOM_CDF2(669), 75 },
                                    },
                                    {
                                        { AOM_CDF2(18273), 7 },
                                        { AOM_CDF2(27763), 6 },
                                        { AOM_CDF2(6274), 6 },
                                        { AOM_CDF2(29647), 32 },
                                        { AOM_CDF2(1854), 26 },
                                        { AOM_CDF2(32079), 76 },
                                        { AOM_CDF2(1083), 0 },
                                        { AOM_CDF2(32487), 11 },
                                        { AOM_CDF2(410), 83 },
                                    },
                                    {
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                    },
                                    {
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                    },
                                },
                                {
                                    {
                                        { AOM_CDF2(18453), 90 },
                                        { AOM_CDF2(25590), 75 },
                                        { AOM_CDF2(10262), 90 },
                                        { AOM_CDF2(25168), 25 },
                                        { AOM_CDF2(13246), 1 },
                                        { AOM_CDF2(31015), 78 },
                                        { AOM_CDF2(4514), 9 },
                                        { AOM_CDF2(31187), 49 },
                                        { AOM_CDF2(5011), 49 },
                                    },
                                    {
                                        { AOM_CDF2(19200), 78 },
                                        { AOM_CDF2(27721), 78 },
                                        { AOM_CDF2(7982), 78 },
                                        { AOM_CDF2(26609), 0 },
                                        { AOM_CDF2(8128), 6 },
                                        { AOM_CDF2(31826), 90 },
                                        { AOM_CDF2(2334), 75 },
                                        { AOM_CDF2(31361), 45 },
                                        { AOM_CDF2(2321), 6 },
                                    },
                                    {
                                        { AOM_CDF2(19567), 0 },
                                        { AOM_CDF2(28437), 0 },
                                        { AOM_CDF2(6801), 3 },
                                        { AOM_CDF2(25757), 32 },
                                        { AOM_CDF2(10038), 7 },
                                        { AOM_CDF2(31927), 90 },
                                        { AOM_CDF2(2095), 90 },
                                        { AOM_CDF2(31761), 31 },
                                        { AOM_CDF2(2139), 6 },
                                    },
                                    {
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                    },
                                    {
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                    },
                                },
                                {
                                    {
                                        { AOM_CDF2(18508), 7 },
                                        { AOM_CDF2(28260), 3 },
                                        { AOM_CDF2(6366), 90 },
                                        { AOM_CDF2(16384), 0 },
                                        { AOM_CDF2(16384), 0 },
                                        { AOM_CDF2(32440), 50 },
                                        { AOM_CDF2(382), 50 },
                                        { AOM_CDF2(16384), 0 },
                                        { AOM_CDF2(16384), 0 },
                                    },
                                    {
                                        { AOM_CDF2(19940), 2 },
                                        { AOM_CDF2(30534), 75 },
                                        { AOM_CDF2(4007), 0 },
                                        { AOM_CDF2(16384), 100 },
                                        { AOM_CDF2(16384), 50 },
                                        { AOM_CDF2(32654), 23 },
                                        { AOM_CDF2(219), 24 },
                                        { AOM_CDF2(16384), 0 },
                                        { AOM_CDF2(16384), 0 },
                                    },
                                    {
                                        { AOM_CDF2(24675), 7 },
                                        { AOM_CDF2(32391), 75 },
                                        { AOM_CDF2(1491), 76 },
                                        { AOM_CDF2(16384), 50 },
                                        { AOM_CDF2(16384), 0 },
                                        { AOM_CDF2(32758), 34 },
                                        { AOM_CDF2(105), 25 },
                                        { AOM_CDF2(16384), 0 },
                                        { AOM_CDF2(16384), 0 },
                                    },
                                    {
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                    },
                                    {
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                        { AOM_CDF2(16384), 0 },  // unused
                                    },
                                },
                              };

static const aom_cdf_prob
    av1_default_dc_sign_cdfs[TOKEN_CDF_Q_CTXS][PLANE_TYPES][DC_SIGN_GROUPS]
                            [DC_SIGN_CONTEXTS][CDF_SIZE(2)] = {
                              {
                                  {
                                      {
                                          { AOM_CDF2(15453), 1 },
                                          { AOM_CDF2(10850), 0 },
                                          { AOM_CDF2(19548), 1 },
                                      },
                                      {
                                          { AOM_CDF2(16325), 75 },
                                          { AOM_CDF2(14962), 75 },
                                          { AOM_CDF2(18929), 75 },
                                      },
                                  },
                                  {
                                      {
                                          { AOM_CDF2(15906), 1 },
                                          { AOM_CDF2(12486), 75 },
                                          { AOM_CDF2(18937), 75 },
                                      },
                                      {
                                          { AOM_CDF2(16384), 0 },
                                          { AOM_CDF2(16384), 0 },
                                          { AOM_CDF2(16384), 0 },
                                      },
                                  },
                              },
                              {
                                  {
                                      {
                                          { AOM_CDF2(16726), 75 },
                                          { AOM_CDF2(11701), 75 },
                                          { AOM_CDF2(19730), 0 },
                                      },
                                      {
                                          { AOM_CDF2(17090), 93 },
                                          { AOM_CDF2(15487), 78 },
                                          { AOM_CDF2(19186), 93 },
                                      },
                                  },
                                  {
                                      {
                                          { AOM_CDF2(16536), 90 },
                                          { AOM_CDF2(13618), 75 },
                                          { AOM_CDF2(19285), 75 },
                                      },
                                      {
                                          { AOM_CDF2(16384), 0 },
                                          { AOM_CDF2(16384), 0 },
                                          { AOM_CDF2(16384), 0 },
                                      },
                                  },
                              },
                              {
                                  {
                                      {
                                          { AOM_CDF2(17166), 75 },
                                          { AOM_CDF2(11323), 75 },
                                          { AOM_CDF2(20062), 0 },
                                      },
                                      {
                                          { AOM_CDF2(17264), 93 },
                                          { AOM_CDF2(15960), 90 },
                                          { AOM_CDF2(19781), 93 },
                                      },
                                  },
                                  {
                                      {
                                          { AOM_CDF2(16417), 75 },
                                          { AOM_CDF2(12508), 75 },
                                          { AOM_CDF2(20374), 75 },
                                      },
                                      {
                                          { AOM_CDF2(16384), 0 },
                                          { AOM_CDF2(16384), 0 },
                                          { AOM_CDF2(16384), 0 },
                                      },
                                  },
                              },
                              {
                                  {
                                      {
                                          { AOM_CDF2(18369), 0 },
                                          { AOM_CDF2(12253), 1 },
                                          { AOM_CDF2(19620), 0 },
                                      },
                                      {
                                          { AOM_CDF2(19048), 75 },
                                          { AOM_CDF2(17951), 90 },
                                          { AOM_CDF2(19796), 90 },
                                      },
                                  },
                                  {
                                      {
                                          { AOM_CDF2(15662), 6 },
                                          { AOM_CDF2(10440), 1 },
                                          { AOM_CDF2(22078), 1 },
                                      },
                                      {
                                          { AOM_CDF2(16384), 0 },
                                          { AOM_CDF2(16384), 0 },
                                          { AOM_CDF2(16384), 0 },
                                      },
                                  },
                              },
                            };

static const aom_cdf_prob av1_default_txb_skip_cdfs
    [TOKEN_CDF_Q_CTXS][2][TX_SIZES][TXB_SKIP_CONTEXTS][CDF_SIZE(2)] = {
      {
          {
              {
                  { AOM_CDF2(30676), 5 }, { AOM_CDF2(1817), 118 },
                  { AOM_CDF2(5257), 5 },  { AOM_CDF2(11824), 31 },
                  { AOM_CDF2(18862), 1 }, { AOM_CDF2(31436), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(9076), 1 },
                  { AOM_CDF2(15764), 0 }, { AOM_CDF2(24131), 5 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(31680), 26 }, { AOM_CDF2(2826), 23 },
                  { AOM_CDF2(7094), 1 },   { AOM_CDF2(15183), 31 },
                  { AOM_CDF2(20827), 1 },  { AOM_CDF2(31633), 30 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(7664), 75 },
                  { AOM_CDF2(15595), 1 },  { AOM_CDF2(24849), 1 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(28327), 57 }, { AOM_CDF2(4727), 1 },
                  { AOM_CDF2(9135), 7 },   { AOM_CDF2(17069), 6 },
                  { AOM_CDF2(19658), 79 }, { AOM_CDF2(30914), 80 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(3695), 0 },
                  { AOM_CDF2(13683), 26 }, { AOM_CDF2(25695), 31 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(15188), 62 }, { AOM_CDF2(1638), 39 },
                  { AOM_CDF2(6781), 20 },  { AOM_CDF2(10684), 30 },
                  { AOM_CDF2(12336), 0 },  { AOM_CDF2(28734), 50 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(5301), 31 },
                  { AOM_CDF2(21879), 62 }, { AOM_CDF2(28445), 44 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(18113), 62 }, { AOM_CDF2(1731), 50 },
                  { AOM_CDF2(20535), 0 },  { AOM_CDF2(29110), 50 },
                  { AOM_CDF2(32670), 0 },  { AOM_CDF2(32577), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
          },
          {
              {
                  { AOM_CDF2(32734), 123 }, { AOM_CDF2(14456), 75 },
                  { AOM_CDF2(12935), 4 },   { AOM_CDF2(24660), 98 },
                  { AOM_CDF2(27344), 38 },  { AOM_CDF2(32086), 118 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(2715), 1 },
                  { AOM_CDF2(13236), 1 },   { AOM_CDF2(25338), 1 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(29599), 80 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32663), 6 },   { AOM_CDF2(14570), 123 },
                  { AOM_CDF2(17992), 118 }, { AOM_CDF2(22899), 93 },
                  { AOM_CDF2(27330), 100 }, { AOM_CDF2(31876), 123 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(3290), 0 },
                  { AOM_CDF2(12288), 1 },   { AOM_CDF2(23325), 1 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(30901), 75 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32333), 31 },  { AOM_CDF2(18623), 78 },
                  { AOM_CDF2(19890), 93 },  { AOM_CDF2(23535), 21 },
                  { AOM_CDF2(27371), 121 }, { AOM_CDF2(31683), 98 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(5302), 0 },
                  { AOM_CDF2(19163), 2 },   { AOM_CDF2(28093), 75 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(25920), 31 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(28335), 62 }, { AOM_CDF2(19661), 80 },
                  { AOM_CDF2(26985), 50 }, { AOM_CDF2(25486), 87 },
                  { AOM_CDF2(30794), 25 }, { AOM_CDF2(29588), 10 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(12429), 50 },
                  { AOM_CDF2(26455), 50 }, { AOM_CDF2(29789), 50 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(19150), 55 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(20735), 60 }, { AOM_CDF2(16384), 50 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF2(28488), 0 },  { AOM_CDF2(1447), 78 },
                  { AOM_CDF2(3421), 76 },  { AOM_CDF2(8913), 31 },
                  { AOM_CDF2(15539), 31 }, { AOM_CDF2(28811), 8 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(3005), 0 },
                  { AOM_CDF2(14073), 0 },  { AOM_CDF2(25450), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(30864), 0 },  { AOM_CDF2(1270), 93 },
                  { AOM_CDF2(2930), 75 },  { AOM_CDF2(9276), 31 },
                  { AOM_CDF2(15981), 31 }, { AOM_CDF2(29037), 5 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(3943), 0 },
                  { AOM_CDF2(11814), 6 },  { AOM_CDF2(23331), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(30925), 0 },  { AOM_CDF2(2053), 75 },
                  { AOM_CDF2(5522), 6 },   { AOM_CDF2(14326), 32 },
                  { AOM_CDF2(19221), 6 },  { AOM_CDF2(30087), 1 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(4514), 1 },
                  { AOM_CDF2(12162), 31 }, { AOM_CDF2(21266), 6 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(27190), 37 }, { AOM_CDF2(1099), 75 },
                  { AOM_CDF2(5241), 4 },   { AOM_CDF2(11687), 32 },
                  { AOM_CDF2(16933), 40 }, { AOM_CDF2(27101), 32 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(5062), 31 },
                  { AOM_CDF2(14756), 32 }, { AOM_CDF2(22099), 31 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(26974), 62 },  { AOM_CDF2(2361), 29 },
                  { AOM_CDF2(7694), 30 },   { AOM_CDF2(20908), 62 },
                  { AOM_CDF2(21996), 120 }, { AOM_CDF2(27307), 62 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
          },
          {
              {
                  { AOM_CDF2(32743), 118 }, { AOM_CDF2(12017), 116 },
                  { AOM_CDF2(14431), 6 },   { AOM_CDF2(18541), 5 },
                  { AOM_CDF2(23529), 5 },   { AOM_CDF2(28333), 8 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(1586), 1 },
                  { AOM_CDF2(11933), 1 },   { AOM_CDF2(22576), 90 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(32072), 3 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32690), 118 }, { AOM_CDF2(13594), 124 },
                  { AOM_CDF2(14658), 120 }, { AOM_CDF2(18152), 90 },
                  { AOM_CDF2(23557), 75 },  { AOM_CDF2(29321), 118 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(1056), 0 },
                  { AOM_CDF2(10778), 1 },   { AOM_CDF2(22345), 1 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(32151), 75 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32578), 75 },  { AOM_CDF2(13948), 124 },
                  { AOM_CDF2(17649), 116 }, { AOM_CDF2(18661), 76 },
                  { AOM_CDF2(26084), 75 },  { AOM_CDF2(30559), 90 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(1503), 1 },
                  { AOM_CDF2(13773), 6 },   { AOM_CDF2(25790), 1 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(30145), 2 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32018), 31 }, { AOM_CDF2(14713), 84 },
                  { AOM_CDF2(16085), 7 },  { AOM_CDF2(20831), 7 },
                  { AOM_CDF2(24132), 16 }, { AOM_CDF2(30161), 1 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(3201), 31 },
                  { AOM_CDF2(19212), 6 },  { AOM_CDF2(27653), 6 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(21398), 62 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(29922), 32 }, { AOM_CDF2(6502), 35 },
                  { AOM_CDF2(21194), 20 }, { AOM_CDF2(25437), 46 },
                  { AOM_CDF2(30629), 94 }, { AOM_CDF2(31813), 124 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF2(26149), 78 }, { AOM_CDF2(2177), 75 },
                  { AOM_CDF2(3177), 75 },  { AOM_CDF2(7716), 1 },
                  { AOM_CDF2(11613), 1 },  { AOM_CDF2(23660), 5 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(1018), 75 },
                  { AOM_CDF2(12076), 75 }, { AOM_CDF2(19285), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(27839), 75 }, { AOM_CDF2(1454), 118 },
                  { AOM_CDF2(1988), 75 },  { AOM_CDF2(6252), 1 },
                  { AOM_CDF2(10044), 6 },  { AOM_CDF2(22153), 6 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(2172), 75 },
                  { AOM_CDF2(11905), 0 },  { AOM_CDF2(20438), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(30478), 0 }, { AOM_CDF2(1746), 90 },
                  { AOM_CDF2(2692), 76 }, { AOM_CDF2(9750), 6 },
                  { AOM_CDF2(13018), 6 }, { AOM_CDF2(26848), 5 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(2970), 75 },
                  { AOM_CDF2(10430), 1 }, { AOM_CDF2(19344), 1 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(31989), 31 }, { AOM_CDF2(1131), 75 },
                  { AOM_CDF2(2226), 7 },   { AOM_CDF2(8985), 32 },
                  { AOM_CDF2(10332), 6 },  { AOM_CDF2(21625), 35 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(3945), 1 },
                  { AOM_CDF2(11479), 7 },  { AOM_CDF2(19011), 6 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32001), 31 }, { AOM_CDF2(5203), 31 },
                  { AOM_CDF2(13488), 26 }, { AOM_CDF2(24035), 32 },
                  { AOM_CDF2(25718), 17 }, { AOM_CDF2(30402), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
          },
          {
              {
                  { AOM_CDF2(32758), 103 }, { AOM_CDF2(9039), 112 },
                  { AOM_CDF2(11611), 5 },   { AOM_CDF2(13405), 15 },
                  { AOM_CDF2(12399), 30 },  { AOM_CDF2(18204), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(797), 75 },
                  { AOM_CDF2(6774), 75 },   { AOM_CDF2(16118), 1 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(32625), 95 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32748), 123 }, { AOM_CDF2(13312), 124 },
                  { AOM_CDF2(12971), 124 }, { AOM_CDF2(17162), 118 },
                  { AOM_CDF2(22019), 93 },  { AOM_CDF2(28848), 75 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(597), 90 },
                  { AOM_CDF2(8357), 1 },    { AOM_CDF2(18563), 1 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(29773), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32746), 75 }, { AOM_CDF2(15541), 124 },
                  { AOM_CDF2(19956), 76 }, { AOM_CDF2(18718), 90 },
                  { AOM_CDF2(25385), 76 }, { AOM_CDF2(27478), 90 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(1129), 75 },
                  { AOM_CDF2(13402), 6 },  { AOM_CDF2(22799), 1 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(27870), 7 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32720), 6 },  { AOM_CDF2(13119), 121 },
                  { AOM_CDF2(24726), 82 }, { AOM_CDF2(20263), 76 },
                  { AOM_CDF2(23308), 1 },  { AOM_CDF2(29238), 5 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(1274), 1 },
                  { AOM_CDF2(19978), 7 },  { AOM_CDF2(30042), 6 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(20431), 57 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32553), 31 }, { AOM_CDF2(14431), 6 },
                  { AOM_CDF2(24895), 7 },  { AOM_CDF2(24202), 6 },
                  { AOM_CDF2(28776), 7 },  { AOM_CDF2(29362), 76 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF2(22722), 90 }, { AOM_CDF2(2145), 75 },
                  { AOM_CDF2(2324), 90 },  { AOM_CDF2(5498), 0 },
                  { AOM_CDF2(4880), 0 },   { AOM_CDF2(9950), 1 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(346), 75 },
                  { AOM_CDF2(4746), 0 },   { AOM_CDF2(10806), 45 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(27410), 0 },  { AOM_CDF2(2663), 118 },
                  { AOM_CDF2(2718), 115 }, { AOM_CDF2(6587), 75 },
                  { AOM_CDF2(8350), 1 },   { AOM_CDF2(15587), 6 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(1549), 75 },
                  { AOM_CDF2(7068), 90 },  { AOM_CDF2(9967), 90 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(30222), 0 }, { AOM_CDF2(2592), 115 },
                  { AOM_CDF2(2729), 90 }, { AOM_CDF2(7930), 1 },
                  { AOM_CDF2(8953), 6 },  { AOM_CDF2(19530), 6 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(1654), 75 },
                  { AOM_CDF2(6118), 76 }, { AOM_CDF2(11631), 76 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 }, { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(31109), 31 }, { AOM_CDF2(935), 118 },
                  { AOM_CDF2(1406), 76 },  { AOM_CDF2(6229), 7 },
                  { AOM_CDF2(6543), 12 },  { AOM_CDF2(15500), 39 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(3194), 1 },
                  { AOM_CDF2(10063), 2 },  { AOM_CDF2(16398), 2 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32586), 123 }, { AOM_CDF2(8079), 7 },
                  { AOM_CDF2(15142), 2 },   { AOM_CDF2(24722), 75 },
                  { AOM_CDF2(26585), 90 },  { AOM_CDF2(31608), 1 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
          },
          {
              {
                  { AOM_CDF2(30037), 54 },  { AOM_CDF2(16384), 24 },
                  { AOM_CDF2(16384), 103 }, { AOM_CDF2(16384), 49 },
                  { AOM_CDF2(16384), 90 },  { AOM_CDF2(16384), 20 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(80), 3 },
                  { AOM_CDF2(16384), 26 },  { AOM_CDF2(16384), 9 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(32566), 9 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32744), 23 },  { AOM_CDF2(13125), 124 },
                  { AOM_CDF2(10535), 119 }, { AOM_CDF2(14316), 118 },
                  { AOM_CDF2(12072), 77 },  { AOM_CDF2(21017), 76 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(270), 75 },
                  { AOM_CDF2(4425), 1 },    { AOM_CDF2(10923), 6 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(32205), 105 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32758), 115 }, { AOM_CDF2(12734), 124 },
                  { AOM_CDF2(14371), 76 },  { AOM_CDF2(15925), 90 },
                  { AOM_CDF2(24258), 1 },   { AOM_CDF2(26907), 76 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(540), 76 },
                  { AOM_CDF2(5661), 6 },    { AOM_CDF2(13902), 6 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(31522), 5 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },   { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32759), 76 }, { AOM_CDF2(12730), 123 },
                  { AOM_CDF2(12867), 7 },  { AOM_CDF2(15844), 77 },
                  { AOM_CDF2(28087), 9 },  { AOM_CDF2(29647), 47 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(1185), 6 },
                  { AOM_CDF2(9765), 1 },   { AOM_CDF2(18440), 6 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(18801), 64 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
              {
                  { AOM_CDF2(32748), 76 }, { AOM_CDF2(14252), 2 },
                  { AOM_CDF2(19389), 1 },  { AOM_CDF2(22827), 7 },
                  { AOM_CDF2(26187), 4 },  { AOM_CDF2(28282), 14 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
                  { AOM_CDF2(16384), 0 },  { AOM_CDF2(16384), 0 },
              },
          },
      },
    };

#if CONFIG_CONTEXT_DERIVATION
static const aom_cdf_prob
    av1_default_v_txb_skip_cdfs[TOKEN_CDF_Q_CTXS][V_TXB_SKIP_CONTEXTS]
                               [CDF_SIZE(2)] = {
                                 {
                                     { AOM_CDF2(1925), 103 },
                                     { AOM_CDF2(6814), 95 },
                                     { AOM_CDF2(15440), 1 },
                                     { AOM_CDF2(559), 50 },
                                     { AOM_CDF2(10552), 0 },
                                     { AOM_CDF2(30583), 0 },
                                     { AOM_CDF2(8138), 90 },
                                     { AOM_CDF2(15535), 90 },
                                     { AOM_CDF2(23596), 75 },
                                     { AOM_CDF2(17114), 50 },
                                     { AOM_CDF2(26785), 0 },
                                     { AOM_CDF2(28672), 0 },
                                 },
                                 {
                                     { AOM_CDF2(964), 93 },
                                     { AOM_CDF2(6326), 75 },
                                     { AOM_CDF2(16496), 1 },
                                     { AOM_CDF2(3332), 34 },
                                     { AOM_CDF2(11334), 32 },
                                     { AOM_CDF2(17641), 35 },
                                     { AOM_CDF2(7634), 75 },
                                     { AOM_CDF2(14504), 0 },
                                     { AOM_CDF2(22398), 75 },
                                     { AOM_CDF2(12534), 10 },
                                     { AOM_CDF2(17711), 37 },
                                     { AOM_CDF2(21845), 45 },
                                 },
                                 {
                                     { AOM_CDF2(1192), 93 },
                                     { AOM_CDF2(7341), 75 },
                                     { AOM_CDF2(15515), 0 },
                                     { AOM_CDF2(1380), 1 },
                                     { AOM_CDF2(4501), 37 },
                                     { AOM_CDF2(11822), 32 },
                                     { AOM_CDF2(8035), 90 },
                                     { AOM_CDF2(12970), 75 },
                                     { AOM_CDF2(18643), 75 },
                                     { AOM_CDF2(13852), 32 },
                                     { AOM_CDF2(16425), 7 },
                                     { AOM_CDF2(17196), 32 },
                                 },
                                 {
                                     { AOM_CDF2(1362), 90 },
                                     { AOM_CDF2(5405), 0 },
                                     { AOM_CDF2(10235), 75 },
                                     { AOM_CDF2(987), 76 },
                                     { AOM_CDF2(4061), 7 },
                                     { AOM_CDF2(6884), 37 },
                                     { AOM_CDF2(8915), 76 },
                                     { AOM_CDF2(12716), 75 },
                                     { AOM_CDF2(16989), 75 },
                                     { AOM_CDF2(14550), 34 },
                                     { AOM_CDF2(16220), 9 },
                                     { AOM_CDF2(18128), 47 },
                                 },
                               };
#endif  // CONFIG_CONTEXT_DERIVATION

static const aom_cdf_prob av1_default_coeff_base_bob_multi_cdfs
    [TOKEN_CDF_Q_CTXS][TX_SIZES][SIG_COEF_CONTEXTS_BOB]
    [CDF_SIZE(NUM_BASE_LEVELS + 1)] = {
      {
          {
              { AOM_CDF3(9327, 16633), 15 },
              { AOM_CDF3(12473, 20430), 76 },
              { AOM_CDF3(11006, 19355), 15 },
          },
          {
              { AOM_CDF3(10104, 16597), 0 },
              { AOM_CDF3(13122, 19983), 16 },
              { AOM_CDF3(13695, 21298), 1 },
          },
          {
              { AOM_CDF3(8776, 13455), 1 },
              { AOM_CDF3(10846, 15705), 3 },
              { AOM_CDF3(11467, 17659), 1 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
          },
          {
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF3(17292, 23855), 75 },
              { AOM_CDF3(19621, 25643), 90 },
              { AOM_CDF3(17055, 25342), 0 },
          },
          {
              { AOM_CDF3(19366, 26667), 0 },
              { AOM_CDF3(21918, 28378), 0 },
              { AOM_CDF3(21806, 28925), 0 },
          },
          {
              { AOM_CDF3(23142, 29184), 6 },
              { AOM_CDF3(25565, 30488), 0 },
              { AOM_CDF3(26465, 30728), 1 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
          },
          {
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF3(17235, 25080), 90 },
              { AOM_CDF3(18542, 26200), 90 },
              { AOM_CDF3(16850, 26452), 75 },
          },
          {
              { AOM_CDF3(20758, 27807), 90 },
              { AOM_CDF3(22683, 29011), 90 },
              { AOM_CDF3(22813, 29702), 75 },
          },
          {
              { AOM_CDF3(24899, 30056), 75 },
              { AOM_CDF3(27287, 31179), 75 },
              { AOM_CDF3(28586, 31683), 75 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
          },
          {
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF3(18396, 31174), 30 },
              { AOM_CDF3(19598, 31598), 36 },
              { AOM_CDF3(18860, 31803), 32 },
          },
          {
              { AOM_CDF3(26140, 31818), 32 },
              { AOM_CDF3(26657, 32002), 32 },
              { AOM_CDF3(26148, 31963), 32 },
          },
          {
              { AOM_CDF3(29733, 32450), 37 },
              { AOM_CDF3(30024, 32604), 35 },
              { AOM_CDF3(30704, 32539), 7 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
          },
          {
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
              { AOM_CDF3(10923, 21845), 0 },  // unused
          },
      },
    };

#if CONFIG_EOB_PT_CTX_REDUCTION
static const aom_cdf_prob av1_default_eob_extra_cdfs[TOKEN_CDF_Q_CTXS]
                                                    [CDF_SIZE(2)] = {
                                                      { AOM_CDF2(16384), 0 },
                                                      { AOM_CDF2(16384), 0 },
                                                      { AOM_CDF2(16384), 0 },
                                                      { AOM_CDF2(16384), 0 },
                                                    };
#else
static const aom_cdf_prob
    av1_default_eob_extra_cdfs[TOKEN_CDF_Q_CTXS][TX_SIZES][PLANE_TYPES]
                              [EOB_COEF_CONTEXTS][CDF_SIZE(2)] = {
                                {
                                    {
                                        {
                                            { AOM_CDF2(19635), 90 },
                                            { AOM_CDF2(17463), 90 },
                                            { AOM_CDF2(6609), 30 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(22601), 21 },
                                            { AOM_CDF2(20457), 6 },
                                            { AOM_CDF2(15919), 6 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(20858), 75 },
                                            { AOM_CDF2(18166), 118 },
                                            { AOM_CDF2(15877), 118 },
                                            { AOM_CDF2(13222), 1 },
                                            { AOM_CDF2(15139), 31 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(21857), 6 },
                                            { AOM_CDF2(20959), 6 },
                                            { AOM_CDF2(19600), 90 },
                                            { AOM_CDF2(19303), 31 },
                                            { AOM_CDF2(21571), 31 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(23483), 90 },
                                            { AOM_CDF2(19130), 90 },
                                            { AOM_CDF2(17255), 115 },
                                            { AOM_CDF2(17108), 118 },
                                            { AOM_CDF2(16286), 91 },
                                            { AOM_CDF2(19276), 2 },
                                            { AOM_CDF2(21175), 37 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(23499), 6 },
                                            { AOM_CDF2(22872), 26 },
                                            { AOM_CDF2(19093), 123 },
                                            { AOM_CDF2(19682), 24 },
                                            { AOM_CDF2(22639), 119 },
                                            { AOM_CDF2(22806), 32 },
                                            { AOM_CDF2(17621), 32 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(29087), 91 },
                                            { AOM_CDF2(20670), 107 },
                                            { AOM_CDF2(17282), 115 },
                                            { AOM_CDF2(17456), 104 },
                                            { AOM_CDF2(20491), 20 },
                                            { AOM_CDF2(23686), 20 },
                                            { AOM_CDF2(15765), 35 },
                                            { AOM_CDF2(18610), 32 },
                                            { AOM_CDF2(19885), 45 },
                                        },
                                        {
                                            { AOM_CDF2(30775), 60 },
                                            { AOM_CDF2(22057), 0 },
                                            { AOM_CDF2(15641), 0 },
                                            { AOM_CDF2(18305), 0 },
                                            { AOM_CDF2(20555), 0 },
                                            { AOM_CDF2(17365), 0 },
                                            { AOM_CDF2(19043), 25 },
                                            { AOM_CDF2(28645), 50 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(30209), 50 },
                                            { AOM_CDF2(17611), 0 },
                                            { AOM_CDF2(26042), 100 },
                                            { AOM_CDF2(18665), 0 },
                                            { AOM_CDF2(14947), 0 },
                                            { AOM_CDF2(18491), 0 },
                                            { AOM_CDF2(26870), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(1890), 0 },
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
                                },
                                {
                                    {
                                        {
                                            { AOM_CDF2(18561), 100 },
                                            { AOM_CDF2(17618), 75 },
                                            { AOM_CDF2(9973), 30 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(22875), 1 },
                                            { AOM_CDF2(22790), 0 },
                                            { AOM_CDF2(20041), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(20002), 123 },
                                            { AOM_CDF2(18017), 123 },
                                            { AOM_CDF2(16224), 123 },
                                            { AOM_CDF2(15824), 0 },
                                            { AOM_CDF2(17246), 31 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(21566), 1 },
                                            { AOM_CDF2(21983), 0 },
                                            { AOM_CDF2(19785), 118 },
                                            { AOM_CDF2(22569), 0 },
                                            { AOM_CDF2(23622), 31 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(21404), 115 },
                                            { AOM_CDF2(18950), 90 },
                                            { AOM_CDF2(17608), 123 },
                                            { AOM_CDF2(18389), 90 },
                                            { AOM_CDF2(17153), 75 },
                                            { AOM_CDF2(22285), 6 },
                                            { AOM_CDF2(18032), 32 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(23447), 1 },
                                            { AOM_CDF2(23432), 1 },
                                            { AOM_CDF2(19691), 123 },
                                            { AOM_CDF2(20223), 115 },
                                            { AOM_CDF2(23003), 76 },
                                            { AOM_CDF2(23135), 31 },
                                            { AOM_CDF2(13723), 62 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(22907), 93 },
                                            { AOM_CDF2(20026), 115 },
                                            { AOM_CDF2(17705), 123 },
                                            { AOM_CDF2(17899), 115 },
                                            { AOM_CDF2(18629), 75 },
                                            { AOM_CDF2(20978), 7 },
                                            { AOM_CDF2(19825), 6 },
                                            { AOM_CDF2(16891), 32 },
                                            { AOM_CDF2(4688), 62 },
                                        },
                                        {
                                            { AOM_CDF2(25642), 32 },
                                            { AOM_CDF2(23459), 81 },
                                            { AOM_CDF2(19077), 115 },
                                            { AOM_CDF2(20186), 91 },
                                            { AOM_CDF2(20726), 9 },
                                            { AOM_CDF2(23648), 76 },
                                            { AOM_CDF2(28399), 45 },
                                            { AOM_CDF2(16384), 70 },
                                            { AOM_CDF2(16384), 100 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(28929), 8 },
                                            { AOM_CDF2(20745), 2 },
                                            { AOM_CDF2(19021), 79 },
                                            { AOM_CDF2(18349), 81 },
                                            { AOM_CDF2(18123), 6 },
                                            { AOM_CDF2(20423), 9 },
                                            { AOM_CDF2(22842), 7 },
                                            { AOM_CDF2(18936), 69 },
                                            { AOM_CDF2(28387), 61 },
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
                                },
                                {
                                    {
                                        {
                                            { AOM_CDF2(20655), 75 },
                                            { AOM_CDF2(23050), 93 },
                                            { AOM_CDF2(17392), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(23345), 0 },
                                            { AOM_CDF2(25104), 0 },
                                            { AOM_CDF2(26910), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(19059), 118 },
                                            { AOM_CDF2(18964), 123 },
                                            { AOM_CDF2(18324), 123 },
                                            { AOM_CDF2(19080), 75 },
                                            { AOM_CDF2(20147), 1 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(26477), 75 },
                                            { AOM_CDF2(25162), 75 },
                                            { AOM_CDF2(21112), 123 },
                                            { AOM_CDF2(25289), 76 },
                                            { AOM_CDF2(25803), 25 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(22569), 115 },
                                            { AOM_CDF2(19695), 123 },
                                            { AOM_CDF2(19418), 123 },
                                            { AOM_CDF2(20949), 90 },
                                            { AOM_CDF2(19945), 75 },
                                            { AOM_CDF2(18259), 6 },
                                            { AOM_CDF2(14928), 32 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(28765), 76 },
                                            { AOM_CDF2(27496), 75 },
                                            { AOM_CDF2(20567), 123 },
                                            { AOM_CDF2(20976), 90 },
                                            { AOM_CDF2(21303), 6 },
                                            { AOM_CDF2(23501), 37 },
                                            { AOM_CDF2(16384), 45 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(22390), 115 },
                                            { AOM_CDF2(20072), 115 },
                                            { AOM_CDF2(19059), 123 },
                                            { AOM_CDF2(20414), 115 },
                                            { AOM_CDF2(18738), 90 },
                                            { AOM_CDF2(21600), 6 },
                                            { AOM_CDF2(22466), 6 },
                                            { AOM_CDF2(13653), 37 },
                                            { AOM_CDF2(6910), 62 },
                                        },
                                        {
                                            { AOM_CDF2(22499), 0 },
                                            { AOM_CDF2(25296), 5 },
                                            { AOM_CDF2(21013), 120 },
                                            { AOM_CDF2(22129), 3 },
                                            { AOM_CDF2(24662), 77 },
                                            { AOM_CDF2(11703), 35 },
                                            { AOM_CDF2(17246), 45 },
                                            { AOM_CDF2(13741), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(23973), 1 },
                                            { AOM_CDF2(19266), 90 },
                                            { AOM_CDF2(18851), 90 },
                                            { AOM_CDF2(17830), 76 },
                                            { AOM_CDF2(15657), 8 },
                                            { AOM_CDF2(19990), 2 },
                                            { AOM_CDF2(19041), 7 },
                                            { AOM_CDF2(27295), 31 },
                                            { AOM_CDF2(28005), 34 },
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
                                },
                                {
                                    {
                                        {
                                            { AOM_CDF2(28727), 0 },
                                            { AOM_CDF2(30951), 78 },
                                            { AOM_CDF2(26426), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(16384), 44 },
                                            { AOM_CDF2(16384), 24 },
                                            { AOM_CDF2(16384), 87 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(21496), 90 },
                                            { AOM_CDF2(22568), 118 },
                                            { AOM_CDF2(19848), 123 },
                                            { AOM_CDF2(25024), 90 },
                                            { AOM_CDF2(30878), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(23442), 1 },
                                            { AOM_CDF2(27045), 99 },
                                            { AOM_CDF2(19707), 120 },
                                            { AOM_CDF2(20753), 100 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(19322), 118 },
                                            { AOM_CDF2(21703), 118 },
                                            { AOM_CDF2(20373), 118 },
                                            { AOM_CDF2(22381), 90 },
                                            { AOM_CDF2(22770), 75 },
                                            { AOM_CDF2(25925), 1 },
                                            { AOM_CDF2(28641), 7 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                        {
                                            { AOM_CDF2(27802), 76 },
                                            { AOM_CDF2(26055), 76 },
                                            { AOM_CDF2(20375), 96 },
                                            { AOM_CDF2(20660), 45 },
                                            { AOM_CDF2(19325), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(23423), 90 },
                                            { AOM_CDF2(22267), 118 },
                                            { AOM_CDF2(22472), 123 },
                                            { AOM_CDF2(24562), 90 },
                                            { AOM_CDF2(24091), 98 },
                                            { AOM_CDF2(17837), 39 },
                                            { AOM_CDF2(6189), 39 },
                                            { AOM_CDF2(29101), 36 },
                                            { AOM_CDF2(3046), 25 },
                                        },
                                        {
                                            { AOM_CDF2(28052), 0 },
                                            { AOM_CDF2(27552), 1 },
                                            { AOM_CDF2(22626), 95 },
                                            { AOM_CDF2(27406), 0 },
                                            { AOM_CDF2(16384), 50 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                            { AOM_CDF2(16384), 0 },
                                        },
                                    },
                                    {
                                        {
                                            { AOM_CDF2(24289), 1 },
                                            { AOM_CDF2(21324), 75 },
                                            { AOM_CDF2(19793), 115 },
                                            { AOM_CDF2(20280), 76 },
                                            { AOM_CDF2(19584), 5 },
                                            { AOM_CDF2(18382), 4 },
                                            { AOM_CDF2(23221), 39 },
                                            { AOM_CDF2(25423), 35 },
                                            { AOM_CDF2(32444), 50 },
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
                                },
                              };
#endif  // CONFIG_EOB_PT_CTX_REDUCTION

static const aom_cdf_prob
    av1_default_eob_multi16_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        5)] = {
      {
          { AOM_CDF5(1413, 1933, 3768, 9455), 6 },
          { AOM_CDF5(1954, 2400, 4205, 7753), 5 },
          { AOM_CDF5(9359, 11741, 16061, 22179), 31 },
      },
      {
          { AOM_CDF5(2832, 4201, 8578, 17754), 30 },
          { AOM_CDF5(4563, 5208, 7444, 11962), 5 },
          { AOM_CDF5(10524, 13197, 18032, 24922), 5 },
      },
      {
          { AOM_CDF5(4390, 6907, 13987, 24674), 30 },
          { AOM_CDF5(1870, 2463, 3813, 9299), 0 },
          { AOM_CDF5(15137, 18012, 23056, 29705), 0 },
      },
      {
          { AOM_CDF5(5508, 11837, 26327, 32095), 31 },
          { AOM_CDF5(6554, 8738, 10923, 24030), 0 },
          { AOM_CDF5(28607, 29647, 32421, 32595), 31 },
      },
    };

static const aom_cdf_prob
    av1_default_eob_multi32_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        6)] = {
      {
          { AOM_CDF6(1183, 1539, 2981, 7359, 12851), 31 },
          { AOM_CDF6(1847, 2098, 2631, 4422, 9368), 5 },
          { AOM_CDF6(14803, 16649, 20616, 25021, 29117), 31 },
      },
      {
          { AOM_CDF6(2170, 3095, 6309, 12580, 18493), 31 },
          { AOM_CDF6(1194, 1592, 2551, 4712, 9835), 6 },
          { AOM_CDF6(12842, 15056, 19310, 24033, 29143), 31 },
      },
      {
          { AOM_CDF6(3673, 5100, 10624, 18431, 23892), 31 },
          { AOM_CDF6(1891, 2179, 3130, 6874, 14672), 1 },
          { AOM_CDF6(17990, 20534, 24659, 28946, 31883), 6 },
      },
      {
          { AOM_CDF6(6158, 10781, 23027, 30726, 32275), 6 },
          { AOM_CDF6(971, 6554, 17719, 26336, 29370), 6 },
          { AOM_CDF6(15245, 19009, 26979, 32073, 32710), 6 },
      },
    };

static const aom_cdf_prob
    av1_default_eob_multi64_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        7)] = {
      {
          { AOM_CDF7(2312, 3086, 5417, 9567, 15311, 24073), 32 },
          { AOM_CDF7(2903, 3159, 4368, 6450, 9722, 16175), 6 },
          { AOM_CDF7(12771, 15568, 20205, 24878, 28919, 31473), 6 },
      },
      {
          { AOM_CDF7(2545, 3542, 6922, 12367, 19691, 28790), 31 },
          { AOM_CDF7(1766, 2030, 2788, 4708, 8375, 15041), 6 },
          { AOM_CDF7(14737, 17201, 21763, 26035, 29703, 31643), 31 },
      },
      {
          { AOM_CDF7(4643, 5964, 11690, 19867, 26822, 31090), 31 },
          { AOM_CDF7(8768, 9181, 12405, 15406, 19647, 24054), 6 },
          { AOM_CDF7(19321, 21411, 25537, 29191, 31652, 32512), 1 },
      },
      {
          { AOM_CDF7(4124, 5987, 14977, 24424, 30701, 32566), 31 },
          { AOM_CDF7(5308, 10204, 17063, 18202, 26370, 31387), 6 },
          { AOM_CDF7(20020, 22282, 26222, 30289, 32236, 32724), 1 },
      },
    };

static const aom_cdf_prob
    av1_default_eob_multi128_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        8)] = {
      {
          { AOM_CDF8(675, 1560, 4377, 8055, 14975, 24348, 28727), 32 },
          { AOM_CDF8(2262, 2942, 4898, 7298, 10228, 15787, 24237), 7 },
          { AOM_CDF8(6359, 10372, 16286, 21782, 27344, 30522, 32240), 31 },
      },
      {
          { AOM_CDF8(2779, 3611, 6534, 11831, 20308, 28993, 30985), 32 },
          { AOM_CDF8(2457, 2766, 3953, 5470, 9201, 15498, 22575), 32 },
          { AOM_CDF8(20578, 22601, 25934, 28568, 30706, 31903, 32453), 31 },
      },
      {
          { AOM_CDF8(5283, 6577, 12169, 19858, 27333, 31436, 32166), 6 },
          { AOM_CDF8(14043, 14677, 18201, 20658, 23277, 25799, 27975), 31 },
          { AOM_CDF8(21897, 23704, 28159, 30728, 32050, 32571, 32716), 6 },
      },
      {
          { AOM_CDF8(5794, 8143, 15765, 23935, 29693, 32195, 32579), 6 },
          { AOM_CDF8(12643, 13738, 16846, 22874, 27070, 29923, 31549), 31 },
          { AOM_CDF8(23530, 25291, 28975, 31125, 32354, 32719, 32723), 1 },
      },
    };

static const aom_cdf_prob
    av1_default_eob_multi256_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        9)] = {
      {
          { AOM_CDF9(1013, 3504, 7711, 11650, 18587, 26840, 29057, 30686), 32 },
          { AOM_CDF9(4699, 6267, 10251, 12882, 16076, 19124, 23200, 28300),
            32 },
          { AOM_CDF9(6625, 11253, 16681, 20805, 25638, 29545, 31656, 32462),
            32 },
      },
      {
          { AOM_CDF9(3077, 4254, 7792, 13858, 22338, 29819, 30981, 32009), 32 },
          { AOM_CDF9(4769, 5305, 7159, 10505, 15051, 19628, 25304, 28503), 32 },
          { AOM_CDF9(20255, 22949, 26558, 29185, 31238, 32165, 32576, 32706),
            31 },
      },
      {
          { AOM_CDF9(5175, 7312, 13185, 20927, 28256, 31787, 32350, 32576), 6 },
          { AOM_CDF9(15994, 17173, 23591, 26244, 28447, 29986, 31213, 31674),
            7 },
          { AOM_CDF9(23527, 25618, 29588, 31566, 32407, 32626, 32712, 32716),
            6 },
      },
      {
          { AOM_CDF9(6750, 9012, 17064, 25137, 30587, 32292, 32459, 32600), 6 },
          { AOM_CDF9(24628, 25525, 28220, 29470, 30917, 31924, 32370, 32429),
            7 },
          { AOM_CDF9(28744, 29723, 31544, 32323, 32566, 32716, 32720, 32724),
            1 },
      },
    };

static const aom_cdf_prob
    av1_default_eob_multi512_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        10)] = {
      {
          { AOM_CDF10(596, 5892, 14641, 18338, 23518, 29816, 30635, 31367,
                      32118),
            32 },
          { AOM_CDF10(10094, 11179, 14282, 17672, 20354, 22267, 24602, 27028,
                      29906),
            32 },
          { AOM_CDF10(9160, 13639, 21053, 24332, 27999, 29843, 31619, 32317,
                      32700),
            32 },
      },
      {
          { AOM_CDF10(4283, 6557, 10372, 16035, 23492, 31107, 31558, 32038,
                      32459),
            32 },
          { AOM_CDF10(11790, 12971, 16095, 20231, 23447, 27017, 29113, 30719,
                      31781),
            32 },
          { AOM_CDF10(19614, 22359, 26048, 28387, 31182, 32040, 32392, 32699,
                      32703),
            32 },
      },
      {
          { AOM_CDF10(5403, 7657, 12353, 19623, 27398, 32240, 32379, 32496,
                      32605),
            32 },
          { AOM_CDF10(13456, 14859, 19117, 22904, 25730, 27624, 30025, 31037,
                      32065),
            7 },
          { AOM_CDF10(20940, 23205, 26669, 29374, 31685, 32529, 32708, 32712,
                      32716),
            7 },
      },
      {
          { AOM_CDF10(8387, 10735, 16407, 24315, 30670, 32414, 32478, 32560,
                      32669),
            6 },
          { AOM_CDF10(22321, 24003, 28174, 30937, 31630, 32111, 32344, 32499,
                      32598),
            2 },
          { AOM_CDF10(25107, 26576, 29436, 31339, 32108, 32710, 32714, 32718,
                      32722),
            7 },
      },
    };

static const aom_cdf_prob
    av1_default_eob_multi1024_cdfs[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][CDF_SIZE(
        11)] = {
      {
          { AOM_CDF11(11880, 13094, 17868, 20898, 24225, 28433, 29482, 30615,
                      30976, 31295),
            57 },
          { AOM_CDF11(9825, 10303, 12215, 14392, 17207, 19332, 21243, 23102,
                      24483, 27404),
            57 },
          { AOM_CDF11(11131, 14952, 27154, 28348, 29266, 30215, 30713, 31378,
                      32095, 32692),
            57 },
      },
      {
          { AOM_CDF11(2486, 5747, 10888, 15748, 22661, 29024, 29963, 30828,
                      31477, 32304),
            32 },
          { AOM_CDF11(12967, 14212, 17927, 21505, 25428, 28451, 30544, 31373,
                      31991, 32438),
            32 },
          { AOM_CDF11(24528, 26435, 30202, 31055, 31966, 32395, 32639, 32700,
                      32704, 32708),
            32 },
      },
      {
          { AOM_CDF11(5538, 7689, 12560, 19337, 26505, 31887, 32111, 32267,
                      32477, 32616),
            32 },
          { AOM_CDF11(15280, 16484, 20477, 24536, 28156, 30431, 31766, 32181,
                      32435, 32572),
            32 },
          { AOM_CDF11(26347, 28435, 30741, 31994, 32563, 32708, 32712, 32716,
                      32720, 32724),
            32 },
      },
      {
          { AOM_CDF11(7658, 11411, 18649, 25489, 30200, 32512, 32585, 32615,
                      32653, 32692),
            6 },
          { AOM_CDF11(14042, 16903, 23512, 30033, 32254, 32631, 32704, 32708,
                      32712, 32716),
            7 },
          { AOM_CDF11(27599, 29349, 31919, 32512, 32712, 32716, 32720, 32724,
                      32728, 32732),
            7 },
      },
    };

static const aom_cdf_prob av1_default_coeff_lps_multi_cdfs_idtx
    [TOKEN_CDF_Q_CTXS][TX_SIZES][IDTX_LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)] = {
      {
          {
              { AOM_CDF4(8888, 14893, 19055), 20 },
              { AOM_CDF4(9588, 16202, 20486), 91 },
              { AOM_CDF4(8714, 14548, 18956), 75 },
              { AOM_CDF4(9260, 14956, 18856), 76 },
              { AOM_CDF4(6878, 13494, 17301), 120 },
              { AOM_CDF4(6610, 11685, 16043), 100 },
              { AOM_CDF4(3637, 6832, 9642), 93 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8744, 14735, 18771), 1 },
              { AOM_CDF4(10378, 16868, 20963), 1 },
              { AOM_CDF4(9882, 16413, 20462), 6 },
              { AOM_CDF4(10346, 16586, 20632), 76 },
              { AOM_CDF4(7694, 15061, 19003), 76 },
              { AOM_CDF4(6624, 12015, 17472), 1 },
              { AOM_CDF4(2910, 5645, 8205), 118 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(6248, 11083, 14969), 1 },
              { AOM_CDF4(7833, 12765, 16941), 7 },
              { AOM_CDF4(6338, 11625, 15656), 7 },
              { AOM_CDF4(8402, 13148, 16773), 12 },
              { AOM_CDF4(5513, 11910, 15703), 7 },
              { AOM_CDF4(4650, 8859, 15312), 32 },
              { AOM_CDF4(1890, 3872, 5855), 91 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF4(10602, 17253, 21709), 0 },
              { AOM_CDF4(10001, 16453, 20888), 75 },
              { AOM_CDF4(10225, 16422, 20615), 90 },
              { AOM_CDF4(12729, 18644, 22341), 90 },
              { AOM_CDF4(8995, 17423, 21566), 75 },
              { AOM_CDF4(7906, 14170, 20618), 75 },
              { AOM_CDF4(4232, 8031, 11563), 118 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(13565, 20175, 23969), 6 },
              { AOM_CDF4(14915, 21720, 25323), 1 },
              { AOM_CDF4(14811, 21829, 25442), 1 },
              { AOM_CDF4(17080, 23526, 26837), 6 },
              { AOM_CDF4(11230, 21285, 25544), 6 },
              { AOM_CDF4(9595, 17206, 23852), 6 },
              { AOM_CDF4(4734, 9118, 12740), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(14807, 21467, 25087), 37 },
              { AOM_CDF4(17538, 24141, 27259), 32 },
              { AOM_CDF4(17050, 24088, 27344), 37 },
              { AOM_CDF4(20445, 26394, 29010), 37 },
              { AOM_CDF4(13038, 23823, 27523), 37 },
              { AOM_CDF4(11652, 19430, 26443), 37 },
              { AOM_CDF4(5631, 10824, 15070), 32 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF4(14248, 21901, 26308), 1 },
              { AOM_CDF4(13022, 20465, 24956), 75 },
              { AOM_CDF4(14653, 21880, 25831), 1 },
              { AOM_CDF4(19516, 25671, 28510), 1 },
              { AOM_CDF4(11593, 24047, 28176), 75 },
              { AOM_CDF4(9850, 17630, 26824), 76 },
              { AOM_CDF4(7397, 13735, 18945), 75 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(16079, 23457, 27419), 1 },
              { AOM_CDF4(16572, 23555, 27047), 0 },
              { AOM_CDF4(17889, 24674, 27776), 0 },
              { AOM_CDF4(21735, 27444, 29734), 1 },
              { AOM_CDF4(12613, 25325, 29150), 1 },
              { AOM_CDF4(10535, 18413, 27982), 1 },
              { AOM_CDF4(7357, 13755, 18923), 1 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(14504, 21671, 25841), 1 },
              { AOM_CDF4(15647, 22503, 26178), 1 },
              { AOM_CDF4(17911, 24618, 27885), 1 },
              { AOM_CDF4(22537, 27989, 30157), 1 },
              { AOM_CDF4(13647, 26519, 29861), 7 },
              { AOM_CDF4(12596, 20701, 28930), 7 },
              { AOM_CDF4(7622, 14270, 19564), 32 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF4(22590, 28419, 31768), 62 },
              { AOM_CDF4(21726, 28623, 32019), 50 },
              { AOM_CDF4(26210, 31234, 32239), 50 },
              { AOM_CDF4(30134, 31969, 32691), 60 },
              { AOM_CDF4(16411, 30629, 32470), 0 },
              { AOM_CDF4(17694, 22841, 32237), 0 },
              { AOM_CDF4(18971, 25966, 30229), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(27538, 30088, 32024), 61 },
              { AOM_CDF4(29185, 31266, 32369), 60 },
              { AOM_CDF4(31081, 32206, 32656), 60 },
              { AOM_CDF4(31912, 32577, 32721), 64 },
              { AOM_CDF4(27172, 32035, 32635), 62 },
              { AOM_CDF4(27637, 28991, 32525), 62 },
              { AOM_CDF4(28323, 30227, 31455), 60 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(30776, 32228, 32629), 62 },
              { AOM_CDF4(31536, 32373, 32594), 50 },
              { AOM_CDF4(31354, 32508, 32725), 50 },
              { AOM_CDF4(32258, 32648, 32748), 62 },
              { AOM_CDF4(28867, 32205, 32666), 60 },
              { AOM_CDF4(30149, 31579, 32738), 60 },
              { AOM_CDF4(29779, 31999, 32547), 60 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
      },
    };

static const aom_cdf_prob
    av1_default_coeff_lps_multi_cdfs[TOKEN_CDF_Q_CTXS][LEVEL_CONTEXTS][CDF_SIZE(
        BR_CDF_SIZE)] = {
      {
          { AOM_CDF4(19824, 26074, 28091), 0 },
          { AOM_CDF4(21127, 28052, 30667), 78 },
          { AOM_CDF4(16981, 24862, 28664), 103 },
          { AOM_CDF4(13206, 21160, 25891), 78 },
          { AOM_CDF4(10471, 17883, 22964), 118 },
          { AOM_CDF4(8720, 15437, 20471), 103 },
          { AOM_CDF4(6269, 11597, 16021), 103 },
      },
      {
          { AOM_CDF4(22683, 28011, 29483), 5 },
          { AOM_CDF4(23879, 29807, 31582), 118 },
          { AOM_CDF4(19643, 27253, 30267), 118 },
          { AOM_CDF4(15573, 23809, 28011), 93 },
          { AOM_CDF4(12402, 20350, 25225), 93 },
          { AOM_CDF4(10221, 17535, 22608), 93 },
          { AOM_CDF4(6889, 12520, 16992), 118 },
      },
      {
          { AOM_CDF4(27317, 30994, 31856), 75 },
          { AOM_CDF4(24876, 30349, 31842), 93 },
          { AOM_CDF4(20326, 27783, 30602), 118 },
          { AOM_CDF4(15919, 24194, 28329), 118 },
          { AOM_CDF4(12565, 20621, 25541), 115 },
          { AOM_CDF4(10363, 17811, 22934), 118 },
          { AOM_CDF4(7102, 13053, 17710), 118 },
      },
      {
          { AOM_CDF4(28911, 31970, 32520), 32 },
          { AOM_CDF4(26846, 31354, 32348), 75 },
          { AOM_CDF4(22703, 29291, 31426), 77 },
          { AOM_CDF4(18357, 26472, 29846), 102 },
          { AOM_CDF4(14543, 22885, 27528), 120 },
          { AOM_CDF4(11678, 20005, 25030), 25 },
          { AOM_CDF4(8756, 16031, 20623), 110 },
      },
    };

static const aom_cdf_prob av1_default_coeff_base_multi_cdfs_idtx
    [TOKEN_CDF_Q_CTXS][TX_SIZES][IDTX_SIG_COEF_CONTEXTS]
    [CDF_SIZE(NUM_BASE_LEVELS + 2)] = {
      {
          {
              { AOM_CDF4(28332, 29935, 31012), 93 },
              { AOM_CDF4(20362, 25886, 28550), 75 },
              { AOM_CDF4(19615, 23974, 27367), 0 },
              { AOM_CDF4(16307, 19685, 22220), 75 },
              { AOM_CDF4(10574, 16536, 20288), 6 },
              { AOM_CDF4(10645, 14901, 18709), 1 },
              { AOM_CDF4(6210, 8625, 10664), 1 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(30832, 31659, 32075), 93 },
              { AOM_CDF4(21119, 27845, 30142), 0 },
              { AOM_CDF4(19103, 23961, 27788), 0 },
              { AOM_CDF4(15884, 18535, 20988), 3 },
              { AOM_CDF4(10366, 16173, 20111), 1 },
              { AOM_CDF4(10704, 13963, 18007), 1 },
              { AOM_CDF4(6057, 7686, 9376), 1 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(32359, 32469, 32538), 100 },
              { AOM_CDF4(21142, 28530, 29943), 1 },
              { AOM_CDF4(18311, 22998, 26613), 1 },
              { AOM_CDF4(14213, 15902, 17427), 75 },
              { AOM_CDF4(6217, 11382, 15267), 2 },
              { AOM_CDF4(5172, 7635, 12107), 6 },
              { AOM_CDF4(2335, 3116, 3955), 75 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF4(27637, 30565, 31401), 75 },
              { AOM_CDF4(19194, 28082, 30022), 75 },
              { AOM_CDF4(17288, 24686, 28862), 0 },
              { AOM_CDF4(15333, 20633, 23360), 0 },
              { AOM_CDF4(8479, 15708, 20299), 0 },
              { AOM_CDF4(5820, 10462, 15810), 0 },
              { AOM_CDF4(4052, 6298, 7937), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(30710, 32208, 32467), 75 },
              { AOM_CDF4(21666, 30225, 31742), 75 },
              { AOM_CDF4(18446, 26511, 30537), 0 },
              { AOM_CDF4(15422, 21150, 24424), 0 },
              { AOM_CDF4(8152, 15927, 22052), 1 },
              { AOM_CDF4(5785, 10694, 16310), 1 },
              { AOM_CDF4(3933, 6287, 8406), 5 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(31974, 32584, 32681), 80 },
              { AOM_CDF4(22164, 31135, 32259), 0 },
              { AOM_CDF4(16849, 27031, 31175), 6 },
              { AOM_CDF4(14421, 20392, 24213), 6 },
              { AOM_CDF4(6114, 13920, 22699), 37 },
              { AOM_CDF4(4166, 8660, 15473), 32 },
              { AOM_CDF4(2584, 4471, 6751), 1 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF4(28081, 30651, 31642), 93 },
              { AOM_CDF4(21015, 29162, 30874), 75 },
              { AOM_CDF4(20711, 26403, 30562), 0 },
              { AOM_CDF4(19682, 24470, 26661), 0 },
              { AOM_CDF4(12342, 20001, 25303), 1 },
              { AOM_CDF4(10294, 15740, 21371), 7 },
              { AOM_CDF4(9787, 13332, 15360), 30 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(30670, 32199, 32481), 75 },
              { AOM_CDF4(21913, 30787, 32043), 93 },
              { AOM_CDF4(18889, 27255, 31240), 75 },
              { AOM_CDF4(17707, 23115, 26169), 0 },
              { AOM_CDF4(9693, 17800, 24044), 1 },
              { AOM_CDF4(7150, 12327, 18583), 1 },
              { AOM_CDF4(6971, 9884, 12326), 6 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(32025, 32589, 32684), 85 },
              { AOM_CDF4(22479, 31362, 32313), 75 },
              { AOM_CDF4(17286, 27306, 31443), 1 },
              { AOM_CDF4(16027, 22152, 25878), 1 },
              { AOM_CDF4(7631, 15855, 23931), 6 },
              { AOM_CDF4(5389, 10664, 17856), 1 },
              { AOM_CDF4(4921, 7819, 10468), 6 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
      },
      {
          {
              { AOM_CDF4(29321, 31402, 32585), 0 },
              { AOM_CDF4(24982, 31542, 32538), 5 },
              { AOM_CDF4(25292, 27217, 32553), 5 },
              { AOM_CDF4(22458, 26136, 29103), 15 },
              { AOM_CDF4(14760, 17180, 31874), 11 },
              { AOM_CDF4(13023, 17414, 23724), 25 },
              { AOM_CDF4(16460, 19241, 20844), 100 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(30705, 32502, 32717), 30 },
              { AOM_CDF4(23047, 32095, 32667), 0 },
              { AOM_CDF4(19619, 27421, 32382), 31 },
              { AOM_CDF4(16188, 21666, 27068), 31 },
              { AOM_CDF4(7419, 12132, 28951), 32 },
              { AOM_CDF4(5170, 8890, 16032), 26 },
              { AOM_CDF4(6099, 10136, 13330), 57 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(32087, 32712, 32748), 85 },
              { AOM_CDF4(20760, 32439, 32743), 0 },
              { AOM_CDF4(11650, 30566, 32663), 32 },
              { AOM_CDF4(11495, 20251, 28581), 32 },
              { AOM_CDF4(5254, 9632, 29930), 32 },
              { AOM_CDF4(4291, 7115, 16582), 35 },
              { AOM_CDF4(7174, 8941, 11140), 32 },
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
              { AOM_CDF4(8192, 16384, 24576), 0 },  // unused
          },
      },
    };

#if TCQ_DIS_CHR
static const aom_cdf_prob av1_default_coeff_base_lf_multi_uv_cdfs
    [TOKEN_CDF_Q_CTXS][LF_SIG_COEF_CONTEXTS_UV][TCQ_CTXS]
    [CDF_SIZE(LF_BASE_SYMBOLS)] = {
      {
          {
              { AOM_CDF6(14366, 26260, 30107, 31462, 31984), 6 },
              { AOM_CDF6(14366, 26260, 30107, 31462, 31984), 6 },
          },
          {
              { AOM_CDF6(8435, 20211, 26486, 29544, 30971), 75 },
              { AOM_CDF6(8435, 20211, 26486, 29544, 30971), 75 },
          },
          {
              { AOM_CDF6(4468, 12279, 18551, 23241, 26398), 118 },
              { AOM_CDF6(4468, 12279, 18551, 23241, 26398), 118 },
          },
          {
              { AOM_CDF6(2017, 5852, 9588, 13227, 16466), 90 },
              { AOM_CDF6(2017, 5852, 9588, 13227, 16466), 90 },
          },
          {
              { AOM_CDF6(13234, 26965, 30663, 31921, 32326), 6 },
              { AOM_CDF6(13234, 26965, 30663, 31921, 32326), 6 },
          },
          {
              { AOM_CDF6(9638, 23022, 28785, 31064, 31943), 118 },
              { AOM_CDF6(9638, 23022, 28785, 31064, 31943), 118 },
          },
          {
              { AOM_CDF6(5677, 15107, 21848, 26210, 28932), 90 },
              { AOM_CDF6(5677, 15107, 21848, 26210, 28932), 90 },
          },
          {
              { AOM_CDF6(2482, 7244, 11769, 15966, 19494), 0 },
              { AOM_CDF6(2482, 7244, 11769, 15966, 19494), 0 },
          },
          {
              { AOM_CDF6(26308, 32422, 32700, 32734, 32738), 0 },
              { AOM_CDF6(26308, 32422, 32700, 32734, 32738), 0 },
          },
          {
              { AOM_CDF6(18410, 31272, 32441, 32627, 32684), 80 },
              { AOM_CDF6(18410, 31272, 32441, 32627, 32684), 80 },
          },
          {
              { AOM_CDF6(9279, 21122, 27290, 29891, 30980), 2 },
              { AOM_CDF6(9279, 21122, 27290, 29891, 30980), 2 },
          },
          {
              { AOM_CDF6(5425, 11645, 15311, 18838, 21700), 24 },
              { AOM_CDF6(5425, 11645, 15311, 18838, 21700), 24 },
          },
      },
      {
          {
              { AOM_CDF6(15811, 26488, 30226, 31615, 32132), 6 },
              { AOM_CDF6(15811, 26488, 30226, 31615, 32132), 6 },
          },
          {
              { AOM_CDF6(8921, 21492, 27700, 30444, 31559), 75 },
              { AOM_CDF6(8921, 21492, 27700, 30444, 31559), 75 },
          },
          {
              { AOM_CDF6(4841, 13432, 20064, 24648, 27631), 93 },
              { AOM_CDF6(4841, 13432, 20064, 24648, 27631), 93 },
          },
          {
              { AOM_CDF6(2088, 6035, 9950, 13721, 17072), 118 },
              { AOM_CDF6(2088, 6035, 9950, 13721, 17072), 118 },
          },
          {
              { AOM_CDF6(15540, 27901, 31326, 32200, 32479), 0 },
              { AOM_CDF6(15540, 27901, 31326, 32200, 32479), 0 },
          },
          {
              { AOM_CDF6(10351, 24377, 29698, 31536, 32155), 75 },
              { AOM_CDF6(10351, 24377, 29698, 31536, 32155), 75 },
          },
          {
              { AOM_CDF6(5509, 15008, 21741, 26006, 28626), 90 },
              { AOM_CDF6(5509, 15008, 21741, 26006, 28626), 90 },
          },
          {
              { AOM_CDF6(2146, 6241, 10294, 14072, 17392), 115 },
              { AOM_CDF6(2146, 6241, 10294, 14072, 17392), 115 },
          },
          {
              { AOM_CDF6(27338, 32320, 32643, 32709, 32732), 93 },
              { AOM_CDF6(27338, 32320, 32643, 32709, 32732), 93 },
          },
          {
              { AOM_CDF6(17248, 30295, 31985, 32426, 32580), 90 },
              { AOM_CDF6(17248, 30295, 31985, 32426, 32580), 90 },
          },
          {
              { AOM_CDF6(7979, 18661, 24838, 28420, 30133), 90 },
              { AOM_CDF6(7979, 18661, 24838, 28420, 30133), 90 },
          },
          {
              { AOM_CDF6(4565, 11089, 15855, 19468, 22228), 16 },
              { AOM_CDF6(4565, 11089, 15855, 19468, 22228), 16 },
          },
      },
      {
          {
              { AOM_CDF6(14796, 27248, 30689, 31786, 32176), 1 },
              { AOM_CDF6(14796, 27248, 30689, 31786, 32176), 1 },
          },
          {
              { AOM_CDF6(8580, 21471, 27905, 30611, 31681), 75 },
              { AOM_CDF6(8580, 21471, 27905, 30611, 31681), 75 },
          },
          {
              { AOM_CDF6(4644, 13296, 20094, 24691, 27651), 118 },
              { AOM_CDF6(4644, 13296, 20094, 24691, 27651), 118 },
          },
          {
              { AOM_CDF6(1845, 5492, 9273, 13250, 16963), 123 },
              { AOM_CDF6(1845, 5492, 9273, 13250, 16963), 123 },
          },
          {
              { AOM_CDF6(15149, 27491, 30644, 31724, 32177), 0 },
              { AOM_CDF6(15149, 27491, 30644, 31724, 32177), 0 },
          },
          {
              { AOM_CDF6(8469, 21408, 27928, 30603, 31665), 118 },
              { AOM_CDF6(8469, 21408, 27928, 30603, 31665), 118 },
          },
          {
              { AOM_CDF6(4538, 12465, 18903, 23659, 26874), 93 },
              { AOM_CDF6(4538, 12465, 18903, 23659, 26874), 93 },
          },
          {
              { AOM_CDF6(1812, 5425, 9106, 12838, 16349), 115 },
              { AOM_CDF6(1812, 5425, 9106, 12838, 16349), 115 },
          },
          {
              { AOM_CDF6(27658, 32396, 32681, 32736, 32740), 75 },
              { AOM_CDF6(27658, 32396, 32681, 32736, 32740), 75 },
          },
          {
              { AOM_CDF6(17508, 30543, 32263, 32604, 32687), 115 },
              { AOM_CDF6(17508, 30543, 32263, 32604, 32687), 115 },
          },
          {
              { AOM_CDF6(8627, 20222, 26962, 30179, 31545), 77 },
              { AOM_CDF6(8627, 20222, 26962, 30179, 31545), 77 },
          },
          {
              { AOM_CDF6(4502, 11287, 17510, 22262, 25858), 79 },
              { AOM_CDF6(4502, 11287, 17510, 22262, 25858), 79 },
          },
      },
      {
          {
              { AOM_CDF6(12028, 25805, 30242, 31686, 32104), 14 },
              { AOM_CDF6(12028, 25805, 30242, 31686, 32104), 14 },
          },
          {
              { AOM_CDF6(6873, 20683, 28552, 31327, 32198), 75 },
              { AOM_CDF6(6873, 20683, 28552, 31327, 32198), 75 },
          },
          {
              { AOM_CDF6(4417, 13376, 21020, 26076, 29345), 99 },
              { AOM_CDF6(4417, 13376, 21020, 26076, 29345), 99 },
          },
          {
              { AOM_CDF6(1872, 5776, 10779, 15967, 20663), 120 },
              { AOM_CDF6(1872, 5776, 10779, 15967, 20663), 120 },
          },
          {
              { AOM_CDF6(12010, 25672, 30138, 31773, 32134), 35 },
              { AOM_CDF6(12010, 25672, 30138, 31773, 32134), 35 },
          },
          {
              { AOM_CDF6(7029, 21007, 28934, 31567, 32294), 90 },
              { AOM_CDF6(7029, 21007, 28934, 31567, 32294), 90 },
          },
          {
              { AOM_CDF6(4309, 12652, 20158, 25517, 29022), 76 },
              { AOM_CDF6(4309, 12652, 20158, 25517, 29022), 76 },
          },
          {
              { AOM_CDF6(2072, 6184, 10807, 15838, 20961), 75 },
              { AOM_CDF6(2072, 6184, 10807, 15838, 20961), 75 },
          },
          {
              { AOM_CDF6(29343, 32431, 32638, 32716, 32732), 45 },
              { AOM_CDF6(29343, 32431, 32638, 32716, 32732), 45 },
          },
          {
              { AOM_CDF6(18659, 30546, 32380, 32697, 32732), 100 },
              { AOM_CDF6(18659, 30546, 32380, 32697, 32732), 100 },
          },
          {
              { AOM_CDF6(5958, 8937, 23831, 26810, 28300), 100 },
              { AOM_CDF6(5958, 8937, 23831, 26810, 28300), 100 },
          },
          {
              { AOM_CDF6(4681, 9362, 14043, 18725, 28087), 0 },
              { AOM_CDF6(4681, 9362, 14043, 18725, 28087), 0 },
          },
      },
    };
#else
static const aom_cdf_prob av1_default_coeff_base_lf_multi_uv_cdfs
    [TOKEN_CDF_Q_CTXS][LF_SIG_COEF_CONTEXTS_UV][TCQ_CTXS]
    [CDF_SIZE(LF_BASE_SYMBOLS)] = {
      {
          {
              { AOM_CDF6(9781, 21716, 27297, 30171, 31110), 45 },
              { AOM_CDF6(25, 8860, 27571, 31085, 31828), 32 },
          },
          {
              { AOM_CDF6(6044, 17805, 25198, 29228, 30809), 123 },
              { AOM_CDF6(4, 12898, 25888, 30044, 31458), 91 },
          },
          {
              { AOM_CDF6(4822, 13604, 19973, 24491, 27595), 118 },
              { AOM_CDF6(5, 8491, 15839, 21471, 25204), 121 },
          },
          {
              { AOM_CDF6(2045, 6193, 9814, 12908, 15866), 93 },
              { AOM_CDF6(7, 4085, 8326, 11860, 14968), 93 },
          },
          {
              { AOM_CDF6(13073, 23713, 28920, 31523, 32259), 34 },
              { AOM_CDF6(37, 14834, 28451, 31329, 32620), 82 },
          },
          {
              { AOM_CDF6(9001, 23111, 28960, 31093, 31817), 103 },
              { AOM_CDF6(4, 16471, 27662, 31008, 32062), 120 },
          },
          {
              { AOM_CDF6(7055, 17234, 23739, 27193, 29766), 94 },
              { AOM_CDF6(11, 9261, 17981, 24358, 27264), 99 },
          },
          {
              { AOM_CDF6(3132, 8652, 13164, 17567, 21093), 75 },
              { AOM_CDF6(23, 4747, 10050, 14450, 18179), 118 },
          },
          {
              { AOM_CDF6(28309, 32616, 32661, 32707, 32732), 24 },
              { AOM_CDF6(13783, 31945, 32673, 32696, 32732), 3 },
          },
          {
              { AOM_CDF6(21420, 31620, 32587, 32696, 32732), 120 },
              { AOM_CDF6(8644, 30763, 32417, 32673, 32702), 4 },
          },
          {
              { AOM_CDF6(9389, 21723, 26141, 30743, 31848), 124 },
              { AOM_CDF6(4546, 15153, 23676, 28222, 30306), 9 },
          },
          {
              { AOM_CDF6(7022, 11703, 22235, 25746, 29257), 50 },
              { AOM_CDF6(2048, 9216, 11264, 15360, 20480), 25 },
          },
      },
      {
          {
              { AOM_CDF6(17089, 27785, 30515, 31581, 32077), 0 },
              { AOM_CDF6(4, 21553, 28458, 31162, 32004), 1 },
          },
          {
              { AOM_CDF6(9569, 21542, 27032, 29749, 31073), 75 },
              { AOM_CDF6(4, 17664, 26364, 30020, 31345), 1 },
          },
          {
              { AOM_CDF6(6312, 15849, 22169, 26107, 28599), 78 },
              { AOM_CDF6(4, 8577, 16450, 22344, 26125), 75 },
          },
          {
              { AOM_CDF6(2192, 6356, 10123, 13646, 16792), 90 },
              { AOM_CDF6(4, 3926, 7904, 11846, 15201), 90 },
          },
          {
              { AOM_CDF6(15280, 26834, 30439, 31709, 32325), 0 },
              { AOM_CDF6(4, 23491, 30058, 31889, 32498), 51 },
          },
          {
              { AOM_CDF6(9976, 22737, 28250, 30663, 31704), 93 },
              { AOM_CDF6(4, 19218, 27956, 31011, 31945), 6 },
          },
          {
              { AOM_CDF6(7250, 17716, 23931, 27624, 29847), 93 },
              { AOM_CDF6(4, 9917, 18344, 24214, 27605), 76 },
          },
          {
              { AOM_CDF6(2539, 7275, 11714, 15486, 18764), 0 },
              { AOM_CDF6(4, 4634, 9034, 13181, 16377), 0 },
          },
          {
              { AOM_CDF6(29690, 32547, 32695, 32734, 32738), 75 },
              { AOM_CDF6(17659, 32273, 32669, 32736, 32740), 75 },
          },
          {
              { AOM_CDF6(21521, 31070, 32355, 32612, 32690), 75 },
              { AOM_CDF6(7095, 29768, 32290, 32612, 32693), 75 },
          },
          {
              { AOM_CDF6(6308, 20000, 27408, 30707, 31946), 7 },
              { AOM_CDF6(6356, 16059, 24613, 29334, 31369), 2 },
          },
          {
              { AOM_CDF6(3181, 9353, 15716, 21315, 26405), 25 },
              { AOM_CDF6(2938, 8623, 14436, 20568, 23762), 75 },
          },
      },
      {
          {
              { AOM_CDF6(16779, 27741, 30586, 31687, 32174), 6 },
              { AOM_CDF6(4, 17861, 27313, 31201, 32291), 75 },
          },
          {
              { AOM_CDF6(8622, 21582, 27525, 30404, 31690), 75 },
              { AOM_CDF6(4, 18219, 27821, 31202, 32109), 75 },
          },
          {
              { AOM_CDF6(7291, 17592, 23804, 27697, 30026), 75 },
              { AOM_CDF6(4, 9740, 18640, 24314, 27709), 76 },
          },
          {
              { AOM_CDF6(2354, 6977, 11538, 15787, 19231), 90 },
              { AOM_CDF6(4, 4544, 9311, 14106, 18044), 90 },
          },
          {
              { AOM_CDF6(15302, 25932, 29598, 31384, 31942), 1 },
              { AOM_CDF6(4, 21357, 29514, 31600, 32365), 1 },
          },
          {
              { AOM_CDF6(7392, 19389, 26018, 29407, 31174), 75 },
              { AOM_CDF6(4, 16343, 26803, 30800, 31863), 5 },
          },
          {
              { AOM_CDF6(5620, 14659, 21274, 26092, 28891), 90 },
              { AOM_CDF6(4, 8349, 14964, 21121, 25439), 76 },
          },
          {
              { AOM_CDF6(1689, 5936, 9187, 13379, 16863), 0 },
              { AOM_CDF6(271, 3371, 7374, 11366, 14399), 0 },
          },
          {
              { AOM_CDF6(31204, 32650, 32725, 32736, 32740), 0 },
              { AOM_CDF6(22725, 32494, 32705, 32736, 32740), 1 },
          },
          {
              { AOM_CDF6(19670, 31154, 32516, 32702, 32732), 76 },
              { AOM_CDF6(2908, 29751, 32484, 32685, 32732), 0 },
          },
          {
              { AOM_CDF6(5193, 20861, 28590, 31395, 32305), 10 },
              { AOM_CDF6(6525, 15813, 25636, 29498, 31365), 10 },
          },
          {
              { AOM_CDF6(3250, 10697, 16655, 24237, 28164), 25 },
              { AOM_CDF6(2855, 5847, 14412, 20939, 24202), 25 },
          },
      },
      {
          {
              { AOM_CDF6(6785, 19632, 25406, 30025, 31469), 25 },
              { AOM_CDF6(22, 9454, 25152, 30620, 32369), 45 },
          },
          {
              { AOM_CDF6(5518, 20171, 28048, 31133, 32098), 0 },
              { AOM_CDF6(6, 11987, 26093, 31223, 32236), 75 },
          },
          {
              { AOM_CDF6(5057, 12939, 21130, 27498, 30854), 1 },
              { AOM_CDF6(1120, 7814, 17439, 23194, 28362), 6 },
          },
          {
              { AOM_CDF6(1986, 6209, 10943, 16915, 22087), 22 },
              { AOM_CDF6(137, 4102, 10591, 16446, 21506), 80 },
          },
          {
              { AOM_CDF6(3745, 13294, 22657, 26776, 30147), 100 },
              { AOM_CDF6(170, 9057, 24796, 30190, 32259), 25 },
          },
          {
              { AOM_CDF6(4951, 17936, 26414, 30470, 31812), 75 },
              { AOM_CDF6(16, 11899, 25933, 31002, 32278), 80 },
          },
          {
              { AOM_CDF6(4085, 11663, 20105, 26867, 30144), 76 },
              { AOM_CDF6(530, 5838, 14592, 21583, 26694), 94 },
          },
          {
              { AOM_CDF6(2720, 6915, 11401, 16409, 20976), 5 },
              { AOM_CDF6(271, 3075, 8557, 15335, 21446), 10 },
          },
          {
              { AOM_CDF6(28672, 29491, 30310, 31130, 31949), 50 },
              { AOM_CDF6(24824, 28796, 29789, 30782, 31775), 50 },
          },
          {
              { AOM_CDF6(15916, 27151, 29959, 30896, 31832), 50 },
              { AOM_CDF6(10031, 18056, 29424, 31431, 32099), 100 },
          },
          {
              { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              { AOM_CDF6(4681, 14043, 18725, 23406, 28087), 0 },
          },
          {
              { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
          },
      },
    };
#endif

#if !CONFIG_COEFF_BR_LF_UV_BYPASS
static const aom_cdf_prob av1_default_coeff_lps_lf_multi_uv_cdfs
    [TOKEN_CDF_Q_CTXS][LF_LEVEL_CONTEXTS_UV][CDF_SIZE(BR_CDF_SIZE)] = {
      {
          { AOM_CDF4(10681, 16666, 20459), 27 },
          { AOM_CDF4(12934, 19949, 23983), 0 },
          { AOM_CDF4(10674, 17654, 22203), 80 },
          { AOM_CDF4(5672, 10350, 14164), 15 },
          { AOM_CDF4(10159, 15058, 19868), 110 },
          { AOM_CDF4(12945, 19062, 23123), 50 },
          { AOM_CDF4(10948, 17325, 21167), 10 },
          { AOM_CDF4(6193, 11020, 14440), 37 },
      },
      {
          { AOM_CDF4(10534, 16544, 20143), 6 },
          { AOM_CDF4(13446, 20483, 24400), 90 },
          { AOM_CDF4(10975, 17881, 22362), 115 },
          { AOM_CDF4(5486, 9994, 13627), 118 },
          { AOM_CDF4(14031, 19994, 23841), 5 },
          { AOM_CDF4(13915, 21251, 25225), 10 },
          { AOM_CDF4(11758, 18566, 22936), 35 },
          { AOM_CDF4(6777, 12174, 16431), 37 },
      },
      {
          { AOM_CDF4(8194, 12742, 15742), 6 },
          { AOM_CDF4(12852, 19642, 23312), 75 },
          { AOM_CDF4(10713, 17509, 22027), 75 },
          { AOM_CDF4(6174, 11173, 15173), 90 },
          { AOM_CDF4(19410, 25254, 28802), 50 },
          { AOM_CDF4(16632, 23992, 27460), 0 },
          { AOM_CDF4(15777, 22452, 26457), 100 },
          { AOM_CDF4(10127, 16718, 20635), 110 },
      },
      {
          { AOM_CDF4(9562, 14462, 17818), 50 },
          { AOM_CDF4(15932, 23039, 26083), 9 },
          { AOM_CDF4(14358, 22335, 26570), 20 },
          { AOM_CDF4(10210, 17627, 22031), 20 },
          { AOM_CDF4(8192, 16384, 24576), 0 },
          { AOM_CDF4(8192, 16384, 24576), 0 },
          { AOM_CDF4(10923, 16384, 27307), 0 },
          { AOM_CDF4(8192, 16384, 24576), 0 },
      },
    };
#endif  // !CONFIG_COEFF_BR_LF_UV_BYPASS

#if TCQ_DIS_CHR
static const aom_cdf_prob
    av1_default_coeff_base_multi_uv_cdfs[4][12][2][CDF_SIZE(4)] = {
      {
          {
              { AOM_CDF4(26007, 32241, 32639), 0 },
              { AOM_CDF4(26007, 32241, 32639), 0 },
          },
          {
              { AOM_CDF4(17365, 29572, 31911), 90 },
              { AOM_CDF4(17365, 29572, 31911), 90 },
          },
          {
              { AOM_CDF4(8543, 21075, 27313), 75 },
              { AOM_CDF4(8543, 21075, 27313), 75 },
          },
          {
              { AOM_CDF4(4393, 12441, 18915), 76 },
              { AOM_CDF4(4393, 12441, 18915), 76 },
          },
          {
              { AOM_CDF4(26852, 32428, 32678), 75 },
              { AOM_CDF4(26852, 32428, 32678), 75 },
          },
          {
              { AOM_CDF4(18225, 30513, 32242), 120 },
              { AOM_CDF4(18225, 30513, 32242), 120 },
          },
          {
              { AOM_CDF4(9602, 22923, 28632), 75 },
              { AOM_CDF4(9602, 22923, 28632), 75 },
          },
          {
              { AOM_CDF4(4728, 13291, 19961), 15 },
              { AOM_CDF4(4728, 13291, 19961), 15 },
          },
          {
              { AOM_CDF4(28239, 32612, 32732), 105 },
              { AOM_CDF4(28239, 32612, 32732), 105 },
          },
          {
              { AOM_CDF4(21777, 31920, 32593), 75 },
              { AOM_CDF4(21777, 31920, 32593), 75 },
          },
          {
              { AOM_CDF4(10167, 22287, 27755), 20 },
              { AOM_CDF4(10167, 22287, 27755), 20 },
          },
          {
              { AOM_CDF4(3763, 10274, 17843), 84 },
              { AOM_CDF4(3763, 10274, 17843), 84 },
          },
      },
      {
          {
              { AOM_CDF4(26071, 32361, 32667), 75 },
              { AOM_CDF4(26071, 32361, 32667), 75 },
          },
          {
              { AOM_CDF4(16124, 29499, 31947), 75 },
              { AOM_CDF4(16124, 29499, 31947), 75 },
          },
          {
              { AOM_CDF4(8857, 21690, 27718), 75 },
              { AOM_CDF4(8857, 21690, 27718), 75 },
          },
          {
              { AOM_CDF4(4563, 12848, 19240), 75 },
              { AOM_CDF4(4563, 12848, 19240), 75 },
          },
          {
              { AOM_CDF4(27210, 32509, 32703), 0 },
              { AOM_CDF4(27210, 32509, 32703), 0 },
          },
          {
              { AOM_CDF4(17974, 30478, 32206), 75 },
              { AOM_CDF4(17974, 30478, 32206), 75 },
          },
          {
              { AOM_CDF4(9325, 22310, 28033), 75 },
              { AOM_CDF4(9325, 22310, 28033), 75 },
          },
          {
              { AOM_CDF4(4214, 11861, 18089), 75 },
              { AOM_CDF4(4214, 11861, 18089), 75 },
          },
          {
              { AOM_CDF4(28512, 32439, 32684), 75 },
              { AOM_CDF4(28512, 32439, 32684), 75 },
          },
          {
              { AOM_CDF4(19918, 30794, 32233), 90 },
              { AOM_CDF4(19918, 30794, 32233), 90 },
          },
          {
              { AOM_CDF4(8545, 20451, 26400), 5 },
              { AOM_CDF4(8545, 20451, 26400), 5 },
          },
          {
              { AOM_CDF4(3297, 10809, 17416), 96 },
              { AOM_CDF4(3297, 10809, 17416), 96 },
          },
      },
      {
          {
              { AOM_CDF4(26847, 32431, 32693), 75 },
              { AOM_CDF4(26847, 32431, 32693), 75 },
          },
          {
              { AOM_CDF4(16626, 29898, 32086), 75 },
              { AOM_CDF4(16626, 29898, 32086), 75 },
          },
          {
              { AOM_CDF4(9575, 22628, 28255), 75 },
              { AOM_CDF4(9575, 22628, 28255), 75 },
          },
          {
              { AOM_CDF4(4790, 13131, 19545), 76 },
              { AOM_CDF4(4790, 13131, 19545), 76 },
          },
          {
              { AOM_CDF4(26405, 32274, 32665), 75 },
              { AOM_CDF4(26405, 32274, 32665), 75 },
          },
          {
              { AOM_CDF4(15958, 29449, 31888), 75 },
              { AOM_CDF4(15958, 29449, 31888), 75 },
          },
          {
              { AOM_CDF4(9027, 21837, 27644), 1 },
              { AOM_CDF4(9027, 21837, 27644), 1 },
          },
          {
              { AOM_CDF4(4555, 12639, 18997), 1 },
              { AOM_CDF4(4555, 12639, 18997), 1 },
          },
          {
              { AOM_CDF4(29803, 32664, 32748), 75 },
              { AOM_CDF4(29803, 32664, 32748), 75 },
          },
          {
              { AOM_CDF4(21668, 31797, 32594), 115 },
              { AOM_CDF4(21668, 31797, 32594), 115 },
          },
          {
              { AOM_CDF4(10619, 23941, 29332), 76 },
              { AOM_CDF4(10619, 23941, 29332), 76 },
          },
          {
              { AOM_CDF4(3730, 9857, 15718), 100 },
              { AOM_CDF4(3730, 9857, 15718), 100 },
          },
      },
      {
          {
              { AOM_CDF4(26525, 32419, 32724), 75 },
              { AOM_CDF4(26525, 32419, 32724), 75 },
          },
          {
              { AOM_CDF4(13626, 29583, 32261), 75 },
              { AOM_CDF4(13626, 29583, 32261), 75 },
          },
          {
              { AOM_CDF4(9014, 23415, 29326), 7 },
              { AOM_CDF4(9014, 23415, 29326), 7 },
          },
          {
              { AOM_CDF4(5308, 16384, 23590), 75 },
              { AOM_CDF4(5308, 16384, 23590), 75 },
          },
          {
              { AOM_CDF4(25016, 32329, 32719), 75 },
              { AOM_CDF4(25016, 32329, 32719), 75 },
          },
          {
              { AOM_CDF4(14001, 29360, 32145), 75 },
              { AOM_CDF4(14001, 29360, 32145), 75 },
          },
          {
              { AOM_CDF4(8594, 22169, 28754), 9 },
              { AOM_CDF4(8594, 22169, 28754), 9 },
          },
          {
              { AOM_CDF4(5179, 14375, 22253), 95 },
              { AOM_CDF4(5179, 14375, 22253), 95 },
          },
          {
              { AOM_CDF4(30742, 32620, 32719), 110 },
              { AOM_CDF4(30742, 32620, 32719), 110 },
          },
          {
              { AOM_CDF4(24363, 31917, 32449), 0 },
              { AOM_CDF4(24363, 31917, 32449), 0 },
          },
          {
              { AOM_CDF4(12288, 24576, 28672), 0 },
              { AOM_CDF4(12288, 24576, 28672), 0 },
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
          },
      },
    };
#else
static const aom_cdf_prob
    av1_default_coeff_base_multi_uv_cdfs[4][12][2][CDF_SIZE(4)] = {
      {
          {
              { AOM_CDF4(26976, 32357, 32674), 15 },
              { AOM_CDF4(13688, 31325, 32590), 75 },
          },
          {
              { AOM_CDF4(16033, 29664, 31962), 123 },
              { AOM_CDF4(4073, 26157, 31417), 123 },
          },
          {
              { AOM_CDF4(9270, 22903, 28549), 115 },
              { AOM_CDF4(2753, 18787, 27123), 91 },
          },
          {
              { AOM_CDF4(4240, 13395, 19917), 75 },
              { AOM_CDF4(2407, 10623, 17620), 75 },
          },
          {
              { AOM_CDF4(28362, 32592, 32740), 75 },
              { AOM_CDF4(14694, 31879, 32670), 104 },
          },
          {
              { AOM_CDF4(19420, 31069, 32479), 115 },
              { AOM_CDF4(4624, 28785, 32101), 100 },
          },
          {
              { AOM_CDF4(11625, 25964, 30133), 75 },
              { AOM_CDF4(4019, 21776, 29015), 5 },
          },
          {
              { AOM_CDF4(5223, 15873, 23538), 1 },
              { AOM_CDF4(1830, 11222, 18485), 5 },
          },
          {
              { AOM_CDF4(29009, 32597, 32708), 15 },
              { AOM_CDF4(16530, 32275, 32711), 0 },
          },
          {
              { AOM_CDF4(24365, 32118, 32692), 95 },
              { AOM_CDF4(11714, 31245, 32608), 75 },
          },
          {
              { AOM_CDF4(13293, 26122, 29522), 107 },
              { AOM_CDF4(3625, 20358, 29840), 22 },
          },
          {
              { AOM_CDF4(8192, 16384, 19115), 100 },
              { AOM_CDF4(6554, 9830, 19661), 0 },
          },
      },
      {
          {
              { AOM_CDF4(29035, 32617, 32714), 0 },
              { AOM_CDF4(14664, 31820, 32613), 3 },
          },
          {
              { AOM_CDF4(19845, 30840, 32248), 78 },
              { AOM_CDF4(4522, 28407, 31913), 78 },
          },
          {
              { AOM_CDF4(11835, 25461, 29832), 3 },
              { AOM_CDF4(3202, 21248, 28587), 3 },
          },
          {
              { AOM_CDF4(4584, 13746, 20185), 78 },
              { AOM_CDF4(1405, 10093, 17846), 78 },
          },
          {
              { AOM_CDF4(29244, 32623, 32720), 0 },
              { AOM_CDF4(15389, 32161, 32669), 75 },
          },
          {
              { AOM_CDF4(21511, 31520, 32460), 75 },
              { AOM_CDF4(4525, 29522, 32179), 78 },
          },
          {
              { AOM_CDF4(13999, 27154, 30730), 5 },
              { AOM_CDF4(2856, 23376, 29705), 80 },
          },
          {
              { AOM_CDF4(3975, 13240, 19693), 1 },
              { AOM_CDF4(1624, 10887, 18511), 8 },
          },
          {
              { AOM_CDF4(30515, 32708, 32748), 0 },
              { AOM_CDF4(20446, 32460, 32728), 0 },
          },
          {
              { AOM_CDF4(23894, 32060, 32635), 75 },
              { AOM_CDF4(11352, 31486, 32583), 90 },
          },
          {
              { AOM_CDF4(9135, 23534, 29064), 81 },
              { AOM_CDF4(3043, 19751, 28730), 75 },
          },
          {
              { AOM_CDF4(1324, 9268, 21514), 50 },
              { AOM_CDF4(3981, 8881, 16843), 100 },
          },
      },
      {
          {
              { AOM_CDF4(29827, 32679, 32740), 90 },
              { AOM_CDF4(13218, 31550, 32609), 78 },
          },
          {
              { AOM_CDF4(19520, 30844, 32312), 78 },
              { AOM_CDF4(2134, 28288, 31990), 78 },
          },
          {
              { AOM_CDF4(12557, 25614, 29993), 75 },
              { AOM_CDF4(2400, 21650, 28830), 0 },
          },
          {
              { AOM_CDF4(4509, 13733, 20527), 5 },
              { AOM_CDF4(1122, 10011, 18176), 75 },
          },
          {
              { AOM_CDF4(29459, 32607, 32707), 76 },
              { AOM_CDF4(13622, 30904, 32518), 93 },
          },
          {
              { AOM_CDF4(16107, 29457, 31969), 76 },
              { AOM_CDF4(1401, 26035, 31304), 76 },
          },
          {
              { AOM_CDF4(9492, 21931, 27503), 5 },
              { AOM_CDF4(1600, 17846, 26132), 0 },
          },
          {
              { AOM_CDF4(2697, 10231, 16133), 0 },
              { AOM_CDF4(1134, 7676, 14766), 0 },
          },
          {
              { AOM_CDF4(31674, 32735, 32748), 75 },
              { AOM_CDF4(25320, 32583, 32741), 0 },
          },
          {
              { AOM_CDF4(22036, 32048, 32660), 76 },
              { AOM_CDF4(10344, 31219, 32599), 75 },
          },
          {
              { AOM_CDF4(8513, 22787, 30053), 10 },
              { AOM_CDF4(2377, 18779, 28564), 25 },
          },
          {
              { AOM_CDF4(964, 9638, 16384), 0 },
              { AOM_CDF4(2979, 6703, 14150), 0 },
          },
      },
      {
          {
              { AOM_CDF4(29687, 32608, 32743), 30 },
              { AOM_CDF4(14502, 30461, 32602), 0 },
          },
          {
              { AOM_CDF4(12517, 29242, 32221), 1 },
              { AOM_CDF4(958, 25057, 31420), 31 },
          },
          {
              { AOM_CDF4(9068, 22501, 28656), 31 },
              { AOM_CDF4(3312, 18147, 26761), 6 },
          },
          {
              { AOM_CDF4(6851, 17268, 25297), 15 },
              { AOM_CDF4(3104, 14112, 22880), 45 },
          },
          {
              { AOM_CDF4(28343, 32584, 32730), 32 },
              { AOM_CDF4(11232, 29648, 32466), 0 },
          },
          {
              { AOM_CDF4(11161, 28132, 32031), 76 },
              { AOM_CDF4(871, 22915, 30936), 76 },
          },
          {
              { AOM_CDF4(6287, 20156, 27209), 44 },
              { AOM_CDF4(2120, 14990, 24159), 7 },
          },
          {
              { AOM_CDF4(5142, 15731, 22040), 10 },
              { AOM_CDF4(2535, 11465, 19181), 0 },
          },
          {
              { AOM_CDF4(20480, 24576, 28672), 50 },
              { AOM_CDF4(18204, 25486, 29127), 100 },
          },
          {
              { AOM_CDF4(10923, 21845, 27307), 0 },
              { AOM_CDF4(13107, 22938, 29491), 0 },
          },
          {
              { AOM_CDF4(6554, 19661, 26214), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
          },
          {
              { AOM_CDF4(8192, 16384, 24576), 0 },
              { AOM_CDF4(8192, 16384, 24576), 0 },
          },
      },
    };
#endif

static const aom_cdf_prob av1_default_coeff_lps_multi_uv_cdfs
    [TOKEN_CDF_Q_CTXS][LEVEL_CONTEXTS_UV][CDF_SIZE(BR_CDF_SIZE)] = {
      {
          { AOM_CDF4(24100, 29563, 31289), 6 },
          { AOM_CDF4(22448, 28885, 31071), 93 },
          { AOM_CDF4(17701, 25572, 29153), 93 },
          { AOM_CDF4(10401, 17438, 22074), 91 },
      },
      {
          { AOM_CDF4(24628, 29706, 31324), 90 },
          { AOM_CDF4(22478, 28819, 31013), 123 },
          { AOM_CDF4(17451, 25258, 28880), 118 },
          { AOM_CDF4(9999, 16711, 21246), 75 },
      },
      {
          { AOM_CDF4(25969, 30708, 31931), 90 },
          { AOM_CDF4(22739, 29115, 31272), 118 },
          { AOM_CDF4(17098, 24923, 28772), 115 },
          { AOM_CDF4(10475, 17482, 22279), 75 },
      },
      {
          { AOM_CDF4(28465, 31762, 32366), 0 },
          { AOM_CDF4(25610, 30882, 32100), 23 },
          { AOM_CDF4(19724, 27881, 31089), 10 },
          { AOM_CDF4(15206, 23453, 28302), 120 },
      },
    };

static const aom_cdf_prob av1_default_coeff_base_lf_eob_multi_uv_cdfs
    [TOKEN_CDF_Q_CTXS][SIG_COEF_CONTEXTS_EOB][CDF_SIZE(LF_BASE_SYMBOLS - 1)] = {
      {
          { AOM_CDF5(27581, 31285, 32137, 32389), 75 },
          { AOM_CDF5(32091, 32608, 32696, 32731), 109 },
          { AOM_CDF5(31701, 32617, 32710, 32728), 0 },
          { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
      },
      {
          { AOM_CDF5(29919, 32176, 32482, 32582), 75 },
          { AOM_CDF5(31710, 32521, 32675, 32720), 95 },
          { AOM_CDF5(31097, 32475, 32679, 32721), 20 },
          { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
      },
      {
          { AOM_CDF5(29814, 32132, 32501, 32588), 75 },
          { AOM_CDF5(31625, 32617, 32733, 32740), 78 },
          { AOM_CDF5(30527, 32340, 32673, 32740), 24 },
          { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
      },
      {
          { AOM_CDF5(31131, 32506, 32661, 32703), 100 },
          { AOM_CDF5(31161, 32411, 32589, 32679), 100 },
          { AOM_CDF5(28744, 31043, 31618, 32193), 0 },
          { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
      },
    };

static const aom_cdf_prob av1_default_coeff_base_eob_multi_uv_cdfs
    [TOKEN_CDF_Q_CTXS][SIG_COEF_CONTEXTS_EOB][CDF_SIZE(NUM_BASE_LEVELS + 1)] = {
      {
          { AOM_CDF3(10923, 21845), 0 },
          { AOM_CDF3(31158, 32513), 118 },
          { AOM_CDF3(32171, 32661), 118 },
          { AOM_CDF3(32155, 32640), 93 },
      },
      {
          { AOM_CDF3(10923, 21845), 0 },
          { AOM_CDF3(32236, 32675), 118 },
          { AOM_CDF3(32459, 32721), 123 },
          { AOM_CDF3(31994, 32604), 93 },
      },
      {
          { AOM_CDF3(10923, 21845), 0 },
          { AOM_CDF3(31906, 32644), 118 },
          { AOM_CDF3(32421, 32725), 123 },
          { AOM_CDF3(32277, 32670), 118 },
      },
      {
          { AOM_CDF3(10923, 21845), 0 },
          { AOM_CDF3(32163, 32714), 118 },
          { AOM_CDF3(32565, 32747), 75 },
          { AOM_CDF3(32568, 32742), 24 },
      },
    };

static const aom_cdf_prob av1_default_coeff_base_lf_multi_cdfs
    [TOKEN_CDF_Q_CTXS][TX_SIZES][LF_SIG_COEF_CONTEXTS][TCQ_CTXS]
    [CDF_SIZE(LF_BASE_SYMBOLS)] = {
      {
          {
              {
                  { AOM_CDF6(7282, 10923, 18204, 23666, 29127), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(1955, 18697, 24078, 28839, 30736), 44 },
                  { AOM_CDF6(6, 9227, 22263, 28518, 30557), 98 },
              },
              {
                  { AOM_CDF6(7835, 16968, 21861, 27114, 29363), 4 },
                  { AOM_CDF6(9, 6957, 16088, 22518, 28393), 84 },
              },
              {
                  { AOM_CDF6(5157, 8164, 14904, 19714, 23273), 92 },
                  { AOM_CDF6(8, 4732, 13020, 18204, 22882), 2 },
              },
              {
                  { AOM_CDF6(2795, 6758, 11364, 16087, 20674), 23 },
                  { AOM_CDF6(10, 3902, 8690, 11447, 16125), 20 },
              },
              {
                  { AOM_CDF6(1928, 5512, 7680, 10087, 13715), 78 },
                  { AOM_CDF6(11, 3119, 8236, 10667, 11844), 97 },
              },
              {
                  { AOM_CDF6(2035, 4686, 7775, 10757, 14165), 94 },
                  { AOM_CDF6(12, 2403, 5388, 7838, 10348), 95 },
              },
              {
                  { AOM_CDF6(1322, 4348, 7063, 9827, 10565), 18 },
                  { AOM_CDF6(151, 1274, 3058, 5560, 8247), 16 },
              },
              {
                  { AOM_CDF6(643, 2439, 3414, 4877, 5919), 99 },
                  { AOM_CDF6(1138, 1142, 2734, 3730, 4758), 115 },
              },
              {
                  { AOM_CDF6(10990, 28116, 30763, 31404, 32126), 104 },
                  { AOM_CDF6(7068, 26746, 32023, 32528, 32716), 75 },
              },
              {
                  { AOM_CDF6(8845, 25337, 30248, 31904, 32397), 124 },
                  { AOM_CDF6(15, 20627, 29234, 31931, 32502), 91 },
              },
              {
                  { AOM_CDF6(6411, 18962, 26813, 29869, 31359), 78 },
                  { AOM_CDF6(23, 12346, 22225, 27510, 30164), 0 },
              },
              {
                  { AOM_CDF6(5081, 13090, 21074, 26248, 29384), 123 },
                  { AOM_CDF6(82, 8726, 16175, 23036, 27738), 95 },
              },
              {
                  { AOM_CDF6(3117, 9688, 15836, 21005, 24720), 94 },
                  { AOM_CDF6(11, 6210, 13037, 18966, 23068), 78 },
              },
              {
                  { AOM_CDF6(2506, 7911, 13032, 18068, 22090), 118 },
                  { AOM_CDF6(84, 5350, 10419, 15440, 19527), 90 },
              },
              {
                  { AOM_CDF6(1616, 4740, 7525, 10555, 12671), 103 },
                  { AOM_CDF6(287, 2686, 5588, 8468, 11146), 103 },
              },
              {
                  { AOM_CDF6(17804, 30182, 32030, 32529, 32621), 118 },
                  { AOM_CDF6(7337, 28224, 32292, 32536, 32671), 98 },
              },
              {
                  { AOM_CDF6(10697, 24858, 29738, 31486, 32180), 123 },
                  { AOM_CDF6(1550, 18695, 27807, 31024, 31954), 120 },
              },
              {
                  { AOM_CDF6(6541, 17579, 24225, 28482, 30506), 123 },
                  { AOM_CDF6(1302, 11607, 20979, 26303, 29208), 115 },
              },
              {
                  { AOM_CDF6(4016, 12357, 19149, 24303, 27411), 115 },
                  { AOM_CDF6(1211, 8231, 16090, 22155, 26456), 118 },
              },
              {
                  { AOM_CDF6(2484, 7270, 11890, 15920, 19658), 123 },
                  { AOM_CDF6(805, 5094, 9946, 14452, 18092), 123 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(18832, 29501, 31412, 32019, 32234), 5 },
                  { AOM_CDF6(18832, 29501, 31412, 32019, 32234), 5 },
              },
              {
                  { AOM_CDF6(12192, 25324, 29644, 31121, 31692), 90 },
                  { AOM_CDF6(12192, 25324, 29644, 31121, 31692), 90 },
              },
              {
                  { AOM_CDF6(7297, 18053, 24620, 28179, 29856), 75 },
                  { AOM_CDF6(7297, 18053, 24620, 28179, 29856), 75 },
              },
              {
                  { AOM_CDF6(6351, 14261, 20044, 23900, 26371), 75 },
                  { AOM_CDF6(6351, 14261, 20044, 23900, 26371), 75 },
              },
              {
                  { AOM_CDF6(3044, 8507, 13554, 17700, 20847), 75 },
                  { AOM_CDF6(3044, 8507, 13554, 17700, 20847), 75 },
              },
              {
                  { AOM_CDF6(2125, 5665, 9347, 12830, 15909), 100 },
                  { AOM_CDF6(2125, 5665, 9347, 12830, 15909), 100 },
              },
              {
                  { AOM_CDF6(1310, 3647, 6103, 8479, 10724), 124 },
                  { AOM_CDF6(1310, 3647, 6103, 8479, 10724), 124 },
              },
              {
                  { AOM_CDF6(24974, 32003, 32552, 32665, 32700), 0 },
                  { AOM_CDF6(24974, 32003, 32552, 32665, 32700), 0 },
              },
              {
                  { AOM_CDF6(14816, 26797, 30763, 31884, 32258), 90 },
                  { AOM_CDF6(14816, 26797, 30763, 31884, 32258), 90 },
              },
              {
                  { AOM_CDF6(7599, 17322, 23806, 27634, 29698), 75 },
                  { AOM_CDF6(7599, 17322, 23806, 27634, 29698), 75 },
              },
              {
                  { AOM_CDF6(7103, 14637, 20025, 23706, 26319), 90 },
                  { AOM_CDF6(7103, 14637, 20025, 23706, 26319), 90 },
              },
              {
                  { AOM_CDF6(2520, 6946, 11110, 14689, 17772), 93 },
                  { AOM_CDF6(2520, 6946, 11110, 14689, 17772), 93 },
              },
#else
              {
                  { AOM_CDF6(17312, 29865, 31400, 32310, 32333), 115 },
                  { AOM_CDF6(9059, 24120, 30505, 31622, 32224), 20 },
              },
              {
                  { AOM_CDF6(8890, 23559, 28930, 30702, 31447), 93 },
                  { AOM_CDF6(2466, 19837, 28050, 30428, 31304), 1 },
              },
              {
                  { AOM_CDF6(6021, 17544, 23564, 27333, 29227), 90 },
                  { AOM_CDF6(116, 13107, 21507, 26208, 28829), 78 },
              },
              {
                  { AOM_CDF6(4708, 12836, 19068, 22531, 26113), 115 },
                  { AOM_CDF6(249, 7883, 16650, 21077, 24077), 4 },
              },
              {
                  { AOM_CDF6(3122, 8313, 12624, 16130, 19351), 0 },
                  { AOM_CDF6(12, 5653, 10338, 14732, 18787), 16 },
              },
              {
                  { AOM_CDF6(2212, 6438, 10755, 13039, 15017), 24 },
                  { AOM_CDF6(647, 1783, 6798, 9804, 12688), 0 },
              },
              {
                  { AOM_CDF6(1068, 2303, 4181, 6253, 7565), 16 },
                  { AOM_CDF6(25, 1816, 4259, 6664, 9119), 75 },
              },
              {
                  { AOM_CDF6(25079, 32114, 32675, 32697, 32703), 91 },
                  { AOM_CDF6(21437, 30387, 32203, 32448, 32555), 98 },
              },
              {
                  { AOM_CDF6(10955, 25219, 29637, 31227, 31990), 124 },
                  { AOM_CDF6(7834, 23802, 29704, 31516, 32162), 90 },
              },
              {
                  { AOM_CDF6(6329, 15833, 22432, 27323, 29450), 118 },
                  { AOM_CDF6(2979, 11375, 20204, 25398, 28130), 123 },
              },
              {
                  { AOM_CDF6(5140, 12640, 17600, 22235, 25437), 98 },
                  { AOM_CDF6(1803, 9333, 17400, 21901, 25276), 91 },
              },
              {
                  { AOM_CDF6(1771, 6054, 10829, 14775, 17959), 78 },
                  { AOM_CDF6(752, 4271, 8403, 12906, 15881), 75 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(3674, 28376, 30234, 31079, 31881), 31 },
                  { AOM_CDF6(4681, 9362, 18725, 23406, 28087), 0 },
              },
              {
                  { AOM_CDF6(4218, 19173, 28003, 30478, 32201), 18 },
                  { AOM_CDF6(4, 9148, 20819, 27374, 30428), 78 },
              },
              {
                  { AOM_CDF6(13420, 23080, 27194, 29702, 31274), 76 },
                  { AOM_CDF6(4, 10769, 22591, 26946, 28756), 0 },
              },
              {
                  { AOM_CDF6(5988, 13144, 19557, 24128, 26546), 76 },
                  { AOM_CDF6(4, 5437, 13509, 19391, 23435), 80 },
              },
              {
                  { AOM_CDF6(2722, 8736, 12093, 16578, 20511), 1 },
                  { AOM_CDF6(4, 5311, 11403, 16009, 19376), 93 },
              },
              {
                  { AOM_CDF6(2209, 7039, 11042, 14142, 16420), 75 },
                  { AOM_CDF6(4, 4341, 7785, 12565, 16556), 124 },
              },
              {
                  { AOM_CDF6(903, 4080, 8196, 10264, 13828), 96 },
                  { AOM_CDF6(2380, 2396, 6345, 9136, 12272), 95 },
              },
              {
                  { AOM_CDF6(1026, 4730, 7747, 10065, 12225), 75 },
                  { AOM_CDF6(4, 3608, 5373, 7862, 10619), 79 },
              },
              {
                  { AOM_CDF6(418, 2529, 3772, 5111, 6595), 123 },
                  { AOM_CDF6(1332, 1336, 2811, 4603, 6006), 98 },
              },
              {
                  { AOM_CDF6(18029, 31174, 32628, 32640, 32679), 80 },
                  { AOM_CDF6(18190, 27990, 32080, 32651, 32732), 104 },
              },
              {
                  { AOM_CDF6(14619, 28632, 31831, 32398, 32631), 18 },
                  { AOM_CDF6(10, 24392, 30550, 32214, 32579), 80 },
              },
              {
                  { AOM_CDF6(12651, 25180, 29580, 31542, 32302), 75 },
                  { AOM_CDF6(19, 19007, 27434, 30532, 31670), 90 },
              },
              {
                  { AOM_CDF6(6287, 17882, 24305, 28365, 30348), 93 },
                  { AOM_CDF6(44, 12345, 21056, 26679, 29581), 118 },
              },
              {
                  { AOM_CDF6(4267, 12266, 18835, 23840, 27226), 123 },
                  { AOM_CDF6(9, 7954, 15525, 21492, 25443), 90 },
              },
              {
                  { AOM_CDF6(3266, 10180, 15955, 20692, 24207), 75 },
                  { AOM_CDF6(235, 5498, 11616, 17368, 21778), 79 },
              },
              {
                  { AOM_CDF6(1920, 5749, 9013, 12069, 14299), 103 },
                  { AOM_CDF6(100, 3140, 6771, 10152, 13109), 78 },
              },
              {
                  { AOM_CDF6(25232, 32327, 32622, 32735, 32739), 103 },
                  { AOM_CDF6(9286, 31105, 32603, 32734, 32738), 90 },
              },
              {
                  { AOM_CDF6(17625, 30012, 32144, 32560, 32624), 118 },
                  { AOM_CDF6(157, 26669, 31834, 32538, 32613), 75 },
              },
              {
                  { AOM_CDF6(12336, 26106, 30658, 32091, 32450), 115 },
                  { AOM_CDF6(205, 20658, 28929, 31522, 32265), 100 },
              },
              {
                  { AOM_CDF6(7103, 18979, 26355, 29947, 31502), 118 },
                  { AOM_CDF6(519, 14086, 23524, 28447, 30906), 93 },
              },
              {
                  { AOM_CDF6(3275, 9601, 15013, 19461, 22814), 93 },
                  { AOM_CDF6(405, 6336, 12573, 17560, 21302), 90 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(26290, 32021, 32468, 32597, 32646), 0 },
                  { AOM_CDF6(26290, 32021, 32468, 32597, 32646), 0 },
              },
              {
                  { AOM_CDF6(16555, 29135, 31552, 32191, 32400), 90 },
                  { AOM_CDF6(16555, 29135, 31552, 32191, 32400), 90 },
              },
              {
                  { AOM_CDF6(10084, 22847, 28258, 30456, 31389), 75 },
                  { AOM_CDF6(10084, 22847, 28258, 30456, 31389), 75 },
              },
              {
                  { AOM_CDF6(6977, 16792, 22984, 26486, 28437), 1 },
                  { AOM_CDF6(6977, 16792, 22984, 26486, 28437), 1 },
              },
              {
                  { AOM_CDF6(4056, 11507, 17511, 21910, 24827), 75 },
                  { AOM_CDF6(4056, 11507, 17511, 21910, 24827), 75 },
              },
              {
                  { AOM_CDF6(2994, 8463, 13359, 17473, 20660), 90 },
                  { AOM_CDF6(2994, 8463, 13359, 17473, 20660), 90 },
              },
              {
                  { AOM_CDF6(1685, 4842, 8021, 10948, 13523), 93 },
                  { AOM_CDF6(1685, 4842, 8021, 10948, 13523), 93 },
              },
              {
                  { AOM_CDF6(28016, 32504, 32702, 32736, 32740), 0 },
                  { AOM_CDF6(28016, 32504, 32702, 32736, 32740), 0 },
              },
              {
                  { AOM_CDF6(18602, 30203, 32140, 32521, 32633), 103 },
                  { AOM_CDF6(18602, 30203, 32140, 32521, 32633), 103 },
              },
              {
                  { AOM_CDF6(11272, 24591, 29571, 31334, 31982), 75 },
                  { AOM_CDF6(11272, 24591, 29571, 31334, 31982), 75 },
              },
              {
                  { AOM_CDF6(8093, 19452, 25731, 28891, 30469), 75 },
                  { AOM_CDF6(8093, 19452, 25731, 28891, 30469), 75 },
              },
              {
                  { AOM_CDF6(4041, 11262, 17038, 21210, 24054), 75 },
                  { AOM_CDF6(4041, 11262, 17038, 21210, 24054), 75 },
              },
#else
              {
                  { AOM_CDF6(26657, 32344, 32640, 32705, 32712), 116 },
                  { AOM_CDF6(22362, 29644, 32028, 32464, 32585), 118 },
              },
              {
                  { AOM_CDF6(18521, 29552, 31754, 32275, 32399), 78 },
                  { AOM_CDF6(248, 26290, 31190, 32053, 32407), 90 },
              },
              {
                  { AOM_CDF6(11819, 23810, 28577, 30723, 31550), 75 },
                  { AOM_CDF6(39, 18396, 26801, 29964, 31206), 15 },
              },
              {
                  { AOM_CDF6(6859, 17565, 23984, 27678, 29487), 100 },
                  { AOM_CDF6(191, 12833, 21666, 26175, 28596), 0 },
              },
              {
                  { AOM_CDF6(4397, 12650, 18866, 22764, 25794), 75 },
                  { AOM_CDF6(326, 7169, 16146, 21374, 24764), 15 },
              },
              {
                  { AOM_CDF6(3768, 10180, 15127, 19145, 21660), 18 },
                  { AOM_CDF6(37, 4767, 10770, 16196, 19573), 0 },
              },
              {
                  { AOM_CDF6(1511, 4441, 8143, 10504, 12649), 75 },
                  { AOM_CDF6(387, 3037, 6815, 9343, 12612), 1 },
              },
              {
                  { AOM_CDF6(28345, 32569, 32740, 32744, 32748), 90 },
                  { AOM_CDF6(26270, 31831, 32584, 32695, 32732), 78 },
              },
              {
                  { AOM_CDF6(20171, 30411, 32018, 32429, 32658), 95 },
                  { AOM_CDF6(5055, 27653, 31630, 32475, 32591), 103 },
              },
              {
                  { AOM_CDF6(11500, 24689, 29434, 31200, 31999), 93 },
                  { AOM_CDF6(1893, 21515, 28730, 31212, 31974), 15 },
              },
              {
                  { AOM_CDF6(8336, 21083, 26785, 29298, 30904), 90 },
                  { AOM_CDF6(1060, 15511, 23468, 27782, 29862), 100 },
              },
              {
                  { AOM_CDF6(3657, 11549, 17275, 21645, 24491), 75 },
                  { AOM_CDF6(1136, 8244, 15202, 20041, 23136), 90 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(15215, 28295, 31182, 31952, 32227), 31 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(2502, 25172, 28818, 31401, 31613), 0 },
                  { AOM_CDF6(5, 7352, 22161, 27439, 30861), 75 },
              },
              {
                  { AOM_CDF6(9307, 21264, 26430, 29284, 30850), 80 },
                  { AOM_CDF6(4, 14958, 24306, 29325, 29918), 25 },
              },
              {
                  { AOM_CDF6(5074, 12161, 17676, 23252, 26183), 20 },
                  { AOM_CDF6(4, 6186, 14065, 19988, 23883), 90 },
              },
              {
                  { AOM_CDF6(2175, 7267, 11231, 15741, 18805), 120 },
                  { AOM_CDF6(5, 4803, 10396, 13620, 18558), 90 },
              },
              {
                  { AOM_CDF6(1378, 5756, 10363, 13119, 16083), 15 },
                  { AOM_CDF6(3232, 3289, 6479, 10657, 13939), 115 },
              },
              {
                  { AOM_CDF6(1251, 4859, 7421, 11080, 12888), 115 },
                  { AOM_CDF6(9, 3044, 6277, 9195, 11552), 75 },
              },
              {
                  { AOM_CDF6(2037, 3829, 6244, 9131, 11854), 118 },
                  { AOM_CDF6(10, 2174, 5919, 7661, 10248), 120 },
              },
              {
                  { AOM_CDF6(616, 1994, 3264, 4556, 5808), 0 },
                  { AOM_CDF6(4, 1476, 2889, 4727, 5675), 93 },
              },
              {
                  { AOM_CDF6(28513, 32606, 32717, 32736, 32740), 0 },
                  { AOM_CDF6(22586, 29480, 32095, 32634, 32732), 78 },
              },
              {
                  { AOM_CDF6(16528, 28513, 31318, 32332, 32621), 93 },
                  { AOM_CDF6(6, 25278, 31129, 32475, 32536), 1 },
              },
              {
                  { AOM_CDF6(15476, 27147, 30597, 31886, 32347), 75 },
                  { AOM_CDF6(4, 24085, 30024, 31588, 32211), 1 },
              },
              {
                  { AOM_CDF6(6360, 17104, 23709, 27776, 29780), 78 },
                  { AOM_CDF6(51, 14017, 22559, 27207, 29739), 18 },
              },
              {
                  { AOM_CDF6(3389, 11324, 18471, 23526, 27122), 80 },
                  { AOM_CDF6(92, 8494, 15658, 20530, 24456), 100 },
              },
              {
                  { AOM_CDF6(3355, 9739, 15158, 19770, 23421), 90 },
                  { AOM_CDF6(186, 6187, 12550, 17514, 21104), 0 },
              },
              {
                  { AOM_CDF6(1419, 4731, 7906, 11060, 13556), 123 },
                  { AOM_CDF6(115, 3565, 6660, 9709, 12459), 98 },
              },
              {
                  { AOM_CDF6(28957, 32607, 32740, 32744, 32748), 108 },
                  { AOM_CDF6(8712, 31404, 32675, 32736, 32740), 95 },
              },
              {
                  { AOM_CDF6(22698, 31491, 32506, 32682, 32719), 93 },
                  { AOM_CDF6(726, 29031, 32119, 32598, 32718), 15 },
              },
              {
                  { AOM_CDF6(18134, 29812, 32029, 32607, 32670), 75 },
                  { AOM_CDF6(703, 25979, 31114, 32274, 32561), 90 },
              },
              {
                  { AOM_CDF6(9071, 21913, 28115, 30916, 32011), 118 },
                  { AOM_CDF6(543, 17308, 26110, 30078, 31789), 75 },
              },
              {
                  { AOM_CDF6(3003, 9500, 14783, 18864, 22122), 93 },
                  { AOM_CDF6(929, 6621, 12508, 17355, 20822), 78 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(29917, 32597, 32714, 32736, 32740), 75 },
                  { AOM_CDF6(29917, 32597, 32714, 32736, 32740), 75 },
              },
              {
                  { AOM_CDF6(19705, 30884, 32225, 32522, 32634), 75 },
                  { AOM_CDF6(19705, 30884, 32225, 32522, 32634), 75 },
              },
              {
                  { AOM_CDF6(11771, 25242, 29797, 31373, 32032), 6 },
                  { AOM_CDF6(11771, 25242, 29797, 31373, 32032), 6 },
              },
              {
                  { AOM_CDF6(7884, 18463, 24730, 28017, 29624), 31 },
                  { AOM_CDF6(7884, 18463, 24730, 28017, 29624), 31 },
              },
              {
                  { AOM_CDF6(4570, 13155, 19787, 24244, 27010), 46 },
                  { AOM_CDF6(4570, 13155, 19787, 24244, 27010), 46 },
              },
              {
                  { AOM_CDF6(3458, 10299, 16096, 20513, 23626), 9 },
                  { AOM_CDF6(3458, 10299, 16096, 20513, 23626), 9 },
              },
              {
                  { AOM_CDF6(2441, 6850, 11016, 14883, 17913), 22 },
                  { AOM_CDF6(2441, 6850, 11016, 14883, 17913), 22 },
              },
              {
                  { AOM_CDF6(30912, 32721, 32740, 32744, 32748), 85 },
                  { AOM_CDF6(30912, 32721, 32740, 32744, 32748), 85 },
              },
              {
                  { AOM_CDF6(22569, 31800, 32564, 32703, 32732), 75 },
                  { AOM_CDF6(22569, 31800, 32564, 32703, 32732), 75 },
              },
              {
                  { AOM_CDF6(13855, 27736, 31221, 32193, 32506), 5 },
                  { AOM_CDF6(13855, 27736, 31221, 32193, 32506), 5 },
              },
              {
                  { AOM_CDF6(8984, 21588, 28059, 30514, 31533), 84 },
                  { AOM_CDF6(8984, 21588, 28059, 30514, 31533), 84 },
              },
              {
                  { AOM_CDF6(4850, 13293, 20179, 24556, 27375), 116 },
                  { AOM_CDF6(4850, 13293, 20179, 24556, 27375), 116 },
              },
#else
              {
                  { AOM_CDF6(29977, 32695, 32740, 32744, 32748), 10 },
                  { AOM_CDF6(21325, 31848, 32576, 32728, 32732), 75 },
              },
              {
                  { AOM_CDF6(21678, 31780, 32552, 32683, 32711), 15 },
                  { AOM_CDF6(1678, 28739, 32080, 32494, 32636), 75 },
              },
              {
                  { AOM_CDF6(14416, 27639, 30815, 31836, 32397), 6 },
                  { AOM_CDF6(65, 22409, 29074, 31594, 32165), 16 },
              },
              {
                  { AOM_CDF6(5000, 16239, 22314, 26157, 28595), 94 },
                  { AOM_CDF6(2422, 9954, 20040, 25589, 27879), 2 },
              },
              {
                  { AOM_CDF6(1007, 8723, 14427, 19459, 23709), 20 },
                  { AOM_CDF6(2626, 7142, 13023, 18590, 22896), 100 },
              },
              {
                  { AOM_CDF6(1998, 7393, 13986, 15185, 19381), 0 },
                  { AOM_CDF6(683, 4779, 9102, 13198, 17067), 25 },
              },
              {
                  { AOM_CDF6(1948, 4428, 6731, 7971, 10273), 0 },
                  { AOM_CDF6(643, 2142, 5354, 7710, 9852), 25 },
              },
              {
                  { AOM_CDF6(30838, 32730, 32740, 32744, 32748), 93 },
                  { AOM_CDF6(30861, 32535, 32735, 32739, 32743), 23 },
              },
              {
                  { AOM_CDF6(24490, 32228, 32648, 32709, 32732), 120 },
                  { AOM_CDF6(9047, 31150, 32510, 32703, 32732), 83 },
              },
              {
                  { AOM_CDF6(13617, 29270, 31661, 32175, 32412), 80 },
                  { AOM_CDF6(8027, 25225, 30107, 31822, 32614), 6 },
              },
              {
                  { AOM_CDF6(5680, 20709, 26214, 29185, 30147), 45 },
                  { AOM_CDF6(3843, 13148, 23059, 29026, 30644), 20 },
              },
              {
                  { AOM_CDF6(3641, 10797, 15945, 21469, 24105), 85 },
                  { AOM_CDF6(1536, 10112, 16896, 20864, 23296), 20 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(10142, 17164, 20675, 27307, 30427), 50 },
                  { AOM_CDF6(4681, 9362, 14043, 18725, 28087), 0 },
              },
              {
                  { AOM_CDF6(61, 11168, 17059, 28657, 32707), 50 },
                  { AOM_CDF6(28, 1249, 19727, 26442, 29882), 76 },
              },
              {
                  { AOM_CDF6(2644, 10879, 17814, 24793, 28607), 29 },
                  { AOM_CDF6(4217, 5131, 15546, 17324, 20016), 22 },
              },
              {
                  { AOM_CDF6(2223, 7049, 14034, 18416, 21020), 20 },
                  { AOM_CDF6(335, 7639, 10186, 12263, 19366), 90 },
              },
              {
                  { AOM_CDF6(2535, 4037, 8920, 12300, 14271), 99 },
                  { AOM_CDF6(99, 3762, 8613, 10791, 14850), 10 },
              },
              {
                  { AOM_CDF6(3678, 7468, 9920, 11368, 14489), 110 },
                  { AOM_CDF6(687, 2062, 4354, 7447, 11572), 5 },
              },
              {
                  { AOM_CDF6(1408, 4480, 5632, 10880, 11776), 20 },
                  { AOM_CDF6(1096, 2071, 4994, 8405, 10476), 122 },
              },
              {
                  { AOM_CDF6(1842, 3948, 5790, 7633, 9870), 122 },
                  { AOM_CDF6(405, 1753, 4989, 7012, 9439), 20 },
              },
              {
                  { AOM_CDF6(871, 2062, 3666, 5362, 5637), 77 },
                  { AOM_CDF6(50, 1464, 2323, 2524, 4494), 102 },
              },
              {
                  { AOM_CDF6(20249, 28740, 30264, 32441, 32659), 31 },
                  { AOM_CDF6(11690, 23981, 31016, 32292, 32618), 1 },
              },
              {
                  { AOM_CDF6(12989, 26034, 30598, 31838, 32064), 29 },
                  { AOM_CDF6(28, 25187, 30578, 32403, 32628), 47 },
              },
              {
                  { AOM_CDF6(6901, 20787, 26916, 30007, 31719), 32 },
                  { AOM_CDF6(5092, 17400, 25761, 28400, 29895), 76 },
              },
              {
                  { AOM_CDF6(2531, 16487, 22130, 27022, 28629), 26 },
                  { AOM_CDF6(4149, 10372, 16414, 21074, 25132), 24 },
              },
              {
                  { AOM_CDF6(2382, 10112, 16384, 19641, 22899), 79 },
                  { AOM_CDF6(390, 6281, 12611, 17480, 20936), 104 },
              },
              {
                  { AOM_CDF6(678, 8616, 13976, 17978, 20081), 17 },
                  { AOM_CDF6(4274, 6910, 11255, 16455, 18877), 23 },
              },
              {
                  { AOM_CDF6(1502, 3808, 6999, 9251, 10377), 91 },
                  { AOM_CDF6(26, 2813, 5183, 7945, 11409), 101 },
              },
              {
                  { AOM_CDF6(27035, 32250, 32646, 32736, 32740), 1 },
                  { AOM_CDF6(11097, 31220, 32609, 32734, 32738), 90 },
              },
              {
                  { AOM_CDF6(17752, 29769, 32185, 32669, 32732), 115 },
                  { AOM_CDF6(3856, 27026, 32053, 32579, 32677), 15 },
              },
              {
                  { AOM_CDF6(12754, 26650, 30781, 32132, 32617), 80 },
                  { AOM_CDF6(1600, 21246, 28993, 31136, 32124), 75 },
              },
              {
                  { AOM_CDF6(6728, 19533, 26420, 29925, 31212), 118 },
                  { AOM_CDF6(351, 14070, 23538, 28291, 30423), 103 },
              },
              {
                  { AOM_CDF6(3011, 9025, 14586, 18554, 21656), 76 },
                  { AOM_CDF6(78, 5651, 10946, 15012, 17956), 6 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
          },
          {
              {
                  { AOM_CDF6(9362, 14043, 18725, 23406, 28087), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(2185, 4369, 8738, 15292, 21845), 100 },
                  { AOM_CDF6(1130, 3390, 9039, 13559, 21469), 50 },
              },
              {
                  { AOM_CDF6(2185, 6554, 13107, 14199, 18569), 100 },
                  { AOM_CDF6(3449, 5174, 6899, 10348, 27594), 100 },
              },
              {
                  { AOM_CDF6(1365, 4096, 5461, 10923, 16384), 50 },
                  { AOM_CDF6(1560, 4681, 7802, 15604, 20285), 50 },
              },
              {
                  { AOM_CDF6(1560, 3121, 4681, 7802, 15604), 50 },
                  { AOM_CDF6(1130, 2260, 4520, 7910, 11299), 50 },
              },
              {
                  { AOM_CDF6(1928, 2891, 5783, 7710, 8674), 50 },
                  { AOM_CDF6(1311, 2621, 5243, 14418, 19661), 50 },
              },
              {
                  { AOM_CDF6(780, 1560, 3901, 4681, 7022), 50 },
                  { AOM_CDF6(2341, 3511, 4681, 5851, 7022), 50 },
              },
              {
                  { AOM_CDF6(1820, 7282, 12743, 16384, 20025), 100 },
                  { AOM_CDF6(1425, 2849, 4274, 7123, 8548), 50 },
              },
              {
                  { AOM_CDF6(449, 1347, 2244, 3142, 4040), 110 },
                  { AOM_CDF6(482, 964, 1928, 2891, 3373), 75 },
              },
              {
                  { AOM_CDF6(936, 4681, 10299, 17788, 22469), 0 },
                  { AOM_CDF6(4428, 12842, 21255, 24355, 28783), 0 },
              },
              {
                  { AOM_CDF6(4498, 9638, 12208, 14778, 19275), 25 },
                  { AOM_CDF6(2553, 5532, 10639, 14895, 22555), 25 },
              },
              {
                  { AOM_CDF6(3332, 12774, 14440, 17772, 19994), 25 },
                  { AOM_CDF6(1092, 6554, 10923, 14199, 16930), 0 },
              },
              {
                  { AOM_CDF6(2458, 11469, 15565, 19661, 23757), 50 },
                  { AOM_CDF6(1638, 4915, 7373, 10650, 13107), 25 },
              },
              {
                  { AOM_CDF6(3511, 8192, 14043, 17554, 19895), 50 },
                  { AOM_CDF6(2185, 4369, 6554, 12015, 17476), 100 },
              },
              {
                  { AOM_CDF6(2979, 8937, 10426, 13405, 17873), 25 },
                  { AOM_CDF6(1311, 5243, 9175, 13107, 19661), 50 },
              },
              {
                  { AOM_CDF6(1040, 2601, 4941, 6242, 8062), 20 },
                  { AOM_CDF6(756, 2269, 5293, 5797, 9074), 110 },
              },
              {
                  { AOM_CDF6(25353, 31333, 32050, 32290, 32529), 70 },
                  { AOM_CDF6(11726, 28110, 32125, 32447, 32607), 14 },
              },
              {
                  { AOM_CDF6(12393, 28987, 31298, 32348, 32558), 40 },
                  { AOM_CDF6(7393, 27373, 30171, 31769, 32568), 10 },
              },
              {
                  { AOM_CDF6(7378, 21484, 26258, 29730, 31900), 85 },
                  { AOM_CDF6(723, 21444, 28913, 31563, 32286), 10 },
              },
              {
                  { AOM_CDF6(4930, 16239, 20879, 27548, 29868), 20 },
                  { AOM_CDF6(1977, 13277, 24011, 27401, 29378), 5 },
              },
              {
                  { AOM_CDF6(2690, 9292, 13287, 17036, 19481), 34 },
                  { AOM_CDF6(246, 4024, 8130, 12155, 14700), 19 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF6(2947, 18388, 21099, 27700, 31825), 2 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(756, 15008, 23273, 29032, 30285), 88 },
                  { AOM_CDF6(4, 7259, 20599, 27428, 30600), 1 },
              },
              {
                  { AOM_CDF6(4695, 13169, 20868, 24921, 28311), 8 },
                  { AOM_CDF6(4, 9798, 16603, 22587, 26476), 1 },
              },
              {
                  { AOM_CDF6(3221, 9129, 14448, 19583, 24130), 4 },
                  { AOM_CDF6(4, 4501, 11838, 17403, 21164), 1 },
              },
              {
                  { AOM_CDF6(2791, 7158, 10806, 14341, 17984), 19 },
                  { AOM_CDF6(4, 4423, 8397, 13102, 17087), 91 },
              },
              {
                  { AOM_CDF6(2685, 5673, 9265, 11927, 14846), 19 },
                  { AOM_CDF6(4, 2421, 6144, 9109, 12397), 19 },
              },
              {
                  { AOM_CDF6(1450, 4907, 7311, 9891, 11822), 23 },
                  { AOM_CDF6(4, 1467, 3132, 5890, 7666), 22 },
              },
              {
                  { AOM_CDF6(1135, 3330, 5373, 7739, 9743), 84 },
                  { AOM_CDF6(4, 2325, 3989, 6202, 8184), 94 },
              },
              {
                  { AOM_CDF6(892, 2071, 3006, 3984, 4800), 78 },
                  { AOM_CDF6(95, 884, 1687, 3182, 4291), 75 },
              },
              {
                  { AOM_CDF6(12403, 25476, 30103, 31427, 31884), 30 },
                  { AOM_CDF6(5167, 24213, 31202, 32123, 32463), 76 },
              },
              {
                  { AOM_CDF6(9196, 22862, 28575, 31046, 31934), 90 },
                  { AOM_CDF6(73, 15774, 25750, 30059, 31553), 15 },
              },
              {
                  { AOM_CDF6(6375, 16967, 24241, 28011, 30154), 90 },
                  { AOM_CDF6(4, 12430, 21155, 26323, 29811), 5 },
              },
              {
                  { AOM_CDF6(4535, 12980, 20317, 25184, 28292), 78 },
                  { AOM_CDF6(4, 8915, 15813, 21574, 25751), 0 },
              },
              {
                  { AOM_CDF6(3419, 9878, 15651, 20421, 23868), 0 },
                  { AOM_CDF6(4, 5590, 11934, 17096, 21448), 90 },
              },
              {
                  { AOM_CDF6(2478, 7338, 12377, 16642, 20196), 78 },
                  { AOM_CDF6(4, 5105, 9717, 14406, 18541), 84 },
              },
              {
                  { AOM_CDF6(1478, 3950, 6445, 9101, 11093), 93 },
                  { AOM_CDF6(195, 2542, 5339, 7707, 10228), 4 },
              },
              {
                  { AOM_CDF6(19165, 29449, 31476, 32322, 32530), 0 },
                  { AOM_CDF6(9431, 27119, 31298, 32276, 32536), 78 },
              },
              {
                  { AOM_CDF6(11722, 25318, 30038, 31734, 32158), 93 },
                  { AOM_CDF6(247, 19830, 28268, 31139, 32057), 75 },
              },
              {
                  { AOM_CDF6(7270, 18135, 25035, 28810, 30789), 78 },
                  { AOM_CDF6(257, 12726, 22288, 27538, 30267), 75 },
              },
              {
                  { AOM_CDF6(4609, 13199, 19805, 24949, 28220), 78 },
                  { AOM_CDF6(234, 8745, 16846, 22880, 26827), 78 },
              },
              {
                  { AOM_CDF6(2487, 7388, 11938, 16081, 19636), 79 },
                  { AOM_CDF6(632, 4523, 9442, 13865, 17726), 78 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(19957, 30881, 32093, 32401, 32499), 75 },
                  { AOM_CDF6(19957, 30881, 32093, 32401, 32499), 75 },
              },
              {
                  { AOM_CDF6(13862, 27524, 30931, 31847, 32161), 118 },
                  { AOM_CDF6(13862, 27524, 30931, 31847, 32161), 118 },
              },
              {
                  { AOM_CDF6(7951, 19522, 26221, 29500, 30783), 75 },
                  { AOM_CDF6(7951, 19522, 26221, 29500, 30783), 75 },
              },
              {
                  { AOM_CDF6(6740, 15489, 21710, 25768, 27994), 75 },
                  { AOM_CDF6(6740, 15489, 21710, 25768, 27994), 75 },
              },
              {
                  { AOM_CDF6(3517, 9766, 15604, 20263, 23629), 78 },
                  { AOM_CDF6(3517, 9766, 15604, 20263, 23629), 78 },
              },
              {
                  { AOM_CDF6(2514, 6923, 11453, 15483, 18838), 90 },
                  { AOM_CDF6(2514, 6923, 11453, 15483, 18838), 90 },
              },
              {
                  { AOM_CDF6(1575, 4396, 7421, 10439, 13122), 115 },
                  { AOM_CDF6(1575, 4396, 7421, 10439, 13122), 115 },
              },
              {
                  { AOM_CDF6(26753, 32413, 32653, 32715, 32732), 75 },
                  { AOM_CDF6(26753, 32413, 32653, 32715, 32732), 75 },
              },
              {
                  { AOM_CDF6(16486, 27781, 31508, 32297, 32527), 90 },
                  { AOM_CDF6(16486, 27781, 31508, 32297, 32527), 90 },
              },
              {
                  { AOM_CDF6(8468, 18731, 25262, 29033, 30854), 75 },
                  { AOM_CDF6(8468, 18731, 25262, 29033, 30854), 75 },
              },
              {
                  { AOM_CDF6(7679, 16687, 22428, 25871, 28305), 93 },
                  { AOM_CDF6(7679, 16687, 22428, 25871, 28305), 93 },
              },
              {
                  { AOM_CDF6(3330, 9195, 14353, 18336, 21471), 78 },
                  { AOM_CDF6(3330, 9195, 14353, 18336, 21471), 78 },
              },
#else
              {
                  { AOM_CDF6(17727, 29311, 31495, 32308, 32394), 75 },
                  { AOM_CDF6(6217, 23353, 28895, 31068, 31825), 75 },
              },
              {
                  { AOM_CDF6(9075, 24491, 29345, 30994, 31581), 118 },
                  { AOM_CDF6(218, 21239, 28414, 30785, 31602), 75 },
              },
              {
                  { AOM_CDF6(5781, 16806, 23722, 27738, 29671), 75 },
                  { AOM_CDF6(4, 12202, 21149, 26109, 28706), 76 },
              },
              {
                  { AOM_CDF6(4397, 13491, 19488, 23738, 26522), 90 },
                  { AOM_CDF6(9, 7739, 14408, 20401, 24205), 15 },
              },
              {
                  { AOM_CDF6(2741, 7891, 13107, 17895, 21406), 79 },
                  { AOM_CDF6(4, 5445, 11477, 16520, 19991), 84 },
              },
              {
                  { AOM_CDF6(1672, 5245, 9460, 12808, 16167), 104 },
                  { AOM_CDF6(13, 4369, 7553, 10464, 13955), 19 },
              },
              {
                  { AOM_CDF6(1259, 3754, 5425, 7792, 9815), 94 },
                  { AOM_CDF6(16, 2165, 4382, 6970, 9593), 79 },
              },
              {
                  { AOM_CDF6(24047, 32183, 32609, 32707, 32717), 3 },
                  { AOM_CDF6(19625, 31025, 32478, 32666, 32681), 8 },
              },
              {
                  { AOM_CDF6(9651, 24655, 29703, 31578, 32209), 95 },
                  { AOM_CDF6(7022, 22403, 29542, 31430, 32077), 78 },
              },
              {
                  { AOM_CDF6(5474, 15286, 22650, 27069, 29481), 115 },
                  { AOM_CDF6(966, 11142, 19980, 25083, 28338), 96 },
              },
              {
                  { AOM_CDF6(4079, 12609, 18456, 22819, 26271), 94 },
                  { AOM_CDF6(735, 8611, 15581, 20142, 23456), 76 },
              },
              {
                  { AOM_CDF6(2019, 6499, 11312, 15078, 17871), 79 },
                  { AOM_CDF6(480, 4349, 8920, 12345, 16045), 83 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(7358, 29088, 29585, 31765, 32518), 0 },
                  { AOM_CDF6(1092, 17112, 21481, 28763, 30583), 31 },
              },
              {
                  { AOM_CDF6(1799, 16656, 25904, 30116, 31674), 78 },
                  { AOM_CDF6(4, 7885, 20364, 27638, 30570), 75 },
              },
              {
                  { AOM_CDF6(8383, 19222, 25541, 28760, 30756), 15 },
                  { AOM_CDF6(4, 13657, 23005, 27761, 29683), 1 },
              },
              {
                  { AOM_CDF6(5483, 13262, 19306, 24089, 27356), 75 },
                  { AOM_CDF6(4, 7556, 15634, 21287, 25136), 0 },
              },
              {
                  { AOM_CDF6(3362, 9344, 13979, 18597, 22100), 75 },
                  { AOM_CDF6(4, 5493, 10754, 15690, 19620), 18 },
              },
              {
                  { AOM_CDF6(2299, 6575, 10927, 14213, 17740), 99 },
                  { AOM_CDF6(4, 4081, 8334, 12017, 15584), 79 },
              },
              {
                  { AOM_CDF6(1779, 5356, 7662, 10623, 13170), 91 },
                  { AOM_CDF6(1129, 1577, 5568, 8753, 11950), 78 },
              },
              {
                  { AOM_CDF6(1657, 4299, 6919, 9362, 11822), 79 },
                  { AOM_CDF6(4, 2904, 5784, 8606, 10987), 99 },
              },
              {
                  { AOM_CDF6(901, 2370, 3694, 5110, 6390), 98 },
                  { AOM_CDF6(26, 1578, 3057, 4546, 6033), 118 },
              },
              {
                  { AOM_CDF6(18197, 30786, 32233, 32566, 32698), 15 },
                  { AOM_CDF6(18189, 25387, 31743, 32513, 32678), 78 },
              },
              {
                  { AOM_CDF6(12034, 27402, 31238, 32348, 32566), 75 },
                  { AOM_CDF6(4, 21214, 29432, 31922, 32410), 0 },
              },
              {
                  { AOM_CDF6(12035, 24963, 29663, 31510, 32189), 15 },
                  { AOM_CDF6(4, 20271, 28450, 31190, 31955), 1 },
              },
              {
                  { AOM_CDF6(7438, 18411, 24999, 28700, 30554), 78 },
                  { AOM_CDF6(4, 12986, 21973, 27016, 29803), 75 },
              },
              {
                  { AOM_CDF6(4527, 12638, 19078, 23936, 27116), 93 },
                  { AOM_CDF6(4, 8342, 15837, 21424, 25160), 0 },
              },
              {
                  { AOM_CDF6(3334, 9604, 15301, 20048, 23474), 78 },
                  { AOM_CDF6(29, 6249, 12338, 17343, 21532), 0 },
              },
              {
                  { AOM_CDF6(1715, 5203, 8419, 11452, 13920), 93 },
                  { AOM_CDF6(278, 3139, 6396, 9529, 12406), 78 },
              },
              {
                  { AOM_CDF6(24768, 31988, 32544, 32716, 32732), 75 },
                  { AOM_CDF6(10750, 30362, 32442, 32659, 32726), 78 },
              },
              {
                  { AOM_CDF6(17706, 30183, 32284, 32621, 32675), 75 },
                  { AOM_CDF6(247, 26203, 31679, 32478, 32621), 75 },
              },
              {
                  { AOM_CDF6(13437, 27033, 31065, 32215, 32489), 75 },
                  { AOM_CDF6(260, 22099, 29474, 31675, 32323), 75 },
              },
              {
                  { AOM_CDF6(8036, 20282, 27031, 30205, 31610), 78 },
                  { AOM_CDF6(245, 15176, 24543, 29054, 31098), 75 },
              },
              {
                  { AOM_CDF6(3442, 9938, 15390, 19682, 22909), 78 },
                  { AOM_CDF6(368, 6602, 12644, 17584, 21317), 3 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(27962, 32440, 32644, 32700, 32719), 75 },
                  { AOM_CDF6(27962, 32440, 32644, 32700, 32719), 75 },
              },
              {
                  { AOM_CDF6(17649, 30227, 32072, 32459, 32572), 90 },
                  { AOM_CDF6(17649, 30227, 32072, 32459, 32572), 90 },
              },
              {
                  { AOM_CDF6(10838, 24277, 29419, 31247, 31906), 75 },
                  { AOM_CDF6(10838, 24277, 29419, 31247, 31906), 75 },
              },
              {
                  { AOM_CDF6(7189, 17877, 24471, 27927, 29660), 75 },
                  { AOM_CDF6(7189, 17877, 24471, 27927, 29660), 75 },
              },
              {
                  { AOM_CDF6(4369, 12444, 19031, 23624, 26498), 15 },
                  { AOM_CDF6(4369, 12444, 19031, 23624, 26498), 15 },
              },
              {
                  { AOM_CDF6(3187, 9258, 14817, 19352, 22697), 75 },
                  { AOM_CDF6(3187, 9258, 14817, 19352, 22697), 75 },
              },
              {
                  { AOM_CDF6(2083, 5866, 9714, 13201, 16204), 90 },
                  { AOM_CDF6(2083, 5866, 9714, 13201, 16204), 90 },
              },
              {
                  { AOM_CDF6(29243, 32638, 32734, 32738, 32742), 0 },
                  { AOM_CDF6(29243, 32638, 32734, 32738, 32742), 0 },
              },
              {
                  { AOM_CDF6(19686, 30972, 32412, 32638, 32703), 75 },
                  { AOM_CDF6(19686, 30972, 32412, 32638, 32703), 75 },
              },
              {
                  { AOM_CDF6(11953, 25789, 30425, 31863, 32324), 75 },
                  { AOM_CDF6(11953, 25789, 30425, 31863, 32324), 75 },
              },
              {
                  { AOM_CDF6(8156, 20260, 26854, 29902, 31236), 75 },
                  { AOM_CDF6(8156, 20260, 26854, 29902, 31236), 75 },
              },
              {
                  { AOM_CDF6(4774, 13186, 19753, 24154, 26962), 75 },
                  { AOM_CDF6(4774, 13186, 19753, 24154, 26962), 75 },
              },
#else
              {
                  { AOM_CDF6(28187, 32364, 32620, 32712, 32732), 0 },
                  { AOM_CDF6(25157, 30844, 32324, 32556, 32644), 0 },
              },
              {
                  { AOM_CDF6(20587, 30548, 32021, 32439, 32546), 90 },
                  { AOM_CDF6(103, 26920, 31486, 32256, 32478), 75 },
              },
              {
                  { AOM_CDF6(13529, 25627, 29857, 31344, 31893), 0 },
                  { AOM_CDF6(49, 21059, 28136, 30603, 31624), 0 },
              },
              {
                  { AOM_CDF6(7492, 18539, 24725, 27948, 29662), 90 },
                  { AOM_CDF6(11, 13465, 22069, 26503, 28996), 75 },
              },
              {
                  { AOM_CDF6(3844, 12052, 18257, 22610, 25119), 91 },
                  { AOM_CDF6(72, 8573, 15672, 20760, 24303), 90 },
              },
              {
                  { AOM_CDF6(2751, 8025, 13562, 17250, 20290), 76 },
                  { AOM_CDF6(90, 5529, 10855, 16056, 19810), 91 },
              },
              {
                  { AOM_CDF6(1613, 5030, 8215, 11394, 13804), 94 },
                  { AOM_CDF6(28, 2753, 5410, 8255, 10717), 90 },
              },
              {
                  { AOM_CDF6(29792, 32631, 32729, 32736, 32740), 75 },
                  { AOM_CDF6(24844, 32079, 32642, 32725, 32732), 15 },
              },
              {
                  { AOM_CDF6(22519, 31172, 32360, 32587, 32668), 75 },
                  { AOM_CDF6(2605, 28274, 31822, 32468, 32610), 75 },
              },
              {
                  { AOM_CDF6(13693, 26375, 30135, 31445, 32119), 75 },
                  { AOM_CDF6(1023, 21564, 28653, 31026, 31972), 75 },
              },
              {
                  { AOM_CDF6(8410, 20598, 26701, 29514, 31068), 93 },
                  { AOM_CDF6(746, 15248, 23652, 27830, 29994), 75 },
              },
              {
                  { AOM_CDF6(3497, 10737, 16003, 20080, 23290), 75 },
                  { AOM_CDF6(738, 6927, 13194, 18007, 21479), 90 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(16600, 30091, 31697, 32051, 32363), 6 },
                  { AOM_CDF6(2731, 8192, 17476, 26761, 31676), 50 },
              },
              {
                  { AOM_CDF6(3067, 18669, 25626, 29742, 31100), 18 },
                  { AOM_CDF6(4, 9589, 22611, 28009, 30613), 77 },
              },
              {
                  { AOM_CDF6(8619, 21012, 26594, 29276, 30618), 1 },
                  { AOM_CDF6(4, 15042, 23453, 27645, 29444), 0 },
              },
              {
                  { AOM_CDF6(5240, 12612, 18747, 23368, 26181), 15 },
                  { AOM_CDF6(4, 7539, 14502, 20242, 24231), 3 },
              },
              {
                  { AOM_CDF6(3041, 9263, 14181, 18518, 21405), 78 },
                  { AOM_CDF6(4, 5537, 10570, 15442, 19453), 78 },
              },
              {
                  { AOM_CDF6(1565, 6300, 9963, 13095, 15994), 5 },
                  { AOM_CDF6(779, 4419, 8965, 12629, 16019), 90 },
              },
              {
                  { AOM_CDF6(1605, 5292, 8378, 11662, 14043), 75 },
                  { AOM_CDF6(4, 3258, 6891, 9987, 12843), 100 },
              },
              {
                  { AOM_CDF6(1483, 4190, 6217, 8820, 10853), 0 },
                  { AOM_CDF6(56, 2604, 5276, 8293, 10935), 115 },
              },
              {
                  { AOM_CDF6(787, 2600, 4032, 5559, 6943), 118 },
                  { AOM_CDF6(344, 1185, 2450, 3906, 5098), 15 },
              },
              {
                  { AOM_CDF6(27103, 32443, 32707, 32736, 32740), 75 },
                  { AOM_CDF6(20689, 26234, 31743, 32545, 32693), 93 },
              },
              {
                  { AOM_CDF6(13891, 27867, 31508, 32372, 32602), 15 },
                  { AOM_CDF6(4, 22422, 29803, 31893, 32452), 75 },
              },
              {
                  { AOM_CDF6(13751, 26798, 30708, 32003, 32290), 81 },
                  { AOM_CDF6(4, 21195, 28064, 30900, 31805), 6 },
              },
              {
                  { AOM_CDF6(6768, 17699, 24498, 28246, 30138), 78 },
                  { AOM_CDF6(5, 12645, 21281, 26256, 29381), 0 },
              },
              {
                  { AOM_CDF6(4456, 13369, 20192, 24504, 26869), 76 },
                  { AOM_CDF6(6, 8366, 15696, 21363, 25582), 78 },
              },
              {
                  { AOM_CDF6(3486, 10293, 15986, 20473, 23764), 75 },
                  { AOM_CDF6(12, 6810, 12754, 18065, 22085), 90 },
              },
              {
                  { AOM_CDF6(1535, 5332, 8845, 12113, 14118), 1 },
                  { AOM_CDF6(306, 3603, 6981, 9853, 12569), 81 },
              },
              {
                  { AOM_CDF6(27062, 32443, 32701, 32736, 32740), 90 },
                  { AOM_CDF6(8783, 30921, 32606, 32736, 32740), 93 },
              },
              {
                  { AOM_CDF6(19664, 30809, 32484, 32689, 32723), 75 },
                  { AOM_CDF6(131, 27591, 32095, 32614, 32700), 5 },
              },
              {
                  { AOM_CDF6(15518, 28444, 31767, 32511, 32631), 80 },
                  { AOM_CDF6(462, 23517, 30230, 32012, 32464), 80 },
              },
              {
                  { AOM_CDF6(8448, 21327, 27877, 30781, 31894), 78 },
                  { AOM_CDF6(285, 15715, 25287, 29627, 31507), 75 },
              },
              {
                  { AOM_CDF6(3875, 11272, 17192, 21524, 24517), 75 },
                  { AOM_CDF6(250, 7725, 14392, 19419, 22992), 75 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(30513, 32660, 32735, 32739, 32743), 75 },
                  { AOM_CDF6(30513, 32660, 32735, 32739, 32743), 75 },
              },
              {
                  { AOM_CDF6(19741, 31317, 32435, 32635, 32696), 75 },
                  { AOM_CDF6(19741, 31317, 32435, 32635, 32696), 75 },
              },
              {
                  { AOM_CDF6(12474, 26518, 30757, 31972, 32360), 1 },
                  { AOM_CDF6(12474, 26518, 30757, 31972, 32360), 1 },
              },
              {
                  { AOM_CDF6(8024, 19833, 26418, 29417, 30720), 31 },
                  { AOM_CDF6(8024, 19833, 26418, 29417, 30720), 31 },
              },
              {
                  { AOM_CDF6(5339, 14743, 21943, 26369, 28810), 1 },
                  { AOM_CDF6(5339, 14743, 21943, 26369, 28810), 1 },
              },
              {
                  { AOM_CDF6(4118, 11547, 18150, 22872, 26018), 2 },
                  { AOM_CDF6(4118, 11547, 18150, 22872, 26018), 2 },
              },
              {
                  { AOM_CDF6(2921, 8227, 13156, 17466, 20816), 31 },
                  { AOM_CDF6(2921, 8227, 13156, 17466, 20816), 31 },
              },
              {
                  { AOM_CDF6(31197, 32729, 32740, 32744, 32748), 75 },
                  { AOM_CDF6(31197, 32729, 32740, 32744, 32748), 75 },
              },
              {
                  { AOM_CDF6(22157, 32007, 32636, 32730, 32734), 75 },
                  { AOM_CDF6(22157, 32007, 32636, 32730, 32734), 75 },
              },
              {
                  { AOM_CDF6(14153, 28311, 31723, 32428, 32619), 90 },
                  { AOM_CDF6(14153, 28311, 31723, 32428, 32619), 90 },
              },
              {
                  { AOM_CDF6(8964, 22454, 28710, 31129, 32018), 95 },
                  { AOM_CDF6(8964, 22454, 28710, 31129, 32018), 95 },
              },
              {
                  { AOM_CDF6(5502, 15192, 22260, 26601, 29088), 1 },
                  { AOM_CDF6(5502, 15192, 22260, 26601, 29088), 1 },
              },
#else
              {
                  { AOM_CDF6(29463, 32687, 32740, 32744, 32748), 1 },
                  { AOM_CDF6(30060, 32241, 32687, 32733, 32737), 2 },
              },
              {
                  { AOM_CDF6(20081, 31320, 32405, 32618, 32681), 91 },
                  { AOM_CDF6(2997, 27445, 31977, 32594, 32703), 5 },
              },
              {
                  { AOM_CDF6(12201, 26600, 30467, 31694, 32298), 75 },
                  { AOM_CDF6(1168, 21069, 28257, 31083, 31973), 76 },
              },
              {
                  { AOM_CDF6(6608, 19057, 25296, 28409, 30208), 0 },
                  { AOM_CDF6(1355, 13532, 22543, 27734, 29777), 1 },
              },
              {
                  { AOM_CDF6(3196, 11943, 19456, 24470, 26443), 1 },
                  { AOM_CDF6(2116, 8475, 16736, 21349, 25179), 76 },
              },
              {
                  { AOM_CDF6(2581, 7688, 12654, 18241, 22750), 76 },
                  { AOM_CDF6(164, 6473, 12781, 17660, 20418), 26 },
              },
              {
                  { AOM_CDF6(1741, 5717, 9639, 13106, 15807), 101 },
                  { AOM_CDF6(1234, 3543, 6805, 9970, 13652), 81 },
              },
              {
                  { AOM_CDF6(31108, 32744, 32748, 32752, 32756), 3 },
                  { AOM_CDF6(32526, 32655, 32740, 32744, 32748), 76 },
              },
              {
                  { AOM_CDF6(23778, 32099, 32627, 32724, 32732), 5 },
                  { AOM_CDF6(7858, 30188, 32401, 32671, 32730), 80 },
              },
              {
                  { AOM_CDF6(15040, 28615, 31463, 32343, 32608), 75 },
                  { AOM_CDF6(3712, 23163, 29685, 31606, 32214), 75 },
              },
              {
                  { AOM_CDF6(9021, 21748, 27503, 30171, 31613), 118 },
                  { AOM_CDF6(412, 17264, 26233, 29993, 31349), 30 },
              },
              {
                  { AOM_CDF6(2676, 12693, 19888, 23658, 26395), 25 },
                  { AOM_CDF6(2724, 9352, 16239, 21594, 25597), 0 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(12476, 22387, 26876, 28913, 29686), 32 },
                  { AOM_CDF6(11565, 12850, 22488, 24415, 29555), 50 },
              },
              {
                  { AOM_CDF6(4632, 16778, 22218, 27222, 29538), 76 },
                  { AOM_CDF6(4, 9182, 21676, 28011, 30127), 76 },
              },
              {
                  { AOM_CDF6(6271, 16535, 22522, 26294, 28910), 1 },
                  { AOM_CDF6(627, 11356, 20018, 24334, 26367), 6 },
              },
              {
                  { AOM_CDF6(3846, 11479, 15961, 21049, 23798), 76 },
                  { AOM_CDF6(4, 6789, 13456, 18824, 22259), 1 },
              },
              {
                  { AOM_CDF6(2668, 6958, 12011, 16127, 19511), 75 },
                  { AOM_CDF6(16, 5282, 10055, 14498, 18215), 100 },
              },
              {
                  { AOM_CDF6(2457, 6882, 10382, 12104, 14356), 100 },
                  { AOM_CDF6(50, 3653, 6698, 10997, 15151), 100 },
              },
              {
                  { AOM_CDF6(1332, 5580, 7419, 10065, 12677), 103 },
                  { AOM_CDF6(1729, 2053, 5025, 9241, 12032), 100 },
              },
              {
                  { AOM_CDF6(1961, 5692, 7972, 10797, 13274), 100 },
                  { AOM_CDF6(613, 724, 4216, 5464, 7542), 31 },
              },
              {
                  { AOM_CDF6(1093, 2768, 4888, 7027, 8432), 118 },
                  { AOM_CDF6(4, 721, 1451, 1935, 2546), 25 },
              },
              {
                  { AOM_CDF6(26062, 32026, 32603, 32715, 32732), 6 },
                  { AOM_CDF6(14735, 26695, 31909, 32575, 32714), 16 },
              },
              {
                  { AOM_CDF6(14374, 28377, 31733, 32528, 32628), 92 },
                  { AOM_CDF6(169, 24931, 30736, 32170, 32579), 85 },
              },
              {
                  { AOM_CDF6(12202, 24597, 28881, 31127, 32007), 90 },
                  { AOM_CDF6(387, 19841, 27589, 30390, 31613), 76 },
              },
              {
                  { AOM_CDF6(6042, 17002, 23576, 27702, 29959), 75 },
                  { AOM_CDF6(444, 12991, 21846, 26889, 29336), 81 },
              },
              {
                  { AOM_CDF6(4218, 11757, 18168, 22459, 25711), 75 },
                  { AOM_CDF6(20, 8709, 15833, 21179, 25095), 75 },
              },
              {
                  { AOM_CDF6(3078, 10061, 15459, 20578, 23302), 0 },
                  { AOM_CDF6(629, 6627, 12718, 17719, 21044), 1 },
              },
              {
                  { AOM_CDF6(1710, 4971, 8028, 10994, 13332), 115 },
                  { AOM_CDF6(152, 3374, 6812, 9963, 12672), 90 },
              },
              {
                  { AOM_CDF6(26911, 32367, 32732, 32736, 32740), 75 },
                  { AOM_CDF6(8376, 31803, 32729, 32736, 32740), 106 },
              },
              {
                  { AOM_CDF6(19996, 30927, 32462, 32682, 32732), 75 },
                  { AOM_CDF6(890, 27922, 32179, 32619, 32718), 0 },
              },
              {
                  { AOM_CDF6(16027, 28867, 31904, 32505, 32639), 1 },
                  { AOM_CDF6(1355, 23391, 29938, 31872, 32440), 83 },
              },
              {
                  { AOM_CDF6(8230, 21419, 27867, 30603, 31809), 93 },
                  { AOM_CDF6(598, 16010, 25389, 29646, 31481), 93 },
              },
              {
                  { AOM_CDF6(3694, 10738, 16136, 20260, 22972), 76 },
                  { AOM_CDF6(221, 7466, 13738, 18195, 21397), 1 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
          },
          {
              {
                  { AOM_CDF6(9166, 19415, 22900, 26209, 27731), 32 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(1877, 11631, 19591, 24107, 28355), 5 },
                  { AOM_CDF6(5, 5792, 19771, 24553, 28482), 6 },
              },
              {
                  { AOM_CDF6(5028, 14669, 19999, 24201, 26854), 7 },
                  { AOM_CDF6(188, 9086, 13171, 19579, 22691), 32 },
              },
              {
                  { AOM_CDF6(2294, 14281, 14937, 17103, 19192), 32 },
                  { AOM_CDF6(1618, 3446, 9623, 15987, 19755), 78 },
              },
              {
                  { AOM_CDF6(1967, 8436, 10048, 12275, 14789), 25 },
                  { AOM_CDF6(227, 2750, 3898, 10215, 13920), 26 },
              },
              {
                  { AOM_CDF6(1813, 5150, 7811, 10378, 12906), 94 },
                  { AOM_CDF6(48, 3235, 7391, 8671, 11349), 75 },
              },
              {
                  { AOM_CDF6(1250, 3797, 6249, 8067, 9487), 24 },
                  { AOM_CDF6(788, 2186, 4559, 8171, 10685), 84 },
              },
              {
                  { AOM_CDF6(1135, 4473, 6534, 8660, 10004), 22 },
                  { AOM_CDF6(528, 1408, 3097, 4681, 7488), 19 },
              },
              {
                  { AOM_CDF6(496, 1447, 2431, 3327, 3864), 93 },
                  { AOM_CDF6(440, 928, 1990, 2834, 3711), 90 },
              },
              {
                  { AOM_CDF6(24090, 30786, 31979, 32633, 32701), 31 },
                  { AOM_CDF6(3548, 27852, 31480, 32390, 32684), 26 },
              },
              {
                  { AOM_CDF6(13259, 26393, 31052, 32197, 32398), 1 },
                  { AOM_CDF6(183, 23714, 30229, 31519, 32174), 32 },
              },
              {
                  { AOM_CDF6(10597, 23480, 28322, 30586, 31626), 76 },
                  { AOM_CDF6(116, 17250, 24532, 28401, 30475), 6 },
              },
              {
                  { AOM_CDF6(6688, 17150, 22414, 26495, 27903), 1 },
                  { AOM_CDF6(269, 8285, 17370, 23434, 26939), 75 },
              },
              {
                  { AOM_CDF6(3076, 10090, 16289, 19838, 23214), 0 },
                  { AOM_CDF6(395, 7105, 12459, 18350, 22226), 0 },
              },
              {
                  { AOM_CDF6(2703, 8018, 10824, 15739, 19225), 0 },
                  { AOM_CDF6(886, 5419, 11713, 16761, 20089), 16 },
              },
              {
                  { AOM_CDF6(859, 2732, 4924, 7222, 8244), 0 },
                  { AOM_CDF6(142, 2303, 4463, 5921, 7690), 0 },
              },
              {
                  { AOM_CDF6(27211, 32516, 32737, 32741, 32745), 25 },
                  { AOM_CDF6(8649, 31088, 32683, 32736, 32740), 17 },
              },
              {
                  { AOM_CDF6(17214, 29762, 32135, 32605, 32709), 75 },
                  { AOM_CDF6(940, 27073, 31747, 32535, 32685), 16 },
              },
              {
                  { AOM_CDF6(13144, 26209, 30425, 31816, 32364), 90 },
                  { AOM_CDF6(1459, 22447, 29150, 31555, 32278), 80 },
              },
              {
                  { AOM_CDF6(6329, 19447, 26168, 29738, 31350), 26 },
                  { AOM_CDF6(951, 14635, 23268, 28098, 30172), 26 },
              },
              {
                  { AOM_CDF6(2403, 7249, 11277, 14779, 17316), 75 },
                  { AOM_CDF6(286, 4813, 9356, 12895, 15764), 80 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF6(4842, 20907, 25073, 29765, 32167), 100 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(1514, 13849, 21779, 26227, 28876), 2 },
                  { AOM_CDF6(4, 4626, 20048, 28108, 30770), 0 },
              },
              {
                  { AOM_CDF6(5384, 14511, 21744, 25595, 28838), 3 },
                  { AOM_CDF6(4, 10090, 17476, 23457, 27340), 25 },
              },
              {
                  { AOM_CDF6(4339, 9530, 15457, 19631, 23750), 18 },
                  { AOM_CDF6(4, 4561, 13182, 19543, 23775), 0 },
              },
              {
                  { AOM_CDF6(2987, 8244, 12055, 15728, 19137), 3 },
                  { AOM_CDF6(4, 5362, 7872, 14521, 18879), 15 },
              },
              {
                  { AOM_CDF6(2144, 6000, 10259, 12320, 15759), 0 },
                  { AOM_CDF6(4, 2260, 6384, 9936, 13243), 23 },
              },
              {
                  { AOM_CDF6(1358, 5655, 9098, 13116, 14912), 93 },
                  { AOM_CDF6(4, 860, 1688, 4919, 6113), 23 },
              },
              {
                  { AOM_CDF6(594, 3593, 6103, 9148, 11505), 5 },
                  { AOM_CDF6(4, 3184, 4747, 7666, 9458), 15 },
              },
              {
                  { AOM_CDF6(1234, 2833, 3871, 4753, 5448), 5 },
                  { AOM_CDF6(20, 2184, 2948, 6053, 6854), 120 },
              },
              {
                  { AOM_CDF6(13560, 27057, 30156, 32100, 32378), 78 },
                  { AOM_CDF6(1140, 23865, 30853, 32090, 32459), 0 },
              },
              {
                  { AOM_CDF6(7642, 21486, 28197, 31175, 31944), 78 },
                  { AOM_CDF6(4, 14682, 25263, 30248, 31687), 0 },
              },
              {
                  { AOM_CDF6(6527, 16184, 23111, 27765, 30299), 75 },
                  { AOM_CDF6(18, 12047, 21862, 26583, 30085), 0 },
              },
              {
                  { AOM_CDF6(5157, 13727, 20385, 26095, 28852), 78 },
                  { AOM_CDF6(4, 11192, 19773, 24679, 27119), 0 },
              },
              {
                  { AOM_CDF6(3723, 10748, 17510, 23330, 26361), 0 },
                  { AOM_CDF6(4, 7023, 14782, 19611, 24566), 1 },
              },
              {
                  { AOM_CDF6(3188, 9214, 14785, 19482, 23156), 78 },
                  { AOM_CDF6(32, 6579, 12195, 18385, 23198), 19 },
              },
              {
                  { AOM_CDF6(3509, 8518, 13498, 17740, 20515), 91 },
                  { AOM_CDF6(284, 3964, 9251, 14644, 18710), 18 },
              },
              {
                  { AOM_CDF6(20237, 29828, 32066, 32618, 32689), 93 },
                  { AOM_CDF6(9257, 25279, 31700, 32524, 32624), 3 },
              },
              {
                  { AOM_CDF6(12703, 26424, 31123, 32320, 32526), 75 },
                  { AOM_CDF6(447, 20271, 29487, 31983, 32481), 3 },
              },
              {
                  { AOM_CDF6(9174, 21444, 28049, 30969, 32021), 78 },
                  { AOM_CDF6(51, 14914, 25199, 29991, 32033), 75 },
              },
              {
                  { AOM_CDF6(6755, 17178, 24052, 28728, 30983), 78 },
                  { AOM_CDF6(182, 9960, 20549, 26765, 30196), 75 },
              },
              {
                  { AOM_CDF6(4805, 13231, 19767, 24730, 28488), 78 },
                  { AOM_CDF6(392, 7136, 15755, 22803, 27395), 79 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(23001, 31002, 32088, 32414, 32533), 75 },
                  { AOM_CDF6(23001, 31002, 32088, 32414, 32533), 75 },
              },
              {
                  { AOM_CDF6(13016, 26663, 30462, 31649, 32090), 93 },
                  { AOM_CDF6(13016, 26663, 30462, 31649, 32090), 93 },
              },
              {
                  { AOM_CDF6(8120, 18424, 25072, 28576, 30186), 75 },
                  { AOM_CDF6(8120, 18424, 25072, 28576, 30186), 75 },
              },
              {
                  { AOM_CDF6(6827, 14574, 20370, 24129, 26517), 115 },
                  { AOM_CDF6(6827, 14574, 20370, 24129, 26517), 115 },
              },
              {
                  { AOM_CDF6(2718, 7699, 12922, 17322, 20773), 75 },
                  { AOM_CDF6(2718, 7699, 12922, 17322, 20773), 75 },
              },
              {
                  { AOM_CDF6(1747, 5672, 9462, 12967, 16209), 3 },
                  { AOM_CDF6(1747, 5672, 9462, 12967, 16209), 3 },
              },
              {
                  { AOM_CDF6(1058, 3245, 5643, 8186, 10793), 124 },
                  { AOM_CDF6(1058, 3245, 5643, 8186, 10793), 124 },
              },
              {
                  { AOM_CDF6(28877, 32461, 32685, 32733, 32737), 75 },
                  { AOM_CDF6(28877, 32461, 32685, 32733, 32737), 75 },
              },
              {
                  { AOM_CDF6(16158, 27458, 31241, 32250, 32529), 90 },
                  { AOM_CDF6(16158, 27458, 31241, 32250, 32529), 90 },
              },
              {
                  { AOM_CDF6(9349, 18871, 25289, 29138, 30802), 75 },
                  { AOM_CDF6(9349, 18871, 25289, 29138, 30802), 75 },
              },
              {
                  { AOM_CDF6(7699, 16203, 21865, 25564, 28151), 98 },
                  { AOM_CDF6(7699, 16203, 21865, 25564, 28151), 98 },
              },
              {
                  { AOM_CDF6(3777, 9327, 13955, 18482, 22279), 93 },
                  { AOM_CDF6(3777, 9327, 13955, 18482, 22279), 93 },
              },
#else
              {
                  { AOM_CDF6(21983, 31523, 32451, 32565, 32597), 1 },
                  { AOM_CDF6(7915, 26221, 31335, 32162, 32396), 18 },
              },
              {
                  { AOM_CDF6(9970, 26361, 30283, 31769, 32051), 90 },
                  { AOM_CDF6(146, 23047, 30673, 31785, 32291), 0 },
              },
              {
                  { AOM_CDF6(8433, 16497, 24824, 28368, 30499), 90 },
                  { AOM_CDF6(44, 14233, 25518, 29679, 30909), 90 },
              },
              {
                  { AOM_CDF6(4571, 12898, 20199, 24817, 27339), 115 },
                  { AOM_CDF6(17, 9601, 17250, 24150, 27028), 15 },
              },
              {
                  { AOM_CDF6(2462, 7987, 14878, 19547, 23765), 0 },
                  { AOM_CDF6(4, 6407, 13197, 18507, 21793), 80 },
              },
              {
                  { AOM_CDF6(2372, 5957, 11178, 15119, 19819), 76 },
                  { AOM_CDF6(13, 5248, 9173, 11632, 15051), 3 },
              },
              {
                  { AOM_CDF6(1870, 4614, 6362, 10447, 12846), 77 },
                  { AOM_CDF6(112, 2200, 5308, 11014, 13376), 4 },
              },
              {
                  { AOM_CDF6(27359, 32586, 32675, 32736, 32740), 75 },
                  { AOM_CDF6(26957, 31269, 32260, 32655, 32688), 91 },
              },
              {
                  { AOM_CDF6(10108, 25011, 30041, 32132, 32489), 91 },
                  { AOM_CDF6(6080, 23078, 30236, 31765, 32433), 93 },
              },
              {
                  { AOM_CDF6(8424, 18673, 26161, 29473, 31228), 78 },
                  { AOM_CDF6(658, 13713, 21419, 27214, 30822), 90 },
              },
              {
                  { AOM_CDF6(5031, 14798, 21785, 26032, 29414), 93 },
                  { AOM_CDF6(232, 11498, 20849, 25284, 29203), 1 },
              },
              {
                  { AOM_CDF6(2583, 7556, 16083, 20862, 24333), 4 },
                  { AOM_CDF6(226, 6279, 12475, 17646, 22868), 10 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(17446, 31630, 31974, 32480, 32694), 6 },
                  { AOM_CDF6(745, 20108, 23087, 26810, 30534), 50 },
              },
              {
                  { AOM_CDF6(444, 16536, 26602, 30636, 31752), 75 },
                  { AOM_CDF6(4, 4122, 20984, 28078, 30725), 75 },
              },
              {
                  { AOM_CDF6(11064, 22139, 26505, 29252, 31031), 0 },
                  { AOM_CDF6(4, 15609, 25598, 29542, 30754), 0 },
              },
              {
                  { AOM_CDF6(7901, 14684, 20503, 25735, 28535), 0 },
                  { AOM_CDF6(4, 6765, 18326, 23476, 26890), 78 },
              },
              {
                  { AOM_CDF6(4852, 9912, 15528, 20821, 24193), 0 },
                  { AOM_CDF6(4, 5364, 11819, 16875, 20416), 0 },
              },
              {
                  { AOM_CDF6(2929, 7620, 12076, 16248, 19712), 94 },
                  { AOM_CDF6(4, 4388, 9223, 12734, 16601), 91 },
              },
              {
                  { AOM_CDF6(1329, 6883, 9969, 13370, 16430), 76 },
                  { AOM_CDF6(1701, 2909, 6304, 9767, 12844), 16 },
              },
              {
                  { AOM_CDF6(1728, 4758, 7450, 10296, 12623), 90 },
                  { AOM_CDF6(574, 2684, 5942, 9418, 11472), 90 },
              },
              {
                  { AOM_CDF6(1206, 3006, 5077, 6698, 7964), 90 },
                  { AOM_CDF6(351, 2285, 4410, 6357, 8731), 94 },
              },
              {
                  { AOM_CDF6(23149, 32286, 32623, 32713, 32732), 85 },
                  { AOM_CDF6(22930, 25618, 32221, 32613, 32674), 75 },
              },
              {
                  { AOM_CDF6(9103, 27259, 31347, 32439, 32593), 75 },
                  { AOM_CDF6(4, 21001, 30139, 32124, 32487), 0 },
              },
              {
                  { AOM_CDF6(12805, 24769, 29614, 31745, 32243), 90 },
                  { AOM_CDF6(4, 21576, 29438, 31527, 32139), 1 },
              },
              {
                  { AOM_CDF6(7436, 18420, 25204, 29137, 30970), 75 },
                  { AOM_CDF6(4, 13571, 23401, 28424, 30593), 75 },
              },
              {
                  { AOM_CDF6(5097, 13176, 20234, 25354, 28201), 90 },
                  { AOM_CDF6(4, 9566, 18132, 23444, 27202), 75 },
              },
              {
                  { AOM_CDF6(3818, 10970, 16891, 21884, 25227), 93 },
                  { AOM_CDF6(14, 7280, 14388, 20162, 24092), 90 },
              },
              {
                  { AOM_CDF6(3110, 9110, 14002, 17782, 20457), 93 },
                  { AOM_CDF6(345, 3823, 8446, 12922, 17200), 78 },
              },
              {
                  { AOM_CDF6(27209, 32546, 32704, 32736, 32740), 100 },
                  { AOM_CDF6(8092, 30462, 32633, 32717, 32732), 75 },
              },
              {
                  { AOM_CDF6(17310, 30556, 32463, 32687, 32714), 75 },
                  { AOM_CDF6(23, 26050, 31978, 32599, 32677), 0 },
              },
              {
                  { AOM_CDF6(13830, 27352, 31478, 32446, 32589), 75 },
                  { AOM_CDF6(41, 21949, 29752, 32000, 32456), 0 },
              },
              {
                  { AOM_CDF6(8213, 21159, 28070, 31157, 32059), 78 },
                  { AOM_CDF6(90, 15277, 25296, 29905, 31684), 78 },
              },
              {
                  { AOM_CDF6(4597, 13368, 20248, 25108, 28086), 78 },
                  { AOM_CDF6(98, 8889, 16780, 22652, 26736), 78 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(29681, 32477, 32658, 32712, 32731), 75 },
                  { AOM_CDF6(29681, 32477, 32658, 32712, 32731), 75 },
              },
              {
                  { AOM_CDF6(16011, 29320, 31832, 32399, 32555), 115 },
                  { AOM_CDF6(16011, 29320, 31832, 32399, 32555), 115 },
              },
              {
                  { AOM_CDF6(9294, 21806, 27768, 30409, 31521), 90 },
                  { AOM_CDF6(9294, 21806, 27768, 30409, 31521), 90 },
              },
              {
                  { AOM_CDF6(6107, 14952, 21511, 25626, 28146), 75 },
                  { AOM_CDF6(6107, 14952, 21511, 25626, 28146), 75 },
              },
              {
                  { AOM_CDF6(2990, 9058, 15121, 20098, 23744), 90 },
                  { AOM_CDF6(2990, 9058, 15121, 20098, 23744), 90 },
              },
              {
                  { AOM_CDF6(2157, 6562, 11095, 15323, 19234), 75 },
                  { AOM_CDF6(2157, 6562, 11095, 15323, 19234), 75 },
              },
              {
                  { AOM_CDF6(1346, 3906, 6845, 9832, 12783), 90 },
                  { AOM_CDF6(1346, 3906, 6845, 9832, 12783), 90 },
              },
              {
                  { AOM_CDF6(30626, 32666, 32740, 32744, 32748), 0 },
                  { AOM_CDF6(30626, 32666, 32740, 32744, 32748), 0 },
              },
              {
                  { AOM_CDF6(19326, 30715, 32384, 32644, 32713), 93 },
                  { AOM_CDF6(19326, 30715, 32384, 32644, 32713), 93 },
              },
              {
                  { AOM_CDF6(11868, 25066, 29958, 31653, 32254), 115 },
                  { AOM_CDF6(11868, 25066, 29958, 31653, 32254), 115 },
              },
              {
                  { AOM_CDF6(8124, 19467, 26069, 29307, 30909), 115 },
                  { AOM_CDF6(8124, 19467, 26069, 29307, 30909), 115 },
              },
              {
                  { AOM_CDF6(4754, 12825, 19113, 23576, 26611), 90 },
                  { AOM_CDF6(4754, 12825, 19113, 23576, 26611), 90 },
              },
#else
              {
                  { AOM_CDF6(29406, 32588, 32680, 32730, 32734), 3 },
                  { AOM_CDF6(29211, 31901, 32601, 32700, 32727), 0 },
              },
              {
                  { AOM_CDF6(20118, 30814, 32177, 32539, 32603), 90 },
                  { AOM_CDF6(52, 26083, 31753, 32437, 32584), 75 },
              },
              {
                  { AOM_CDF6(14046, 26099, 30218, 31810, 32229), 75 },
                  { AOM_CDF6(19, 21296, 29291, 31425, 32124), 75 },
              },
              {
                  { AOM_CDF6(8821, 20915, 27111, 29825, 31049), 76 },
                  { AOM_CDF6(25, 14867, 24255, 28687, 30494), 90 },
              },
              {
                  { AOM_CDF6(3921, 16065, 23650, 27649, 29279), 91 },
                  { AOM_CDF6(16, 9887, 17153, 23036, 26898), 90 },
              },
              {
                  { AOM_CDF6(4216, 10745, 17208, 22251, 24017), 76 },
                  { AOM_CDF6(28, 7148, 14242, 19205, 24305), 1 },
              },
              {
                  { AOM_CDF6(2205, 6596, 10889, 14943, 18040), 93 },
                  { AOM_CDF6(19, 4689, 8461, 13407, 16732), 0 },
              },
              {
                  { AOM_CDF6(30076, 32703, 32739, 32743, 32747), 75 },
                  { AOM_CDF6(29035, 32152, 32690, 32736, 32740), 15 },
              },
              {
                  { AOM_CDF6(21372, 31362, 32509, 32685, 32713), 93 },
                  { AOM_CDF6(1817, 27720, 32036, 32601, 32687), 75 },
              },
              {
                  { AOM_CDF6(14525, 27622, 31252, 32173, 32500), 115 },
                  { AOM_CDF6(314, 22101, 29720, 31843, 32403), 91 },
              },
              {
                  { AOM_CDF6(9040, 22221, 28132, 30633, 31759), 100 },
                  { AOM_CDF6(348, 17437, 26954, 30164, 31598), 90 },
              },
              {
                  { AOM_CDF6(5377, 15294, 22474, 26827, 29028), 115 },
                  { AOM_CDF6(421, 9759, 18236, 23952, 27679), 90 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(19498, 31803, 32362, 32603, 32717), 7 },
                  { AOM_CDF6(1074, 19876, 25247, 29814, 32231), 50 },
              },
              {
                  { AOM_CDF6(1716, 19324, 26985, 30752, 31712), 1 },
                  { AOM_CDF6(4, 7344, 23296, 27775, 30880), 5 },
              },
              {
                  { AOM_CDF6(11553, 24088, 28463, 30245, 31254), 1 },
                  { AOM_CDF6(4, 17370, 25445, 29140, 30587), 1 },
              },
              {
                  { AOM_CDF6(7024, 15273, 21840, 25935, 28288), 0 },
                  { AOM_CDF6(4, 7731, 17353, 23076, 27037), 16 },
              },
              {
                  { AOM_CDF6(3746, 9985, 15632, 20017, 23027), 75 },
                  { AOM_CDF6(4, 6373, 11602, 17042, 21419), 80 },
              },
              {
                  { AOM_CDF6(2082, 7026, 11381, 14786, 17520), 0 },
                  { AOM_CDF6(4, 4871, 9364, 13890, 17656), 115 },
              },
              {
                  { AOM_CDF6(2087, 6015, 9380, 12730, 15665), 100 },
                  { AOM_CDF6(4, 3918, 7711, 11351, 14541), 100 },
              },
              {
                  { AOM_CDF6(1341, 4509, 7293, 9812, 12372), 75 },
                  { AOM_CDF6(256, 2590, 5623, 9095, 11636), 0 },
              },
              {
                  { AOM_CDF6(976, 3688, 5636, 8039, 9715), 105 },
                  { AOM_CDF6(687, 1620, 3367, 5293, 6811), 20 },
              },
              {
                  { AOM_CDF6(28887, 32555, 32681, 32736, 32740), 86 },
                  { AOM_CDF6(24226, 25856, 32179, 32602, 32696), 91 },
              },
              {
                  { AOM_CDF6(12303, 28716, 32009, 32572, 32656), 76 },
                  { AOM_CDF6(4, 24146, 30930, 32278, 32584), 76 },
              },
              {
                  { AOM_CDF6(15354, 28286, 31174, 32271, 32471), 1 },
                  { AOM_CDF6(4, 22931, 29154, 31249, 32097), 5 },
              },
              {
                  { AOM_CDF6(8069, 19928, 26367, 29511, 31034), 78 },
                  { AOM_CDF6(4, 14000, 23263, 28180, 30447), 0 },
              },
              {
                  { AOM_CDF6(4893, 14615, 22005, 26172, 28105), 90 },
                  { AOM_CDF6(22, 8940, 16767, 22484, 26784), 90 },
              },
              {
                  { AOM_CDF6(3896, 11404, 17690, 22419, 25421), 100 },
                  { AOM_CDF6(4, 7262, 13989, 19525, 23762), 90 },
              },
              {
                  { AOM_CDF6(2008, 7092, 11478, 15543, 18272), 75 },
                  { AOM_CDF6(524, 5359, 9988, 13990, 17384), 76 },
              },
              {
                  { AOM_CDF6(27048, 32468, 32715, 32736, 32740), 90 },
                  { AOM_CDF6(5312, 31067, 32652, 32736, 32740), 75 },
              },
              {
                  { AOM_CDF6(19326, 30997, 32564, 32716, 32732), 75 },
                  { AOM_CDF6(35, 27721, 32232, 32660, 32719), 0 },
              },
              {
                  { AOM_CDF6(16626, 29282, 32015, 32591, 32677), 0 },
                  { AOM_CDF6(158, 24544, 30759, 32284, 32576), 0 },
              },
              {
                  { AOM_CDF6(9045, 22277, 28749, 31356, 32195), 93 },
                  { AOM_CDF6(62, 16924, 26769, 30712, 32049), 75 },
              },
              {
                  { AOM_CDF6(4870, 14063, 21039, 25691, 28445), 75 },
                  { AOM_CDF6(65, 9937, 18152, 23756, 27274), 75 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(31190, 32623, 32728, 32736, 32740), 75 },
                  { AOM_CDF6(31190, 32623, 32728, 32736, 32740), 75 },
              },
              {
                  { AOM_CDF6(17296, 30538, 32283, 32595, 32679), 115 },
                  { AOM_CDF6(17296, 30538, 32283, 32595, 32679), 115 },
              },
              {
                  { AOM_CDF6(9855, 23676, 29421, 31444, 32139), 90 },
                  { AOM_CDF6(9855, 23676, 29421, 31444, 32139), 90 },
              },
              {
                  { AOM_CDF6(5610, 15572, 23067, 27145, 29141), 75 },
                  { AOM_CDF6(5610, 15572, 23067, 27145, 29141), 75 },
              },
              {
                  { AOM_CDF6(3359, 10455, 17462, 22799, 26242), 75 },
                  { AOM_CDF6(3359, 10455, 17462, 22799, 26242), 75 },
              },
              {
                  { AOM_CDF6(2562, 7733, 13030, 18172, 22037), 95 },
                  { AOM_CDF6(2562, 7733, 13030, 18172, 22037), 95 },
              },
              {
                  { AOM_CDF6(1533, 4582, 7815, 11120, 14554), 8 },
                  { AOM_CDF6(1533, 4582, 7815, 11120, 14554), 8 },
              },
              {
                  { AOM_CDF6(31894, 32738, 32742, 32746, 32750), 0 },
                  { AOM_CDF6(31894, 32738, 32742, 32746, 32750), 0 },
              },
              {
                  { AOM_CDF6(20885, 31806, 32640, 32734, 32738), 118 },
                  { AOM_CDF6(20885, 31806, 32640, 32734, 32738), 118 },
              },
              {
                  { AOM_CDF6(12973, 27352, 31367, 32350, 32605), 100 },
                  { AOM_CDF6(12973, 27352, 31367, 32350, 32605), 100 },
              },
              {
                  { AOM_CDF6(8298, 21287, 28025, 30769, 31890), 91 },
                  { AOM_CDF6(8298, 21287, 28025, 30769, 31890), 91 },
              },
              {
                  { AOM_CDF6(4886, 13785, 20958, 25454, 28197), 76 },
                  { AOM_CDF6(4886, 13785, 20958, 25454, 28197), 76 },
              },
#else
              {
                  { AOM_CDF6(30342, 32711, 32740, 32744, 32748), 1 },
                  { AOM_CDF6(32530, 32554, 32723, 32736, 32740), 0 },
              },
              {
                  { AOM_CDF6(22069, 32091, 32582, 32700, 32725), 76 },
                  { AOM_CDF6(1691, 27972, 32195, 32594, 32715), 75 },
              },
              {
                  { AOM_CDF6(18133, 30538, 32123, 32524, 32661), 75 },
                  { AOM_CDF6(190, 25412, 31270, 32353, 32588), 75 },
              },
              {
                  { AOM_CDF6(11850, 25829, 30540, 31840, 32307), 5 },
                  { AOM_CDF6(518, 19636, 28618, 31448, 32157), 0 },
              },
              {
                  { AOM_CDF6(6808, 18208, 26007, 29720, 30957), 76 },
                  { AOM_CDF6(1777, 11958, 22427, 27329, 30086), 76 },
              },
              {
                  { AOM_CDF6(3794, 11308, 17744, 23796, 27654), 76 },
                  { AOM_CDF6(142, 9991, 19680, 25302, 27286), 32 },
              },
              {
                  { AOM_CDF6(2605, 7800, 12499, 16700, 20057), 101 },
                  { AOM_CDF6(1423, 4167, 8975, 13502, 17999), 90 },
              },
              {
                  { AOM_CDF6(30658, 32697, 32740, 32744, 32748), 0 },
                  { AOM_CDF6(32694, 32698, 32740, 32744, 32748), 75 },
              },
              {
                  { AOM_CDF6(24296, 32409, 32692, 32736, 32740), 80 },
                  { AOM_CDF6(3366, 29976, 32536, 32726, 32732), 80 },
              },
              {
                  { AOM_CDF6(19397, 31285, 32443, 32688, 32732), 100 },
                  { AOM_CDF6(785, 27193, 32021, 32573, 32689), 75 },
              },
              {
                  { AOM_CDF6(12726, 27082, 31132, 32222, 32566), 100 },
                  { AOM_CDF6(29, 23495, 31268, 32442, 32671), 0 },
              },
              {
                  { AOM_CDF6(5088, 19909, 27814, 30515, 31513), 26 },
                  { AOM_CDF6(2779, 13976, 22563, 27348, 29775), 1 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(18369, 28750, 30767, 31679, 31962), 32 },
                  { AOM_CDF6(11235, 21533, 28087, 29491, 30427), 50 },
              },
              {
                  { AOM_CDF6(5658, 17658, 24620, 28977, 30879), 1 },
                  { AOM_CDF6(4, 11402, 22885, 28559, 30692), 15 },
              },
              {
                  { AOM_CDF6(10074, 22055, 27447, 29607, 31031), 25 },
                  { AOM_CDF6(4, 18604, 26012, 28842, 30041), 26 },
              },
              {
                  { AOM_CDF6(4255, 14456, 19489, 24335, 27001), 6 },
                  { AOM_CDF6(4, 9669, 17146, 23378, 26599), 0 },
              },
              {
                  { AOM_CDF6(3095, 7838, 13438, 17076, 20209), 31 },
                  { AOM_CDF6(4, 6699, 12805, 17939, 22481), 100 },
              },
              {
                  { AOM_CDF6(1907, 8481, 12666, 15353, 18350), 75 },
                  { AOM_CDF6(1534, 4650, 8748, 14149, 18320), 0 },
              },
              {
                  { AOM_CDF6(899, 4980, 8076, 11175, 13025), 26 },
                  { AOM_CDF6(1198, 4437, 8203, 12605, 16331), 75 },
              },
              {
                  { AOM_CDF6(1921, 7069, 11605, 15583, 18257), 100 },
                  { AOM_CDF6(1090, 1427, 3417, 4036, 5950), 26 },
              },
              {
                  { AOM_CDF6(1550, 4215, 7211, 9789, 11281), 0 },
                  { AOM_CDF6(6, 1331, 2225, 3502, 4755), 75 },
              },
              {
                  { AOM_CDF6(28052, 32262, 32618, 32709, 32732), 6 },
                  { AOM_CDF6(9595, 28065, 32429, 32683, 32732), 12 },
              },
              {
                  { AOM_CDF6(14331, 29666, 32065, 32556, 32672), 2 },
                  { AOM_CDF6(14, 26353, 31293, 32488, 32668), 5 },
              },
              {
                  { AOM_CDF6(15089, 27195, 30928, 32030, 32402), 5 },
                  { AOM_CDF6(7, 23017, 29417, 31412, 32197), 5 },
              },
              {
                  { AOM_CDF6(7753, 19265, 26158, 29666, 30969), 15 },
                  { AOM_CDF6(42, 15833, 24498, 28601, 30566), 0 },
              },
              {
                  { AOM_CDF6(4879, 13241, 20168, 24896, 27798), 75 },
                  { AOM_CDF6(13, 9232, 17012, 22633, 26646), 90 },
              },
              {
                  { AOM_CDF6(3212, 10660, 16186, 21879, 23748), 25 },
                  { AOM_CDF6(224, 7767, 15065, 19540, 23626), 75 },
              },
              {
                  { AOM_CDF6(2415, 7154, 11280, 14840, 17255), 75 },
                  { AOM_CDF6(347, 3747, 7551, 11144, 14391), 0 },
              },
              {
                  { AOM_CDF6(26824, 32497, 32740, 32744, 32748), 0 },
                  { AOM_CDF6(4927, 31928, 32702, 32736, 32740), 76 },
              },
              {
                  { AOM_CDF6(20714, 31285, 32541, 32722, 32732), 75 },
                  { AOM_CDF6(141, 28731, 32423, 32683, 32732), 1 },
              },
              {
                  { AOM_CDF6(16966, 29936, 32293, 32637, 32698), 1 },
                  { AOM_CDF6(333, 24800, 30741, 32242, 32573), 80 },
              },
              {
                  { AOM_CDF6(9428, 23165, 29520, 31725, 32322), 76 },
                  { AOM_CDF6(183, 17188, 26499, 30397, 31890), 80 },
              },
              {
                  { AOM_CDF6(4433, 12770, 19162, 23467, 26183), 80 },
                  { AOM_CDF6(239, 8770, 16053, 21208, 24514), 80 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
          },
          {
              {
                  { AOM_CDF6(18220, 27817, 29642, 30638, 31232), 62 },
                  { AOM_CDF6(1170, 21455, 26917, 30818, 31988), 50 },
              },
              {
                  { AOM_CDF6(3891, 15848, 22851, 27994, 29793), 5 },
                  { AOM_CDF6(4, 9572, 22857, 27471, 30250), 6 },
              },
              {
                  { AOM_CDF6(8542, 20260, 25710, 28676, 30426), 1 },
                  { AOM_CDF6(4, 16305, 22090, 27076, 29048), 7 },
              },
              {
                  { AOM_CDF6(4585, 16828, 20728, 22695, 24948), 6 },
                  { AOM_CDF6(1694, 5176, 14173, 20427, 24734), 31 },
              },
              {
                  { AOM_CDF6(2914, 12533, 16153, 19908, 21920), 1 },
                  { AOM_CDF6(205, 5047, 7747, 14490, 19383), 1 },
              },
              {
                  { AOM_CDF6(2733, 7494, 11642, 15790, 18460), 0 },
                  { AOM_CDF6(4, 4288, 8809, 11860, 14604), 0 },
              },
              {
                  { AOM_CDF6(1920, 6130, 10679, 13827, 15622), 0 },
                  { AOM_CDF6(337, 2970, 5839, 8292, 11888), 1 },
              },
              {
                  { AOM_CDF6(1370, 4921, 7259, 9478, 11706), 15 },
                  { AOM_CDF6(622, 3070, 5927, 9122, 12000), 15 },
              },
              {
                  { AOM_CDF6(806, 2086, 3484, 5269, 6034), 75 },
                  { AOM_CDF6(425, 1430, 3548, 4874, 6064), 75 },
              },
              {
                  { AOM_CDF6(27172, 32021, 32535, 32721, 32732), 32 },
                  { AOM_CDF6(6700, 28268, 32266, 32682, 32726), 1 },
              },
              {
                  { AOM_CDF6(14033, 28608, 31937, 32516, 32666), 0 },
                  { AOM_CDF6(5, 25162, 30887, 32200, 32541), 1 },
              },
              {
                  { AOM_CDF6(13870, 26890, 30545, 31882, 32345), 1 },
                  { AOM_CDF6(39, 21623, 28581, 31009, 32019), 6 },
              },
              {
                  { AOM_CDF6(7662, 21491, 27056, 29892, 31051), 1 },
                  { AOM_CDF6(2510, 10777, 20599, 26536, 29402), 1 },
              },
              {
                  { AOM_CDF6(4480, 13687, 19862, 23836, 26585), 0 },
                  { AOM_CDF6(9, 8613, 15756, 21761, 25473), 0 },
              },
              {
                  { AOM_CDF6(3242, 9810, 14940, 19815, 22853), 0 },
                  { AOM_CDF6(1766, 6619, 13390, 18348, 22410), 1 },
              },
              {
                  { AOM_CDF6(2348, 5889, 8839, 12121, 13993), 75 },
                  { AOM_CDF6(121, 2095, 5699, 8594, 10331), 26 },
              },
              {
                  { AOM_CDF6(28278, 32642, 32740, 32744, 32748), 31 },
                  { AOM_CDF6(9278, 31654, 32709, 32736, 32740), 81 },
              },
              {
                  { AOM_CDF6(19933, 31040, 32461, 32697, 32732), 80 },
                  { AOM_CDF6(278, 29000, 32281, 32678, 32730), 1 },
              },
              {
                  { AOM_CDF6(16199, 28568, 31564, 32374, 32586), 75 },
                  { AOM_CDF6(393, 25547, 31051, 32294, 32586), 81 },
              },
              {
                  { AOM_CDF6(9057, 23326, 28792, 31102, 32035), 1 },
                  { AOM_CDF6(1317, 17380, 26498, 30264, 31685), 1 },
              },
              {
                  { AOM_CDF6(3381, 10098, 15426, 19504, 22173), 75 },
                  { AOM_CDF6(458, 7020, 13130, 17600, 20791), 75 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF6(4681, 14043, 18725, 23406, 28087), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(103, 11271, 26838, 30664, 32076), 1 },
                  { AOM_CDF6(4, 6418, 20098, 27848, 30875), 1 },
              },
              {
                  { AOM_CDF6(4587, 16793, 22808, 28242, 31144), 80 },
                  { AOM_CDF6(10, 19684, 25353, 28286, 31126), 27 },
              },
              {
                  { AOM_CDF6(2377, 8011, 16642, 27160, 30985), 2 },
                  { AOM_CDF6(3228, 3851, 16419, 26311, 27649), 12 },
              },
              {
                  { AOM_CDF6(5056, 11235, 16478, 19099, 27900), 9 },
                  { AOM_CDF6(410, 9830, 12698, 18022, 22118), 15 },
              },
              {
                  { AOM_CDF6(4681, 9362, 10923, 17164, 20285), 100 },
                  { AOM_CDF6(5461, 8192, 13653, 19115, 27307), 100 },
              },
              {
                  { AOM_CDF6(9362, 14043, 18725, 23406, 28087), 0 },
                  { AOM_CDF6(4681, 9362, 14043, 18725, 23406), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(19256, 30277, 32446, 32709, 32732), 31 },
                  { AOM_CDF6(3451, 24556, 32425, 32736, 32740), 31 },
              },
              {
                  { AOM_CDF6(7962, 30736, 32423, 32723, 32732), 18 },
                  { AOM_CDF6(20, 19022, 30515, 32657, 32732), 0 },
              },
              {
                  { AOM_CDF6(14671, 31018, 32209, 32545, 32731), 1 },
                  { AOM_CDF6(146, 25519, 31340, 32292, 32658), 37 },
              },
              {
                  { AOM_CDF6(10448, 24695, 31106, 32056, 32293), 20 },
                  { AOM_CDF6(4551, 16384, 28217, 30644, 32465), 35 },
              },
              {
                  { AOM_CDF6(5461, 19115, 21845, 24576, 27307), 100 },
                  { AOM_CDF6(4681, 14043, 25746, 28087, 30427), 100 },
              },
              {
                  { AOM_CDF6(4681, 9362, 18725, 23406, 28087), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(19246, 32462, 32666, 32734, 32738), 0 },
                  { AOM_CDF6(12369, 30212, 32735, 32739, 32743), 83 },
              },
              {
                  { AOM_CDF6(14644, 31985, 32623, 32681, 32732), 16 },
                  { AOM_CDF6(5146, 29315, 32442, 32670, 32732), 5 },
              },
              {
                  { AOM_CDF6(13473, 29774, 32103, 32435, 32602), 35 },
                  { AOM_CDF6(3277, 23101, 30966, 32276, 32604), 50 },
              },
              {
                  { AOM_CDF6(8623, 20696, 25869, 29319, 31043), 50 },
                  { AOM_CDF6(2185, 15292, 24030, 28399, 30583), 50 },
              },
              {
                  { AOM_CDF6(4681, 14043, 18725, 23406, 28087), 0 },
                  { AOM_CDF6(4681, 9362, 18725, 23406, 28087), 0 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(27523, 31549, 32239, 32557, 32644), 99 },
                  { AOM_CDF6(27523, 31549, 32239, 32557, 32644), 99 },
              },
              {
                  { AOM_CDF6(13891, 26332, 30540, 31905, 32545), 15 },
                  { AOM_CDF6(13891, 26332, 30540, 31905, 32545), 15 },
              },
              {
                  { AOM_CDF6(9368, 19790, 26821, 30278, 31710), 42 },
                  { AOM_CDF6(9368, 19790, 26821, 30278, 31710), 42 },
              },
              {
                  { AOM_CDF6(7770, 17585, 24454, 27851, 29822), 50 },
                  { AOM_CDF6(7770, 17585, 24454, 27851, 29822), 50 },
              },
              {
                  { AOM_CDF6(2731, 8548, 14128, 22439, 25882), 0 },
                  { AOM_CDF6(2731, 8548, 14128, 22439, 25882), 0 },
              },
              {
                  { AOM_CDF6(520, 7282, 12483, 20285, 23406), 0 },
                  { AOM_CDF6(520, 7282, 12483, 20285, 23406), 0 },
              },
              {
                  { AOM_CDF6(1638, 11469, 14746, 16384, 19661), 0 },
                  { AOM_CDF6(1638, 11469, 14746, 16384, 19661), 0 },
              },
              {
                  { AOM_CDF6(30430, 32574, 32740, 32744, 32748), 41 },
                  { AOM_CDF6(30430, 32574, 32740, 32744, 32748), 41 },
              },
              {
                  { AOM_CDF6(19285, 29197, 32409, 32731, 32735), 19 },
                  { AOM_CDF6(19285, 29197, 32409, 32731, 32735), 19 },
              },
              {
                  { AOM_CDF6(13323, 25054, 30001, 31837, 32425), 25 },
                  { AOM_CDF6(13323, 25054, 30001, 31837, 32425), 25 },
              },
              {
                  { AOM_CDF6(10464, 17623, 22855, 28913, 31667), 0 },
                  { AOM_CDF6(10464, 17623, 22855, 28913, 31667), 0 },
              },
              {
                  { AOM_CDF6(5461, 16384, 21845, 25122, 29491), 0 },
                  { AOM_CDF6(5461, 16384, 21845, 25122, 29491), 0 },
              },
#else
              {
                  { AOM_CDF6(30065, 32622, 32658, 32695, 32731), 35 },
                  { AOM_CDF6(31535, 31634, 32447, 32620, 32719), 86 },
              },
              {
                  { AOM_CDF6(4975, 27885, 32307, 32614, 32676), 93 },
                  { AOM_CDF6(10034, 26418, 30444, 32258, 32683), 79 },
              },
              {
                  { AOM_CDF6(7312, 18144, 28706, 30331, 32497), 25 },
                  { AOM_CDF6(6489, 12004, 22386, 30497, 32119), 25 },
              },
              {
                  { AOM_CDF6(2521, 15124, 22686, 27727, 30247), 0 },
                  { AOM_CDF6(2341, 7022, 14043, 21065, 30427), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(4681, 9362, 14043, 18725, 28087), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(29770, 31911, 32125, 32340, 32554), 85 },
                  { AOM_CDF6(28236, 31199, 32245, 32419, 32594), 45 },
              },
              {
                  { AOM_CDF6(14791, 29127, 32085, 32313, 32540), 35 },
                  { AOM_CDF6(16177, 27210, 32187, 32602, 32685), 8 },
              },
              {
                  { AOM_CDF6(9830, 16384, 22938, 26214, 29491), 50 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(6656, 28025, 30549, 32028, 32513), 9 },
                  { AOM_CDF6(5041, 10082, 17644, 20165, 27727), 0 },
              },
              {
                  { AOM_CDF6(279, 20342, 28831, 31316, 32156), 0 },
                  { AOM_CDF6(4, 5222, 23131, 29232, 31279), 0 },
              },
              {
                  { AOM_CDF6(9562, 18247, 25664, 29587, 30939), 0 },
                  { AOM_CDF6(4, 13602, 25267, 28755, 30693), 1 },
              },
              {
                  { AOM_CDF6(4293, 14308, 21667, 26856, 29719), 0 },
                  { AOM_CDF6(4, 9398, 17200, 24012, 27811), 15 },
              },
              {
                  { AOM_CDF6(2096, 9179, 15466, 21307, 25525), 75 },
                  { AOM_CDF6(3678, 6848, 13812, 19976, 23634), 0 },
              },
              {
                  { AOM_CDF6(2093, 7368, 12031, 17663, 22850), 96 },
                  { AOM_CDF6(4, 5580, 11201, 13997, 19937), 1 },
              },
              {
                  { AOM_CDF6(2403, 7524, 10107, 15168, 19192), 1 },
                  { AOM_CDF6(390, 3333, 8138, 13419, 17675), 75 },
              },
              {
                  { AOM_CDF6(1438, 5212, 8806, 12640, 17612), 95 },
                  { AOM_CDF6(2950, 3076, 7910, 11802, 16384), 22 },
              },
              {
                  { AOM_CDF6(2048, 6400, 7680, 9472, 12288), 100 },
                  { AOM_CDF6(2712, 3390, 6102, 10169, 13559), 75 },
              },
              {
                  { AOM_CDF6(22013, 32196, 32686, 32736, 32740), 19 },
                  { AOM_CDF6(25440, 27016, 32521, 32717, 32732), 31 },
              },
              {
                  { AOM_CDF6(7585, 28552, 31957, 32673, 32724), 78 },
                  { AOM_CDF6(4, 22111, 31096, 32550, 32696), 1 },
              },
              {
                  { AOM_CDF6(12055, 26278, 30763, 32298, 32561), 90 },
                  { AOM_CDF6(4, 20893, 29384, 31964, 32534), 0 },
              },
              {
                  { AOM_CDF6(9942, 22699, 28956, 31224, 32218), 90 },
                  { AOM_CDF6(4, 15990, 26096, 30521, 32102), 0 },
              },
              {
                  { AOM_CDF6(7289, 19148, 26592, 30466, 31729), 90 },
                  { AOM_CDF6(92, 11742, 22346, 28190, 30826), 90 },
              },
              {
                  { AOM_CDF6(6358, 17829, 23866, 28606, 30931), 78 },
                  { AOM_CDF6(1898, 12645, 21679, 26717, 30071), 0 },
              },
              {
                  { AOM_CDF6(4002, 13924, 23096, 26431, 29683), 79 },
                  { AOM_CDF6(324, 10139, 16708, 23116, 26523), 14 },
              },
              {
                  { AOM_CDF6(25247, 32524, 32740, 32744, 32748), 75 },
                  { AOM_CDF6(12678, 31117, 32694, 32736, 32740), 75 },
              },
              {
                  { AOM_CDF6(17536, 31499, 32665, 32736, 32740), 78 },
                  { AOM_CDF6(41, 27760, 32458, 32733, 32737), 0 },
              },
              {
                  { AOM_CDF6(14889, 29231, 32278, 32729, 32733), 3 },
                  { AOM_CDF6(26, 24638, 31616, 32617, 32730), 0 },
              },
              {
                  { AOM_CDF6(11492, 26637, 31599, 32516, 32713), 90 },
                  { AOM_CDF6(102, 20398, 30029, 32289, 32679), 75 },
              },
              {
                  { AOM_CDF6(8508, 22676, 29590, 31571, 32464), 75 },
                  { AOM_CDF6(1358, 16850, 27388, 31584, 32502), 1 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(30812, 32458, 32672, 32720, 32732), 0 },
                  { AOM_CDF6(30812, 32458, 32672, 32720, 32732), 0 },
              },
              {
                  { AOM_CDF6(14805, 28597, 31834, 32457, 32643), 75 },
                  { AOM_CDF6(14805, 28597, 31834, 32457, 32643), 75 },
              },
              {
                  { AOM_CDF6(7885, 19026, 25996, 30110, 31734), 15 },
                  { AOM_CDF6(7885, 19026, 25996, 30110, 31734), 15 },
              },
              {
                  { AOM_CDF6(5016, 13942, 20823, 26044, 29114), 8 },
                  { AOM_CDF6(5016, 13942, 20823, 26044, 29114), 8 },
              },
              {
                  { AOM_CDF6(1976, 6346, 13934, 19218, 24119), 85 },
                  { AOM_CDF6(1976, 6346, 13934, 19218, 24119), 85 },
              },
              {
                  { AOM_CDF6(1964, 4548, 10578, 14265, 18951), 0 },
                  { AOM_CDF6(1964, 4548, 10578, 14265, 18951), 0 },
              },
              {
                  { AOM_CDF6(1365, 4642, 7646, 12379, 15110), 0 },
                  { AOM_CDF6(1365, 4642, 7646, 12379, 15110), 0 },
              },
              {
                  { AOM_CDF6(31956, 32712, 32740, 32744, 32748), 3 },
                  { AOM_CDF6(31956, 32712, 32740, 32744, 32748), 3 },
              },
              {
                  { AOM_CDF6(21806, 31381, 32627, 32722, 32732), 75 },
                  { AOM_CDF6(21806, 31381, 32627, 32722, 32732), 75 },
              },
              {
                  { AOM_CDF6(15258, 27788, 31399, 32476, 32692), 80 },
                  { AOM_CDF6(15258, 27788, 31399, 32476, 32692), 80 },
              },
              {
                  { AOM_CDF6(10991, 24246, 29922, 31791, 32374), 14 },
                  { AOM_CDF6(10991, 24246, 29922, 31791, 32374), 14 },
              },
              {
                  { AOM_CDF6(7128, 19357, 26823, 30289, 31838), 0 },
                  { AOM_CDF6(7128, 19357, 26823, 30289, 31838), 0 },
              },
#else
              {
                  { AOM_CDF6(31611, 32737, 32741, 32745, 32749), 3 },
                  { AOM_CDF6(32251, 32256, 32675, 32736, 32740), 95 },
              },
              {
                  { AOM_CDF6(18253, 31230, 32610, 32718, 32732), 115 },
                  { AOM_CDF6(3056, 15496, 31002, 32619, 32722), 0 },
              },
              {
                  { AOM_CDF6(7546, 22799, 30371, 32072, 32613), 76 },
                  { AOM_CDF6(541, 13260, 24176, 31034, 32436), 75 },
              },
              {
                  { AOM_CDF6(6260, 19563, 27290, 30127, 31790), 4 },
                  { AOM_CDF6(452, 7137, 18193, 26033, 30858), 15 },
              },
              {
                  { AOM_CDF6(2657, 8266, 17417, 22436, 27749), 100 },
                  { AOM_CDF6(2450, 7350, 14700, 24806, 28481), 75 },
              },
              {
                  { AOM_CDF6(2185, 7646, 17476, 24030, 27307), 50 },
                  { AOM_CDF6(2341, 5851, 10533, 15214, 22235), 100 },
              },
              {
                  { AOM_CDF6(5958, 8937, 11916, 23831, 29789), 0 },
                  { AOM_CDF6(5041, 7562, 17644, 20165, 22686), 0 },
              },
              {
                  { AOM_CDF6(31149, 32734, 32740, 32744, 32748), 75 },
                  { AOM_CDF6(32558, 32562, 32740, 32744, 32748), 75 },
              },
              {
                  { AOM_CDF6(17945, 31441, 32694, 32736, 32740), 0 },
                  { AOM_CDF6(13777, 28050, 32518, 32735, 32739), 80 },
              },
              {
                  { AOM_CDF6(14102, 29029, 32191, 32576, 32732), 22 },
                  { AOM_CDF6(4931, 19953, 29751, 32184, 32573), 8 },
              },
              {
                  { AOM_CDF6(10293, 25626, 29827, 31088, 32138), 100 },
                  { AOM_CDF6(5699, 18521, 27069, 30394, 32056), 75 },
              },
              {
                  { AOM_CDF6(5958, 14895, 22342, 28300, 29789), 50 },
                  { AOM_CDF6(3277, 14199, 26214, 28399, 29491), 100 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(13826, 30096, 31508, 31995, 32431), 12 },
                  { AOM_CDF6(364, 24030, 28035, 30948, 32040), 50 },
              },
              {
                  { AOM_CDF6(2024, 20665, 29011, 31585, 32126), 78 },
                  { AOM_CDF6(4, 8009, 22643, 28090, 30886), 75 },
              },
              {
                  { AOM_CDF6(9042, 19748, 26650, 29420, 30908), 0 },
                  { AOM_CDF6(4, 15104, 25370, 28952, 30726), 1 },
              },
              {
                  { AOM_CDF6(5663, 14243, 20147, 25301, 28118), 3 },
                  { AOM_CDF6(4, 8744, 17349, 24894, 28283), 77 },
              },
              {
                  { AOM_CDF6(4324, 10283, 15647, 20778, 24998), 90 },
                  { AOM_CDF6(608, 3981, 12664, 18623, 22678), 6 },
              },
              {
                  { AOM_CDF6(2178, 8215, 12957, 17634, 21210), 75 },
                  { AOM_CDF6(4, 4448, 9077, 14090, 18542), 100 },
              },
              {
                  { AOM_CDF6(2852, 7347, 11567, 15906, 18923), 0 },
                  { AOM_CDF6(4, 3584, 8589, 12570, 15662), 100 },
              },
              {
                  { AOM_CDF6(2170, 5425, 8883, 11651, 16255), 75 },
                  { AOM_CDF6(575, 2679, 6246, 9553, 12791), 3 },
              },
              {
                  { AOM_CDF6(1091, 4635, 7487, 10199, 11852), 94 },
                  { AOM_CDF6(888, 2537, 6321, 9027, 12410), 94 },
              },
              {
                  { AOM_CDF6(25512, 32470, 32602, 32710, 32732), 6 },
                  { AOM_CDF6(26203, 26914, 32437, 32680, 32727), 0 },
              },
              {
                  { AOM_CDF6(7855, 28626, 32017, 32664, 32705), 90 },
                  { AOM_CDF6(4, 24232, 31445, 32615, 32669), 6 },
              },
              {
                  { AOM_CDF6(12772, 26369, 31001, 32261, 32532), 75 },
                  { AOM_CDF6(4, 20769, 28386, 31339, 32286), 5 },
              },
              {
                  { AOM_CDF6(7429, 20433, 27496, 30476, 31733), 98 },
                  { AOM_CDF6(173, 15439, 24894, 29430, 31473), 5 },
              },
              {
                  { AOM_CDF6(5452, 15764, 23190, 27739, 30212), 95 },
                  { AOM_CDF6(35, 10613, 20093, 26065, 29483), 5 },
              },
              {
                  { AOM_CDF6(4436, 13684, 19977, 24466, 28001), 100 },
                  { AOM_CDF6(11, 10056, 17692, 23917, 28088), 75 },
              },
              {
                  { AOM_CDF6(3349, 10294, 16898, 22126, 25937), 75 },
                  { AOM_CDF6(38, 7563, 15147, 20341, 24586), 100 },
              },
              {
                  { AOM_CDF6(24231, 32550, 32740, 32744, 32748), 1 },
                  { AOM_CDF6(10035, 31049, 32696, 32736, 32740), 78 },
              },
              {
                  { AOM_CDF6(17904, 31376, 32608, 32736, 32740), 75 },
                  { AOM_CDF6(57, 28030, 32494, 32729, 32733), 5 },
              },
              {
                  { AOM_CDF6(15749, 29415, 32287, 32668, 32726), 75 },
                  { AOM_CDF6(170, 24907, 31527, 32566, 32702), 0 },
              },
              {
                  { AOM_CDF6(10203, 24118, 30238, 32080, 32562), 93 },
                  { AOM_CDF6(95, 19638, 29424, 32134, 32602), 76 },
              },
              {
                  { AOM_CDF6(6820, 19262, 26836, 30391, 31927), 90 },
                  { AOM_CDF6(633, 13427, 23701, 28986, 31432), 75 },
              },
#if TCQ_DIS_1D
              {
                  { AOM_CDF6(31727, 32641, 32740, 32744, 32748), 75 },
                  { AOM_CDF6(31727, 32641, 32740, 32744, 32748), 75 },
              },
              {
                  { AOM_CDF6(15406, 30201, 32359, 32684, 32732), 90 },
                  { AOM_CDF6(15406, 30201, 32359, 32684, 32732), 90 },
              },
              {
                  { AOM_CDF6(7790, 20673, 28093, 31356, 32248), 6 },
                  { AOM_CDF6(7790, 20673, 28093, 31356, 32248), 6 },
              },
              {
                  { AOM_CDF6(4934, 13061, 20093, 24456, 26939), 0 },
                  { AOM_CDF6(4934, 13061, 20093, 24456, 26939), 0 },
              },
              {
                  { AOM_CDF6(1586, 7752, 14710, 20348, 24664), 0 },
                  { AOM_CDF6(1586, 7752, 14710, 20348, 24664), 0 },
              },
              {
                  { AOM_CDF6(2556, 5578, 8366, 16268, 20219), 0 },
                  { AOM_CDF6(2556, 5578, 8366, 16268, 20219), 0 },
              },
              {
                  { AOM_CDF6(2521, 5041, 8822, 13233, 14494), 0 },
                  { AOM_CDF6(2521, 5041, 8822, 13233, 14494), 0 },
              },
              {
                  { AOM_CDF6(32430, 32744, 32748, 32752, 32756), 0 },
                  { AOM_CDF6(32430, 32744, 32748, 32752, 32756), 0 },
              },
              {
                  { AOM_CDF6(21293, 32055, 32715, 32736, 32740), 115 },
                  { AOM_CDF6(21293, 32055, 32715, 32736, 32740), 115 },
              },
              {
                  { AOM_CDF6(13103, 28431, 31813, 32514, 32666), 77 },
                  { AOM_CDF6(13103, 28431, 31813, 32514, 32666), 77 },
              },
              {
                  { AOM_CDF6(11926, 24738, 30229, 31941, 32473), 100 },
                  { AOM_CDF6(11926, 24738, 30229, 31941, 32473), 100 },
              },
              {
                  { AOM_CDF6(8797, 18693, 24411, 29249, 31229), 0 },
                  { AOM_CDF6(8797, 18693, 24411, 29249, 31229), 0 },
              },
#else
              {
                  { AOM_CDF6(31832, 32744, 32748, 32752, 32756), 1 },
                  { AOM_CDF6(32493, 32497, 32732, 32736, 32740), 23 },
              },
              {
                  { AOM_CDF6(20492, 31817, 32617, 32734, 32738), 116 },
                  { AOM_CDF6(3049, 17895, 31462, 32663, 32732), 1 },
              },
              {
                  { AOM_CDF6(9825, 25324, 30272, 32105, 32608), 76 },
                  { AOM_CDF6(123, 17557, 26349, 31545, 32458), 6 },
              },
              {
                  { AOM_CDF6(3772, 18097, 26886, 29688, 31799), 62 },
                  { AOM_CDF6(2875, 9572, 19210, 27345, 30971), 10 },
              },
              {
                  { AOM_CDF6(3121, 10031, 17387, 24966, 27864), 0 },
                  { AOM_CDF6(1024, 4864, 13824, 24576, 28672), 75 },
              },
              {
                  { AOM_CDF6(2809, 7490, 10299, 19661, 25278), 0 },
                  { AOM_CDF6(993, 2979, 5958, 11916, 22838), 50 },
              },
              {
                  { AOM_CDF6(5461, 8192, 10923, 19115, 21845), 0 },
                  { AOM_CDF6(4681, 9362, 14043, 18725, 23406), 0 },
              },
              {
                  { AOM_CDF6(31763, 32744, 32748, 32752, 32756), 91 },
                  { AOM_CDF6(32703, 32707, 32740, 32744, 32748), 78 },
              },
              {
                  { AOM_CDF6(21784, 31921, 32688, 32736, 32740), 115 },
                  { AOM_CDF6(12131, 29664, 32529, 32736, 32740), 76 },
              },
              {
                  { AOM_CDF6(17300, 30491, 32421, 32712, 32732), 105 },
                  { AOM_CDF6(6373, 25522, 31389, 32523, 32707), 76 },
              },
              {
                  { AOM_CDF6(12965, 25217, 29634, 31913, 32626), 20 },
                  { AOM_CDF6(1688, 21798, 28549, 31924, 32487), 35 },
              },
              {
                  { AOM_CDF6(3641, 21845, 26700, 29127, 31554), 50 },
                  { AOM_CDF6(2185, 13107, 25122, 29491, 31676), 100 },
              },
#endif
          },
          {
              {
                  { AOM_CDF6(8613, 20556, 23536, 25091, 26003), 62 },
                  { AOM_CDF6(1986, 22838, 26810, 27803, 30782), 0 },
              },
              {
                  { AOM_CDF6(6279, 18732, 26646, 30934, 31795), 15 },
                  { AOM_CDF6(4, 10543, 23137, 29084, 31111), 0 },
              },
              {
                  { AOM_CDF6(8218, 19760, 27324, 30665, 31557), 5 },
                  { AOM_CDF6(4, 12799, 23864, 26749, 29109), 0 },
              },
              {
                  { AOM_CDF6(4475, 14723, 21406, 25961, 29402), 75 },
                  { AOM_CDF6(4, 10550, 16902, 23737, 26955), 75 },
              },
              {
                  { AOM_CDF6(4448, 9585, 15110, 21430, 23401), 0 },
                  { AOM_CDF6(309, 5193, 13034, 17861, 22598), 100 },
              },
              {
                  { AOM_CDF6(1077, 7491, 14022, 17113, 20146), 25 },
                  { AOM_CDF6(2105, 6352, 11058, 15427, 20316), 0 },
              },
              {
                  { AOM_CDF6(70, 4311, 10294, 13415, 14999), 1 },
                  { AOM_CDF6(2108, 5758, 10101, 12876, 18393), 75 },
              },
              {
                  { AOM_CDF6(2564, 6809, 11633, 15439, 18753), 79 },
                  { AOM_CDF6(575, 2488, 5007, 6371, 10735), 4 },
              },
              {
                  { AOM_CDF6(723, 3507, 9011, 10667, 13029), 2 },
                  { AOM_CDF6(252, 4003, 6745, 9277, 12244), 79 },
              },
              {
                  { AOM_CDF6(21500, 31191, 32202, 32440, 32653), 11 },
                  { AOM_CDF6(11699, 26342, 32210, 32680, 32729), 15 },
              },
              {
                  { AOM_CDF6(9266, 28260, 31935, 32650, 32702), 3 },
                  { AOM_CDF6(12, 22710, 31051, 32462, 32682), 8 },
              },
              {
                  { AOM_CDF6(11689, 24665, 30573, 32137, 32475), 6 },
                  { AOM_CDF6(64, 18543, 27879, 31017, 32128), 83 },
              },
              {
                  { AOM_CDF6(7204, 20213, 26931, 30222, 31650), 75 },
                  { AOM_CDF6(12, 13666, 23220, 28445, 30662), 0 },
              },
              {
                  { AOM_CDF6(4445, 14032, 21192, 26262, 29127), 75 },
                  { AOM_CDF6(1071, 9072, 17930, 23602, 27713), 90 },
              },
              {
                  { AOM_CDF6(3859, 11533, 20546, 27129, 28995), 5 },
                  { AOM_CDF6(184, 8308, 15465, 20333, 24250), 75 },
              },
              {
                  { AOM_CDF6(2430, 7985, 14207, 19894, 23379), 78 },
                  { AOM_CDF6(85, 7758, 12921, 17137, 20512), 0 },
              },
              {
                  { AOM_CDF6(21566, 32515, 32740, 32744, 32748), 5 },
                  { AOM_CDF6(8620, 30973, 32707, 32736, 32740), 78 },
              },
              {
                  { AOM_CDF6(17279, 31216, 32596, 32736, 32740), 90 },
                  { AOM_CDF6(207, 27501, 32482, 32723, 32732), 76 },
              },
              {
                  { AOM_CDF6(13625, 28746, 32081, 32634, 32702), 90 },
                  { AOM_CDF6(137, 23469, 31179, 32458, 32670), 1 },
              },
              {
                  { AOM_CDF6(9348, 24108, 30408, 32306, 32629), 100 },
                  { AOM_CDF6(588, 17392, 27403, 31193, 32262), 75 },
              },
              {
                  { AOM_CDF6(5467, 17883, 25316, 29345, 31422), 5 },
                  { AOM_CDF6(1273, 11714, 21205, 27668, 30187), 100 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
          },
          {
              {
                  { AOM_CDF6(13160, 19266, 26298, 29021, 29957), 62 },
                  { AOM_CDF6(1092, 20753, 26214, 27307, 29491), 0 },
              },
              {
                  { AOM_CDF6(4494, 17963, 26089, 30266, 31649), 50 },
                  { AOM_CDF6(4, 11259, 24279, 30041, 31846), 5 },
              },
              {
                  { AOM_CDF6(8127, 20172, 25885, 28716, 30473), 26 },
                  { AOM_CDF6(4, 15477, 24326, 28905, 30937), 26 },
              },
              {
                  { AOM_CDF6(7212, 18933, 23539, 26142, 28510), 31 },
                  { AOM_CDF6(18, 4513, 14791, 24270, 27565), 51 },
              },
              {
                  { AOM_CDF6(4725, 13158, 18974, 22416, 25027), 0 },
                  { AOM_CDF6(4, 6667, 11649, 18165, 23220), 75 },
              },
              {
                  { AOM_CDF6(2450, 9526, 13330, 18186, 20899), 0 },
                  { AOM_CDF6(135, 5116, 9741, 15376, 18761), 1 },
              },
              {
                  { AOM_CDF6(2424, 7825, 12782, 17792, 19814), 4 },
                  { AOM_CDF6(943, 3248, 7356, 9921, 13757), 17 },
              },
              {
                  { AOM_CDF6(1923, 5841, 8602, 11234, 13778), 92 },
                  { AOM_CDF6(1261, 3199, 7476, 11149, 13961), 0 },
              },
              {
                  { AOM_CDF6(1271, 4107, 6087, 8404, 10405), 95 },
                  { AOM_CDF6(364, 2061, 4403, 6650, 9569), 17 },
              },
              {
                  { AOM_CDF6(19974, 31195, 32511, 32650, 32708), 64 },
                  { AOM_CDF6(11043, 27360, 32405, 32678, 32732), 0 },
              },
              {
                  { AOM_CDF6(11073, 28422, 32132, 32651, 32704), 0 },
                  { AOM_CDF6(91, 23170, 30711, 32209, 32618), 0 },
              },
              {
                  { AOM_CDF6(11024, 25260, 30211, 32007, 32423), 16 },
                  { AOM_CDF6(26, 20572, 28818, 31501, 32244), 26 },
              },
              {
                  { AOM_CDF6(7119, 21592, 27990, 30777, 31784), 1 },
                  { AOM_CDF6(1894, 12546, 22679, 28466, 30651), 76 },
              },
              {
                  { AOM_CDF6(5351, 15769, 22334, 27146, 29388), 75 },
                  { AOM_CDF6(288, 9553, 18635, 24945, 28121), 75 },
              },
              {
                  { AOM_CDF6(4265, 13205, 19307, 24759, 27357), 15 },
                  { AOM_CDF6(984, 7888, 15234, 20377, 24333), 1 },
              },
              {
                  { AOM_CDF6(3618, 9695, 14664, 19765, 22936), 75 },
                  { AOM_CDF6(136, 3144, 8883, 13039, 14830), 3 },
              },
              {
                  { AOM_CDF6(22563, 32398, 32730, 32736, 32740), 75 },
                  { AOM_CDF6(10900, 31406, 32732, 32736, 32740), 3 },
              },
              {
                  { AOM_CDF6(17540, 31373, 32648, 32736, 32740), 80 },
                  { AOM_CDF6(1176, 27728, 32234, 32679, 32732), 78 },
              },
              {
                  { AOM_CDF6(14607, 28847, 31996, 32616, 32713), 90 },
                  { AOM_CDF6(724, 24111, 31336, 32435, 32676), 75 },
              },
              {
                  { AOM_CDF6(9831, 24661, 30499, 32029, 32499), 0 },
                  { AOM_CDF6(995, 17805, 27641, 31300, 32308), 100 },
              },
              {
                  { AOM_CDF6(5169, 15856, 23014, 27080, 29509), 76 },
                  { AOM_CDF6(858, 11007, 19697, 25201, 28493), 76 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
              {
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
                  { AOM_CDF6(5461, 10923, 16384, 21845, 27307), 0 },
              },
          },
      },
    };

static const aom_cdf_prob av1_default_coeff_base_lf_eob_multi_cdfs
    [TOKEN_CDF_Q_CTXS][TX_SIZES][SIG_COEF_CONTEXTS_EOB]
    [CDF_SIZE(LF_BASE_SYMBOLS - 1)] = {
      {
          {
              { AOM_CDF5(25706, 30492, 31324, 31818), 31 },
              { AOM_CDF5(30435, 32242, 32513, 32588), 6 },
              { AOM_CDF5(30457, 32136, 32402, 32484), 6 },
              { AOM_CDF5(31003, 32505, 32676, 32714), 93 },
          },
          {
              { AOM_CDF5(26971, 31768, 32295, 32463), 32 },
              { AOM_CDF5(31374, 32459, 32638, 32681), 0 },
              { AOM_CDF5(31787, 32529, 32654, 32694), 75 },
              { AOM_CDF5(31845, 32521, 32709, 32740), 115 },
          },
          {
              { AOM_CDF5(23861, 30832, 31714, 32106), 62 },
              { AOM_CDF5(31998, 32582, 32684, 32714), 0 },
              { AOM_CDF5(32595, 32690, 32743, 32747), 70 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(2526, 25431, 28941, 29883), 62 },
              { AOM_CDF5(32001, 32609, 32739, 32743), 78 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(380, 9474, 22790, 27895), 50 },
              { AOM_CDF5(31118, 31976, 32602, 32703), 110 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
      },
      {
          {
              { AOM_CDF5(27737, 31996, 32445, 32576), 91 },
              { AOM_CDF5(30369, 32297, 32588, 32661), 0 },
              { AOM_CDF5(30243, 32316, 32564, 32629), 0 },
              { AOM_CDF5(30663, 32404, 32673, 32728), 75 },
          },
          {
              { AOM_CDF5(27556, 32109, 32520, 32621), 1 },
              { AOM_CDF5(32081, 32624, 32700, 32728), 75 },
              { AOM_CDF5(32088, 32612, 32712, 32735), 75 },
              { AOM_CDF5(32040, 32619, 32735, 32740), 78 },
          },
          {
              { AOM_CDF5(26819, 31724, 32283, 32522), 7 },
              { AOM_CDF5(32282, 32649, 32726, 32740), 0 },
              { AOM_CDF5(32658, 32742, 32746, 32750), 45 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(21304, 30635, 31780, 32198), 37 },
              { AOM_CDF5(32186, 32676, 32744, 32748), 115 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(12126, 24303, 28452, 30526), 62 },
              { AOM_CDF5(31647, 32479, 32695, 32740), 1 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
      },
      {
          {
              { AOM_CDF5(28639, 32047, 32463, 32605), 76 },
              { AOM_CDF5(29406, 32125, 32576, 32668), 75 },
              { AOM_CDF5(29707, 32214, 32602, 32664), 75 },
              { AOM_CDF5(28971, 32253, 32613, 32713), 75 },
          },
          {
              { AOM_CDF5(28326, 32215, 32550, 32645), 0 },
              { AOM_CDF5(32183, 32660, 32727, 32740), 75 },
              { AOM_CDF5(32136, 32641, 32726, 32740), 100 },
              { AOM_CDF5(31896, 32611, 32736, 32740), 104 },
          },
          {
              { AOM_CDF5(28120, 31988, 32441, 32602), 6 },
              { AOM_CDF5(32388, 32686, 32743, 32747), 75 },
              { AOM_CDF5(32648, 32741, 32745, 32749), 123 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(24374, 30819, 31833, 32231), 32 },
              { AOM_CDF5(32269, 32694, 32744, 32748), 90 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(22957, 29321, 31125, 31846), 62 },
              { AOM_CDF5(31994, 32641, 32740, 32744), 76 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
      },
      {
          {
              { AOM_CDF5(29534, 32281, 32481, 32541), 5 },
              { AOM_CDF5(30069, 32534, 32711, 32740), 1 },
              { AOM_CDF5(29896, 32098, 32507, 32629), 1 },
              { AOM_CDF5(30824, 32659, 32744, 32748), 36 },
          },
          {
              { AOM_CDF5(29970, 32442, 32654, 32712), 1 },
              { AOM_CDF5(32327, 32690, 32744, 32748), 75 },
              { AOM_CDF5(31949, 32625, 32744, 32748), 75 },
              { AOM_CDF5(32125, 32716, 32744, 32748), 33 },
          },
          {
              { AOM_CDF5(30437, 32423, 32627, 32709), 6 },
              { AOM_CDF5(32458, 32699, 32744, 32748), 90 },
              { AOM_CDF5(32629, 32748, 32752, 32756), 110 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(29506, 32156, 32559, 32656), 7 },
              { AOM_CDF5(32305, 32712, 32744, 32748), 90 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
          {
              { AOM_CDF5(28059, 31831, 32419, 32581), 37 },
              { AOM_CDF5(32204, 32695, 32744, 32748), 115 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
              { AOM_CDF5(6554, 13107, 19661, 26214), 0 },
          },
      },
    };

static const aom_cdf_prob av1_default_coeff_lps_lf_multi_cdfs
    [TOKEN_CDF_Q_CTXS][LF_LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)] = {
      {
          { AOM_CDF4(11134, 16178, 19899), 37 },
          { AOM_CDF4(14209, 21292, 25390), 0 },
          { AOM_CDF4(10509, 17908, 22545), 120 },
          { AOM_CDF4(7969, 14230, 18833), 122 },
          { AOM_CDF4(5595, 10344, 14337), 0 },
          { AOM_CDF4(3765, 7274, 10572), 0 },
          { AOM_CDF4(1943, 3992, 5749), 0 },
          { AOM_CDF4(13985, 20205, 23552), 6 },
          { AOM_CDF4(15212, 22618, 26486), 75 },
          { AOM_CDF4(12922, 20394, 24820), 78 },
          { AOM_CDF4(10592, 17631, 22350), 78 },
          { AOM_CDF4(8638, 15045, 19817), 78 },
          { AOM_CDF4(7247, 13014, 17636), 103 },
          { AOM_CDF4(4779, 8939, 12611), 78 },
      },
      {
          { AOM_CDF4(13088, 19263, 22982), 7 },
          { AOM_CDF4(14262, 21344, 25512), 90 },
          { AOM_CDF4(10739, 18195, 22743), 90 },
          { AOM_CDF4(8306, 14823, 19422), 93 },
          { AOM_CDF4(5893, 11028, 15278), 119 },
          { AOM_CDF4(4005, 7924, 11328), 124 },
          { AOM_CDF4(2510, 5206, 7576), 123 },
          { AOM_CDF4(16346, 22896, 25975), 0 },
          { AOM_CDF4(16946, 24317, 27877), 118 },
          { AOM_CDF4(14288, 21952, 26180), 118 },
          { AOM_CDF4(11744, 19136, 23771), 93 },
          { AOM_CDF4(9581, 16358, 21132), 93 },
          { AOM_CDF4(7955, 14043, 18682), 93 },
          { AOM_CDF4(5025, 9313, 12987), 94 },
      },
      {
          { AOM_CDF4(13254, 19857, 23719), 7 },
          { AOM_CDF4(14262, 21454, 25537), 75 },
          { AOM_CDF4(11067, 18481, 23057), 90 },
          { AOM_CDF4(8612, 15226, 19923), 75 },
          { AOM_CDF4(6093, 11413, 15998), 79 },
          { AOM_CDF4(4336, 8520, 12622), 24 },
          { AOM_CDF4(2578, 5542, 8244), 10 },
          { AOM_CDF4(18493, 25023, 27859), 76 },
          { AOM_CDF4(18020, 25395, 28759), 118 },
          { AOM_CDF4(14882, 22648, 26802), 118 },
          { AOM_CDF4(12104, 19622, 24289), 118 },
          { AOM_CDF4(9886, 16845, 21651), 118 },
          { AOM_CDF4(8242, 14551, 19269), 118 },
          { AOM_CDF4(5483, 10136, 14059), 118 },
      },
      {
          { AOM_CDF4(13824, 20203, 23865), 37 },
          { AOM_CDF4(15398, 22782, 26792), 75 },
          { AOM_CDF4(12116, 19859, 24426), 79 },
          { AOM_CDF4(9317, 16205, 21113), 85 },
          { AOM_CDF4(6217, 11714, 16106), 100 },
          { AOM_CDF4(4063, 7613, 11221), 75 },
          { AOM_CDF4(1678, 3522, 5640), 0 },
          { AOM_CDF4(21546, 27863, 30260), 20 },
          { AOM_CDF4(20447, 27486, 30198), 115 },
          { AOM_CDF4(17133, 24839, 28573), 75 },
          { AOM_CDF4(13936, 21768, 26146), 115 },
          { AOM_CDF4(11373, 18713, 23385), 115 },
          { AOM_CDF4(8981, 15618, 20382), 95 },
          { AOM_CDF4(6257, 11343, 15350), 95 },
      },
    };

static const aom_cdf_prob av1_default_coeff_base_multi_cdfs
    [TOKEN_CDF_Q_CTXS][TX_SIZES][SIG_COEF_CONTEXTS][TCQ_CTXS]
    [CDF_SIZE(NUM_BASE_LEVELS + 2)] = {
      {
          {
              {
                  { AOM_CDF4(9911, 23475, 27980), 1 },
                  { AOM_CDF4(7466, 24475, 29860), 5 },
              },
              {
                  { AOM_CDF4(7431, 20151, 26724), 0 },
                  { AOM_CDF4(3547, 15880, 24516), 0 },
              },
              {
                  { AOM_CDF4(3955, 11873, 18767), 93 },
                  { AOM_CDF4(2110, 8736, 16351), 93 },
              },
              {
                  { AOM_CDF4(2387, 8923, 14103), 98 },
                  { AOM_CDF4(1229, 6753, 12824), 98 },
              },
              {
                  { AOM_CDF4(2584, 7611, 11697), 79 },
                  { AOM_CDF4(2174, 5522, 10544), 124 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(26355, 31673, 31967), 5 },
                  { AOM_CDF4(21698, 29301, 31546), 0 },
              },
              {
                  { AOM_CDF4(10160, 21589, 27759), 115 },
                  { AOM_CDF4(9867, 21454, 27578), 75 },
              },
              {
                  { AOM_CDF4(5721, 15490, 20552), 98 },
                  { AOM_CDF4(4143, 12020, 18749), 120 },
              },
              {
                  { AOM_CDF4(3802, 8540, 13391), 76 },
                  { AOM_CDF4(1774, 6392, 11297), 96 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(25509, 32561, 32672), 98 },
                  { AOM_CDF4(12988, 31640, 32582), 93 },
              },
              {
                  { AOM_CDF4(18698, 30842, 32313), 90 },
                  { AOM_CDF4(594, 27488, 31893), 90 },
              },
              {
                  { AOM_CDF4(12530, 26543, 30912), 75 },
                  { AOM_CDF4(1486, 21670, 29502), 0 },
              },
              {
                  { AOM_CDF4(7692, 20402, 27137), 118 },
                  { AOM_CDF4(1113, 14704, 24576), 100 },
              },
              {
                  { AOM_CDF4(3558, 10943, 16907), 78 },
                  { AOM_CDF4(724, 7680, 14468), 93 },
              },
              {
                  { AOM_CDF4(26931, 32549, 32694), 103 },
                  { AOM_CDF4(15071, 31732, 32618), 78 },
              },
              {
                  { AOM_CDF4(19084, 30691, 32236), 90 },
                  { AOM_CDF4(830, 27097, 31813), 90 },
              },
              {
                  { AOM_CDF4(12249, 26154, 30609), 90 },
                  { AOM_CDF4(1280, 21242, 29138), 75 },
              },
              {
                  { AOM_CDF4(7240, 19420, 26513), 93 },
                  { AOM_CDF4(1137, 14246, 24261), 98 },
              },
              {
                  { AOM_CDF4(3820, 12097, 18705), 78 },
                  { AOM_CDF4(1134, 8005, 15383), 78 },
              },
              {
                  { AOM_CDF4(27133, 32439, 32676), 80 },
                  { AOM_CDF4(15403, 31421, 32531), 75 },
              },
              {
                  { AOM_CDF4(18195, 30297, 32087), 75 },
                  { AOM_CDF4(1523, 27081, 31571), 90 },
              },
              {
                  { AOM_CDF4(12135, 26136, 30637), 90 },
                  { AOM_CDF4(2161, 21148, 28932), 93 },
              },
              {
                  { AOM_CDF4(7592, 20384, 27162), 95 },
                  { AOM_CDF4(1282, 14451, 24183), 90 },
              },
              {
                  { AOM_CDF4(4501, 13020, 20010), 75 },
                  { AOM_CDF4(1033, 9449, 17295), 78 },
              },
              {
                  { AOM_CDF4(28771, 32520, 32691), 5 },
                  { AOM_CDF4(25566, 31723, 32508), 75 },
              },
              {
                  { AOM_CDF4(19424, 30341, 32063), 123 },
                  { AOM_CDF4(6882, 28090, 31664), 100 },
              },
              {
                  { AOM_CDF4(12826, 25901, 30379), 90 },
                  { AOM_CDF4(1770, 21646, 28894), 90 },
              },
              {
                  { AOM_CDF4(8377, 20578, 27179), 75 },
                  { AOM_CDF4(1573, 15815, 25349), 90 },
              },
              {
                  { AOM_CDF4(4649, 13924, 20629), 75 },
                  { AOM_CDF4(1503, 9789, 17292), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(28854, 32695, 32748), 115 },
                  { AOM_CDF4(12411, 32167, 32710), 120 },
              },
              {
                  { AOM_CDF4(24388, 32501, 32687), 115 },
                  { AOM_CDF4(1625, 30542, 32488), 75 },
              },
              {
                  { AOM_CDF4(19525, 31055, 32369), 75 },
                  { AOM_CDF4(2639, 27754, 31832), 90 },
              },
              {
                  { AOM_CDF4(10554, 24158, 29743), 90 },
                  { AOM_CDF4(1130, 19180, 27829), 75 },
              },
              {
                  { AOM_CDF4(3789, 10960, 16575), 118 },
                  { AOM_CDF4(520, 7283, 13933), 75 },
              },
              {
                  { AOM_CDF4(29715, 32710, 32748), 120 },
                  { AOM_CDF4(17447, 32494, 32748), 0 },
              },
              {
                  { AOM_CDF4(25092, 32480, 32689), 75 },
                  { AOM_CDF4(2354, 30996, 32522), 78 },
              },
              {
                  { AOM_CDF4(19759, 30919, 32342), 75 },
                  { AOM_CDF4(3224, 28062, 31824), 93 },
              },
              {
                  { AOM_CDF4(10218, 24406, 29721), 75 },
                  { AOM_CDF4(553, 18678, 27497), 1 },
              },
              {
                  { AOM_CDF4(3903, 11809, 18079), 90 },
                  { AOM_CDF4(876, 8023, 15164), 93 },
              },
              {
                  { AOM_CDF4(30501, 32742, 32748), 115 },
                  { AOM_CDF4(23059, 32635, 32748), 0 },
              },
              {
                  { AOM_CDF4(25215, 32497, 32719), 5 },
                  { AOM_CDF4(1312, 31296, 32598), 75 },
              },
              {
                  { AOM_CDF4(19118, 30779, 32380), 78 },
                  { AOM_CDF4(2213, 27623, 31932), 118 },
              },
              {
                  { AOM_CDF4(9961, 24369, 30152), 118 },
                  { AOM_CDF4(1179, 18947, 28027), 78 },
              },
              {
                  { AOM_CDF4(4812, 14034, 21158), 94 },
                  { AOM_CDF4(670, 9913, 18145), 78 },
              },
              {
                  { AOM_CDF4(31081, 32752, 32756), 0 },
                  { AOM_CDF4(32217, 32694, 32748), 40 },
              },
              {
                  { AOM_CDF4(25544, 32468, 32720), 90 },
                  { AOM_CDF4(12903, 32263, 32687), 0 },
              },
              {
                  { AOM_CDF4(19559, 31187, 32323), 5 },
                  { AOM_CDF4(7208, 27848, 31639), 75 },
              },
              {
                  { AOM_CDF4(8580, 23093, 29482), 7 },
                  { AOM_CDF4(3378, 19711, 27979), 2 },
              },
              {
                  { AOM_CDF4(3732, 13062, 18362), 99 },
                  { AOM_CDF4(973, 7781, 16309), 99 },
              },
          },
          {
              {
                  { AOM_CDF4(28282, 32549, 32747), 1 },
                  { AOM_CDF4(11860, 32474, 32713), 105 },
              },
              {
                  { AOM_CDF4(21596, 31855, 32593), 75 },
                  { AOM_CDF4(6499, 29302, 32354), 115 },
              },
              {
                  { AOM_CDF4(17775, 30322, 32504), 0 },
                  { AOM_CDF4(4755, 26204, 31331), 90 },
              },
              {
                  { AOM_CDF4(9759, 24406, 29260), 78 },
                  { AOM_CDF4(2287, 19324, 27443), 115 },
              },
              {
                  { AOM_CDF4(3377, 9366, 13633), 76 },
                  { AOM_CDF4(797, 6396, 12389), 1 },
              },
              {
                  { AOM_CDF4(30527, 32752, 32756), 0 },
                  { AOM_CDF4(15980, 32344, 32738), 20 },
              },
              {
                  { AOM_CDF4(25230, 32333, 32707), 78 },
                  { AOM_CDF4(2908, 31470, 32664), 91 },
              },
              {
                  { AOM_CDF4(19476, 30970, 32442), 90 },
                  { AOM_CDF4(3271, 28129, 32278), 0 },
              },
              {
                  { AOM_CDF4(11357, 25813, 30250), 5 },
                  { AOM_CDF4(4200, 20358, 28587), 76 },
              },
              {
                  { AOM_CDF4(3467, 10601, 15034), 76 },
                  { AOM_CDF4(633, 6909, 12886), 100 },
              },
              {
                  { AOM_CDF4(30357, 32752, 32756), 90 },
                  { AOM_CDF4(32487, 32636, 32748), 20 },
              },
              {
                  { AOM_CDF4(25416, 32651, 32737), 115 },
                  { AOM_CDF4(6470, 31368, 32554), 118 },
              },
              {
                  { AOM_CDF4(19673, 30970, 32437), 118 },
                  { AOM_CDF4(1222, 28493, 32131), 93 },
              },
              {
                  { AOM_CDF4(11450, 25887, 30574), 123 },
                  { AOM_CDF4(1683, 21008, 29386), 118 },
              },
              {
                  { AOM_CDF4(4818, 14687, 20709), 90 },
                  { AOM_CDF4(1164, 9850, 17767), 90 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(27354, 31913, 32483), 73 },
                  { AOM_CDF4(11852, 31374, 32536), 24 },
              },
              {
                  { AOM_CDF4(10645, 30772, 32103), 24 },
                  { AOM_CDF4(5783, 26343, 30037), 122 },
              },
              {
                  { AOM_CDF4(11886, 25540, 31804), 84 },
                  { AOM_CDF4(8908, 22588, 29428), 22 },
              },
              {
                  { AOM_CDF4(7761, 22420, 26732), 20 },
                  { AOM_CDF4(2913, 17234, 27671), 95 },
              },
              {
                  { AOM_CDF4(2602, 9025, 12603), 1 },
                  { AOM_CDF4(1285, 6184, 11324), 7 },
              },
              {
                  { AOM_CDF4(29092, 32448, 32608), 69 },
                  { AOM_CDF4(7858, 31431, 32601), 111 },
              },
              {
                  { AOM_CDF4(21517, 31362, 32487), 49 },
                  { AOM_CDF4(6411, 27639, 32626), 12 },
              },
              {
                  { AOM_CDF4(11214, 28108, 31312), 9 },
                  { AOM_CDF4(7206, 21445, 29508), 34 },
              },
              {
                  { AOM_CDF4(6995, 23748, 27798), 99 },
                  { AOM_CDF4(10454, 11660, 24727), 20 },
              },
              {
                  { AOM_CDF4(3874, 9524, 13963), 26 },
                  { AOM_CDF4(936, 6468, 12256), 6 },
              },
              {
                  { AOM_CDF4(28353, 32713, 32748), 90 },
                  { AOM_CDF4(31550, 32740, 32748), 15 },
              },
              {
                  { AOM_CDF4(25877, 32529, 32733), 109 },
                  { AOM_CDF4(8162, 31635, 32661), 119 },
              },
              {
                  { AOM_CDF4(20541, 31441, 32577), 93 },
                  { AOM_CDF4(1666, 28501, 32344), 103 },
              },
              {
                  { AOM_CDF4(12767, 27183, 31213), 93 },
                  { AOM_CDF4(2261, 22344, 30204), 100 },
              },
              {
                  { AOM_CDF4(4507, 13756, 19425), 120 },
                  { AOM_CDF4(1320, 9309, 16783), 115 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF4(11002, 24278, 28599), 30 },
                  { AOM_CDF4(7635, 25247, 30573), 30 },
              },
              {
                  { AOM_CDF4(9342, 22895, 28499), 5 },
                  { AOM_CDF4(2898, 18765, 27310), 0 },
              },
              {
                  { AOM_CDF4(4722, 14185, 21265), 0 },
                  { AOM_CDF4(1870, 9779, 17974), 3 },
              },
              {
                  { AOM_CDF4(2765, 9205, 15256), 93 },
                  { AOM_CDF4(906, 6582, 12719), 120 },
              },
              {
                  { AOM_CDF4(1502, 5720, 9797), 93 },
                  { AOM_CDF4(898, 3846, 7965), 93 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(26827, 32314, 32367), 0 },
                  { AOM_CDF4(22720, 29196, 31720), 78 },
              },
              {
                  { AOM_CDF4(9789, 22978, 28620), 75 },
                  { AOM_CDF4(9490, 21290, 28388), 75 },
              },
              {
                  { AOM_CDF4(5484, 15035, 21213), 93 },
                  { AOM_CDF4(3766, 11712, 18984), 91 },
              },
              {
                  { AOM_CDF4(1491, 6548, 11124), 3 },
                  { AOM_CDF4(618, 5212, 10994), 120 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(26269, 32250, 32581), 90 },
                  { AOM_CDF4(12884, 31467, 32486), 78 },
              },
              {
                  { AOM_CDF4(20239, 31267, 32400), 75 },
                  { AOM_CDF4(672, 28214, 32024), 75 },
              },
              {
                  { AOM_CDF4(15311, 28636, 31503), 75 },
                  { AOM_CDF4(1992, 24440, 30564), 0 },
              },
              {
                  { AOM_CDF4(9111, 22219, 28171), 78 },
                  { AOM_CDF4(983, 16993, 26154), 0 },
              },
              {
                  { AOM_CDF4(4051, 11724, 17568), 3 },
                  { AOM_CDF4(999, 8044, 15026), 3 },
              },
              {
                  { AOM_CDF4(26941, 32326, 32613), 5 },
                  { AOM_CDF4(13971, 31827, 32558), 90 },
              },
              {
                  { AOM_CDF4(21181, 31492, 32450), 75 },
                  { AOM_CDF4(721, 29088, 32150), 75 },
              },
              {
                  { AOM_CDF4(16062, 29048, 31631), 0 },
                  { AOM_CDF4(2165, 25111, 30769), 0 },
              },
              {
                  { AOM_CDF4(8958, 22223, 28345), 3 },
                  { AOM_CDF4(870, 17027, 26234), 0 },
              },
              {
                  { AOM_CDF4(4338, 12665, 19168), 3 },
                  { AOM_CDF4(565, 8976, 16711), 3 },
              },
              {
                  { AOM_CDF4(28370, 32559, 32688), 30 },
                  { AOM_CDF4(15977, 31944, 32627), 5 },
              },
              {
                  { AOM_CDF4(22467, 31867, 32552), 75 },
                  { AOM_CDF4(2046, 30051, 32343), 75 },
              },
              {
                  { AOM_CDF4(17466, 29933, 32033), 0 },
                  { AOM_CDF4(1991, 26676, 31416), 0 },
              },
              {
                  { AOM_CDF4(9933, 23928, 29552), 3 },
                  { AOM_CDF4(1613, 19041, 27807), 3 },
              },
              {
                  { AOM_CDF4(4931, 14533, 21644), 78 },
                  { AOM_CDF4(969, 10299, 18631), 3 },
              },
              {
                  { AOM_CDF4(29960, 32673, 32716), 0 },
                  { AOM_CDF4(26617, 32170, 32597), 75 },
              },
              {
                  { AOM_CDF4(21657, 31204, 32351), 75 },
                  { AOM_CDF4(6501, 28784, 32022), 75 },
              },
              {
                  { AOM_CDF4(15333, 27691, 30969), 0 },
                  { AOM_CDF4(754, 22911, 29565), 0 },
              },
              {
                  { AOM_CDF4(9040, 21696, 27445), 0 },
                  { AOM_CDF4(1340, 16493, 25380), 0 },
              },
              {
                  { AOM_CDF4(4165, 12290, 18104), 75 },
                  { AOM_CDF4(717, 8616, 15740), 90 },
              },
          },
          {
              {
                  { AOM_CDF4(28102, 32642, 32747), 75 },
                  { AOM_CDF4(11192, 32121, 32682), 75 },
              },
              {
                  { AOM_CDF4(22720, 32094, 32631), 75 },
                  { AOM_CDF4(1029, 29647, 32431), 75 },
              },
              {
                  { AOM_CDF4(17617, 30103, 32178), 75 },
                  { AOM_CDF4(2186, 26319, 31518), 75 },
              },
              {
                  { AOM_CDF4(10164, 24321, 29798), 75 },
                  { AOM_CDF4(1541, 18791, 27852), 78 },
              },
              {
                  { AOM_CDF4(4569, 13369, 19570), 75 },
                  { AOM_CDF4(430, 9398, 17094), 0 },
              },
              {
                  { AOM_CDF4(29415, 32696, 32748), 75 },
                  { AOM_CDF4(17471, 32468, 32725), 0 },
              },
              {
                  { AOM_CDF4(24255, 32400, 32684), 75 },
                  { AOM_CDF4(2056, 30672, 32547), 75 },
              },
              {
                  { AOM_CDF4(18920, 30859, 32391), 0 },
                  { AOM_CDF4(1658, 27471, 31907), 0 },
              },
              {
                  { AOM_CDF4(10691, 25259, 30411), 93 },
                  { AOM_CDF4(1318, 19777, 28708), 93 },
              },
              {
                  { AOM_CDF4(4853, 14405, 21019), 75 },
                  { AOM_CDF4(552, 10170, 18305), 0 },
              },
              {
                  { AOM_CDF4(30584, 32731, 32748), 10 },
                  { AOM_CDF4(23983, 32663, 32748), 0 },
              },
              {
                  { AOM_CDF4(26226, 32602, 32732), 80 },
                  { AOM_CDF4(2213, 31721, 32632), 0 },
              },
              {
                  { AOM_CDF4(21116, 31671, 32553), 5 },
                  { AOM_CDF4(3052, 29248, 32288), 78 },
              },
              {
                  { AOM_CDF4(12457, 27032, 31218), 3 },
                  { AOM_CDF4(1152, 21857, 29902), 78 },
              },
              {
                  { AOM_CDF4(5938, 16655, 23702), 3 },
                  { AOM_CDF4(470, 12098, 20941), 0 },
              },
              {
                  { AOM_CDF4(31236, 32752, 32756), 75 },
                  { AOM_CDF4(32626, 32657, 32747), 115 },
              },
              {
                  { AOM_CDF4(24830, 32210, 32643), 75 },
                  { AOM_CDF4(11977, 30847, 32503), 93 },
              },
              {
                  { AOM_CDF4(17202, 29314, 31855), 75 },
                  { AOM_CDF4(3761, 25105, 30992), 75 },
              },
              {
                  { AOM_CDF4(8894, 22748, 28997), 90 },
                  { AOM_CDF4(1417, 18345, 27236), 18 },
              },
              {
                  { AOM_CDF4(4554, 14484, 21831), 78 },
                  { AOM_CDF4(1276, 11326, 19495), 98 },
              },
          },
          {
              {
                  { AOM_CDF4(28429, 32590, 32748), 0 },
                  { AOM_CDF4(10471, 32353, 32722), 91 },
              },
              {
                  { AOM_CDF4(23081, 32002, 32639), 78 },
                  { AOM_CDF4(2638, 30460, 32556), 76 },
              },
              {
                  { AOM_CDF4(19093, 30527, 32281), 75 },
                  { AOM_CDF4(1502, 27612, 31946), 76 },
              },
              {
                  { AOM_CDF4(10247, 24716, 29895), 90 },
                  { AOM_CDF4(2487, 19280, 27993), 15 },
              },
              {
                  { AOM_CDF4(4115, 11673, 17004), 93 },
                  { AOM_CDF4(294, 8279, 15142), 1 },
              },
              {
                  { AOM_CDF4(30029, 32682, 32748), 75 },
                  { AOM_CDF4(16134, 32602, 32748), 76 },
              },
              {
                  { AOM_CDF4(26307, 32563, 32721), 76 },
                  { AOM_CDF4(2427, 31584, 32634), 76 },
              },
              {
                  { AOM_CDF4(19996, 30995, 32404), 1 },
                  { AOM_CDF4(3513, 27942, 31889), 1 },
              },
              {
                  { AOM_CDF4(10612, 24825, 29989), 75 },
                  { AOM_CDF4(1047, 19492, 28275), 1 },
              },
              {
                  { AOM_CDF4(4242, 13259, 19184), 1 },
                  { AOM_CDF4(1172, 9168, 16321), 78 },
              },
              {
                  { AOM_CDF4(31457, 32729, 32748), 10 },
                  { AOM_CDF4(30422, 32660, 32748), 75 },
              },
              {
                  { AOM_CDF4(27779, 32641, 32736), 75 },
                  { AOM_CDF4(5287, 31840, 32647), 3 },
              },
              {
                  { AOM_CDF4(21913, 31552, 32527), 5 },
                  { AOM_CDF4(2875, 29189, 32244), 75 },
              },
              {
                  { AOM_CDF4(12313, 26713, 31050), 78 },
                  { AOM_CDF4(1321, 21630, 29645), 78 },
              },
              {
                  { AOM_CDF4(5733, 16109, 22581), 118 },
                  { AOM_CDF4(1094, 11550, 19983), 118 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(28836, 32608, 32748), 76 },
                  { AOM_CDF4(12041, 31811, 32704), 78 },
              },
              {
                  { AOM_CDF4(21729, 31640, 32585), 75 },
                  { AOM_CDF4(1474, 29481, 32423), 0 },
              },
              {
                  { AOM_CDF4(17033, 29575, 31908), 31 },
                  { AOM_CDF4(3501, 25200, 30759), 76 },
              },
              {
                  { AOM_CDF4(9052, 22321, 27988), 1 },
                  { AOM_CDF4(192, 17125, 25929), 31 },
              },
              {
                  { AOM_CDF4(2920, 9043, 13114), 75 },
                  { AOM_CDF4(553, 5873, 11221), 90 },
              },
              {
                  { AOM_CDF4(30400, 32727, 32748), 30 },
                  { AOM_CDF4(15723, 32253, 32735), 0 },
              },
              {
                  { AOM_CDF4(23657, 31953, 32640), 1 },
                  { AOM_CDF4(1747, 30564, 32572), 31 },
              },
              {
                  { AOM_CDF4(17630, 29726, 31946), 6 },
                  { AOM_CDF4(3860, 26071, 31138), 6 },
              },
              {
                  { AOM_CDF4(8637, 22828, 28646), 75 },
                  { AOM_CDF4(2949, 17650, 26557), 75 },
              },
              {
                  { AOM_CDF4(3507, 10250, 15354), 75 },
                  { AOM_CDF4(316, 7273, 13420), 0 },
              },
              {
                  { AOM_CDF4(31156, 32752, 32756), 75 },
                  { AOM_CDF4(32095, 32698, 32748), 95 },
              },
              {
                  { AOM_CDF4(26799, 32544, 32715), 0 },
                  { AOM_CDF4(7084, 31575, 32632), 83 },
              },
              {
                  { AOM_CDF4(20240, 31046, 32441), 78 },
                  { AOM_CDF4(2070, 28001, 32005), 93 },
              },
              {
                  { AOM_CDF4(11710, 25938, 30699), 93 },
                  { AOM_CDF4(708, 20558, 29036), 93 },
              },
              {
                  { AOM_CDF4(5423, 15197, 21637), 118 },
                  { AOM_CDF4(496, 10569, 18629), 118 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF4(16326, 28983, 31911), 0 },
                  { AOM_CDF4(7616, 27309, 32152), 5 },
              },
              {
                  { AOM_CDF4(11910, 28533, 32025), 0 },
                  { AOM_CDF4(1018, 24707, 31314), 0 },
              },
              {
                  { AOM_CDF4(7809, 21268, 28992), 0 },
                  { AOM_CDF4(1435, 13270, 25053), 18 },
              },
              {
                  { AOM_CDF4(4700, 14253, 20551), 99 },
                  { AOM_CDF4(888, 9263, 17374), 20 },
              },
              {
                  { AOM_CDF4(2877, 7004, 14133), 0 },
                  { AOM_CDF4(1725, 5605, 12360), 25 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(30989, 32644, 32658), 16 },
                  { AOM_CDF4(29010, 30211, 32249), 81 },
              },
              {
                  { AOM_CDF4(9968, 24258, 30431), 90 },
                  { AOM_CDF4(17080, 23752, 30728), 78 },
              },
              {
                  { AOM_CDF4(8203, 19415, 25530), 75 },
                  { AOM_CDF4(4388, 15905, 23703), 121 },
              },
              {
                  { AOM_CDF4(2002, 9976, 18563), 20 },
                  { AOM_CDF4(833, 7425, 15001), 120 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(26444, 32662, 32717), 75 },
                  { AOM_CDF4(12140, 31769, 32608), 75 },
              },
              {
                  { AOM_CDF4(19825, 31746, 32528), 90 },
                  { AOM_CDF4(172, 28082, 32222), 75 },
              },
              {
                  { AOM_CDF4(15038, 28799, 31837), 78 },
                  { AOM_CDF4(246, 23903, 30926), 75 },
              },
              {
                  { AOM_CDF4(9459, 23394, 29521), 93 },
                  { AOM_CDF4(392, 17523, 27428), 78 },
              },
              {
                  { AOM_CDF4(5383, 15157, 22039), 78 },
                  { AOM_CDF4(483, 10281, 19547), 78 },
              },
              {
                  { AOM_CDF4(27957, 32692, 32742), 0 },
                  { AOM_CDF4(16068, 32186, 32674), 15 },
              },
              {
                  { AOM_CDF4(21189, 32040, 32596), 78 },
                  { AOM_CDF4(519, 29230, 32357), 75 },
              },
              {
                  { AOM_CDF4(15518, 29451, 32090), 3 },
                  { AOM_CDF4(518, 24257, 31235), 75 },
              },
              {
                  { AOM_CDF4(9479, 23708, 29840), 93 },
                  { AOM_CDF4(356, 17554, 27711), 75 },
              },
              {
                  { AOM_CDF4(5200, 15798, 23717), 78 },
                  { AOM_CDF4(317, 11756, 21578), 78 },
              },
              {
                  { AOM_CDF4(28801, 32711, 32748), 0 },
                  { AOM_CDF4(15606, 32106, 32682), 5 },
              },
              {
                  { AOM_CDF4(20809, 32112, 32618), 3 },
                  { AOM_CDF4(439, 29522, 32418), 0 },
              },
              {
                  { AOM_CDF4(15143, 29596, 32281), 93 },
                  { AOM_CDF4(421, 25032, 31540), 75 },
              },
              {
                  { AOM_CDF4(9887, 24635, 30595), 93 },
                  { AOM_CDF4(680, 18846, 28698), 93 },
              },
              {
                  { AOM_CDF4(5863, 17830, 25557), 119 },
                  { AOM_CDF4(492, 12641, 23054), 78 },
              },
              {
                  { AOM_CDF4(29938, 32717, 32734), 0 },
                  { AOM_CDF4(31527, 32422, 32653), 90 },
              },
              {
                  { AOM_CDF4(20011, 31604, 32508), 90 },
                  { AOM_CDF4(7437, 28695, 32303), 75 },
              },
              {
                  { AOM_CDF4(14508, 28284, 31574), 75 },
                  { AOM_CDF4(205, 22797, 30247), 75 },
              },
              {
                  { AOM_CDF4(9692, 23077, 28850), 75 },
                  { AOM_CDF4(250, 17402, 26850), 0 },
              },
              {
                  { AOM_CDF4(5632, 16478, 23602), 75 },
                  { AOM_CDF4(269, 11438, 20122), 75 },
              },
          },
          {
              {
                  { AOM_CDF4(28336, 32677, 32744), 75 },
                  { AOM_CDF4(11298, 32165, 32677), 75 },
              },
              {
                  { AOM_CDF4(22501, 32227, 32643), 93 },
                  { AOM_CDF4(187, 29940, 32482), 5 },
              },
              {
                  { AOM_CDF4(18859, 30771, 32385), 75 },
                  { AOM_CDF4(827, 27322, 31931), 0 },
              },
              {
                  { AOM_CDF4(11313, 25752, 30760), 78 },
                  { AOM_CDF4(865, 20203, 29028), 78 },
              },
              {
                  { AOM_CDF4(5819, 16740, 23829), 75 },
                  { AOM_CDF4(382, 11856, 20933), 78 },
              },
              {
                  { AOM_CDF4(29494, 32716, 32748), 75 },
                  { AOM_CDF4(22811, 32563, 32726), 75 },
              },
              {
                  { AOM_CDF4(24040, 32572, 32706), 75 },
                  { AOM_CDF4(671, 31036, 32587), 75 },
              },
              {
                  { AOM_CDF4(20384, 31538, 32546), 0 },
                  { AOM_CDF4(520, 28431, 32214), 0 },
              },
              {
                  { AOM_CDF4(12468, 27111, 31300), 75 },
                  { AOM_CDF4(855, 21229, 29823), 78 },
              },
              {
                  { AOM_CDF4(6292, 18163, 25251), 75 },
                  { AOM_CDF4(297, 12949, 22469), 90 },
              },
              {
                  { AOM_CDF4(29956, 32739, 32748), 5 },
                  { AOM_CDF4(25511, 32678, 32747), 0 },
              },
              {
                  { AOM_CDF4(25094, 32653, 32727), 75 },
                  { AOM_CDF4(1027, 31569, 32601), 75 },
              },
              {
                  { AOM_CDF4(21341, 31980, 32614), 75 },
                  { AOM_CDF4(1222, 29205, 32367), 78 },
              },
              {
                  { AOM_CDF4(13596, 28307, 31818), 93 },
                  { AOM_CDF4(463, 22903, 30766), 78 },
              },
              {
                  { AOM_CDF4(7257, 20011, 27200), 78 },
                  { AOM_CDF4(243, 14888, 24930), 75 },
              },
              {
                  { AOM_CDF4(30263, 32752, 32756), 5 },
                  { AOM_CDF4(32687, 32691, 32736), 75 },
              },
              {
                  { AOM_CDF4(24209, 32619, 32710), 100 },
                  { AOM_CDF4(5284, 31291, 32536), 90 },
              },
              {
                  { AOM_CDF4(19815, 31729, 32509), 75 },
                  { AOM_CDF4(727, 27808, 32167), 78 },
              },
              {
                  { AOM_CDF4(14179, 29447, 32088), 75 },
                  { AOM_CDF4(431, 23037, 30646), 90 },
              },
              {
                  { AOM_CDF4(7559, 20896, 27797), 75 },
                  { AOM_CDF4(968, 15248, 25120), 75 },
              },
          },
          {
              {
                  { AOM_CDF4(28071, 32641, 32748), 1 },
                  { AOM_CDF4(8252, 32402, 32724), 0 },
              },
              {
                  { AOM_CDF4(22806, 32096, 32661), 75 },
                  { AOM_CDF4(1629, 30429, 32579), 80 },
              },
              {
                  { AOM_CDF4(18671, 30613, 32354), 0 },
                  { AOM_CDF4(914, 27565, 32050), 80 },
              },
              {
                  { AOM_CDF4(11122, 25437, 30413), 90 },
                  { AOM_CDF4(1038, 20145, 28882), 90 },
              },
              {
                  { AOM_CDF4(4570, 12828, 18535), 75 },
                  { AOM_CDF4(118, 9801, 17797), 76 },
              },
              {
                  { AOM_CDF4(28991, 32683, 32748), 0 },
                  { AOM_CDF4(15477, 32477, 32734), 0 },
              },
              {
                  { AOM_CDF4(24905, 32598, 32731), 1 },
                  { AOM_CDF4(654, 31461, 32635), 76 },
              },
              {
                  { AOM_CDF4(19448, 30956, 32442), 90 },
                  { AOM_CDF4(1943, 27652, 31993), 75 },
              },
              {
                  { AOM_CDF4(10931, 25186, 30249), 75 },
                  { AOM_CDF4(324, 20290, 29116), 81 },
              },
              {
                  { AOM_CDF4(4786, 15004, 21436), 1 },
                  { AOM_CDF4(620, 10260, 18119), 75 },
              },
              {
                  { AOM_CDF4(30637, 32738, 32748), 0 },
                  { AOM_CDF4(31051, 32680, 32748), 75 },
              },
              {
                  { AOM_CDF4(26531, 32666, 32740), 90 },
                  { AOM_CDF4(3541, 31673, 32651), 93 },
              },
              {
                  { AOM_CDF4(21778, 31813, 32600), 75 },
                  { AOM_CDF4(1449, 29130, 32312), 93 },
              },
              {
                  { AOM_CDF4(13573, 27879, 31575), 118 },
                  { AOM_CDF4(512, 22769, 30465), 75 },
              },
              {
                  { AOM_CDF4(6758, 18214, 24828), 118 },
                  { AOM_CDF4(596, 13133, 22284), 93 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(29445, 32681, 32748), 6 },
                  { AOM_CDF4(11704, 32239, 32721), 5 },
              },
              {
                  { AOM_CDF4(23524, 32207, 32669), 80 },
                  { AOM_CDF4(825, 30714, 32552), 15 },
              },
              {
                  { AOM_CDF4(19561, 31160, 32352), 1 },
                  { AOM_CDF4(1954, 27112, 31684), 1 },
              },
              {
                  { AOM_CDF4(10514, 24500, 29706), 76 },
                  { AOM_CDF4(459, 20277, 28132), 6 },
              },
              {
                  { AOM_CDF4(4027, 11634, 16675), 76 },
                  { AOM_CDF4(302, 7940, 14728), 76 },
              },
              {
                  { AOM_CDF4(30990, 32726, 32748), 76 },
                  { AOM_CDF4(13751, 32427, 32744), 15 },
              },
              {
                  { AOM_CDF4(24701, 32333, 32698), 76 },
                  { AOM_CDF4(1865, 31436, 32660), 0 },
              },
              {
                  { AOM_CDF4(18571, 30647, 32260), 26 },
                  { AOM_CDF4(1470, 27002, 31702), 31 },
              },
              {
                  { AOM_CDF4(10161, 24255, 29484), 75 },
                  { AOM_CDF4(1106, 18944, 27931), 0 },
              },
              {
                  { AOM_CDF4(4526, 12987, 18916), 75 },
                  { AOM_CDF4(190, 9362, 17078), 1 },
              },
              {
                  { AOM_CDF4(30893, 32752, 32756), 80 },
                  { AOM_CDF4(32376, 32674, 32748), 75 },
              },
              {
                  { AOM_CDF4(26541, 32626, 32731), 5 },
                  { AOM_CDF4(4703, 31625, 32658), 90 },
              },
              {
                  { AOM_CDF4(20317, 31425, 32556), 0 },
                  { AOM_CDF4(775, 28219, 32158), 83 },
              },
              {
                  { AOM_CDF4(12426, 26935, 31266), 118 },
                  { AOM_CDF4(416, 21790, 29983), 75 },
              },
              {
                  { AOM_CDF4(6506, 17745, 24684), 75 },
                  { AOM_CDF4(90, 12713, 21792), 81 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
      },
      {
          {
              {
                  { AOM_CDF4(17246, 29319, 31043), 0 },
                  { AOM_CDF4(18264, 31336, 32589), 45 },
              },
              {
                  { AOM_CDF4(16160, 31421, 32319), 25 },
                  { AOM_CDF4(2789, 29979, 32071), 50 },
              },
              {
                  { AOM_CDF4(10923, 21845, 27307), 0 },
                  { AOM_CDF4(10923, 16384, 27307), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(30878, 31508, 32138), 100 },
                  { AOM_CDF4(29943, 31073, 32203), 100 },
              },
              {
                  { AOM_CDF4(22599, 30508, 31638), 25 },
                  { AOM_CDF4(24986, 30720, 32358), 50 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(27138, 32658, 32748), 78 },
                  { AOM_CDF4(12393, 32378, 32729), 75 },
              },
              {
                  { AOM_CDF4(22187, 32461, 32727), 93 },
                  { AOM_CDF4(150, 30941, 32665), 0 },
              },
              {
                  { AOM_CDF4(17338, 31331, 32662), 93 },
                  { AOM_CDF4(2202, 27719, 32467), 3 },
              },
              {
                  { AOM_CDF4(13758, 29699, 32413), 90 },
                  { AOM_CDF4(1079, 25405, 31906), 90 },
              },
              {
                  { AOM_CDF4(11365, 25609, 31366), 22 },
                  { AOM_CDF4(1451, 17186, 29102), 12 },
              },
              {
                  { AOM_CDF4(29971, 32735, 32748), 0 },
                  { AOM_CDF4(13825, 32566, 32748), 3 },
              },
              {
                  { AOM_CDF4(24172, 32629, 32748), 78 },
                  { AOM_CDF4(1297, 31983, 32725), 0 },
              },
              {
                  { AOM_CDF4(16654, 31941, 32723), 78 },
                  { AOM_CDF4(3318, 28618, 32553), 93 },
              },
              {
                  { AOM_CDF4(12270, 30242, 32551), 5 },
                  { AOM_CDF4(10574, 22775, 31722), 21 },
              },
              {
                  { AOM_CDF4(10923, 22838, 30782), 50 },
                  { AOM_CDF4(5174, 21270, 28744), 100 },
              },
              {
                  { AOM_CDF4(31048, 32752, 32756), 3 },
                  { AOM_CDF4(22367, 32622, 32748), 18 },
              },
              {
                  { AOM_CDF4(22974, 32701, 32748), 19 },
                  { AOM_CDF4(2681, 32393, 32748), 0 },
              },
              {
                  { AOM_CDF4(17292, 32314, 32748), 0 },
                  { AOM_CDF4(8144, 29399, 32623), 100 },
              },
              {
                  { AOM_CDF4(13392, 29206, 32198), 10 },
                  { AOM_CDF4(4725, 27586, 32158), 0 },
              },
              {
                  { AOM_CDF4(9830, 22938, 29491), 100 },
                  { AOM_CDF4(12288, 16384, 28672), 0 },
              },
              {
                  { AOM_CDF4(31998, 32752, 32756), 0 },
                  { AOM_CDF4(32571, 32686, 32748), 75 },
              },
              {
                  { AOM_CDF4(19342, 32307, 32748), 75 },
                  { AOM_CDF4(18144, 30296, 32668), 100 },
              },
              {
                  { AOM_CDF4(15433, 30457, 32507), 19 },
                  { AOM_CDF4(2680, 25132, 31605), 19 },
              },
              {
                  { AOM_CDF4(13717, 25529, 32006), 0 },
                  { AOM_CDF4(6554, 22359, 30069), 0 },
              },
              {
                  { AOM_CDF4(8937, 20852, 26810), 100 },
                  { AOM_CDF4(2979, 17873, 26810), 100 },
              },
          },
          {
              {
                  { AOM_CDF4(27773, 32697, 32748), 75 },
                  { AOM_CDF4(10530, 32415, 32721), 0 },
              },
              {
                  { AOM_CDF4(22306, 32332, 32705), 93 },
                  { AOM_CDF4(492, 30902, 32645), 75 },
              },
              {
                  { AOM_CDF4(18533, 31223, 32571), 90 },
                  { AOM_CDF4(1465, 28239, 32330), 15 },
              },
              {
                  { AOM_CDF4(11912, 27520, 31690), 78 },
                  { AOM_CDF4(1661, 21597, 30499), 18 },
              },
              {
                  { AOM_CDF4(8998, 22965, 29595), 75 },
                  { AOM_CDF4(544, 17233, 27639), 76 },
              },
              {
                  { AOM_CDF4(29374, 32740, 32748), 75 },
                  { AOM_CDF4(25268, 32443, 32748), 78 },
              },
              {
                  { AOM_CDF4(24227, 32633, 32745), 75 },
                  { AOM_CDF4(1308, 31731, 32699), 5 },
              },
              {
                  { AOM_CDF4(19766, 31677, 32647), 0 },
                  { AOM_CDF4(2057, 28914, 32486), 75 },
              },
              {
                  { AOM_CDF4(14000, 29870, 32447), 91 },
                  { AOM_CDF4(1783, 23278, 31313), 75 },
              },
              {
                  { AOM_CDF4(9983, 26232, 31448), 1 },
                  { AOM_CDF4(2513, 18611, 29499), 1 },
              },
              {
                  { AOM_CDF4(30685, 32748, 32752), 75 },
                  { AOM_CDF4(28900, 32694, 32748), 0 },
              },
              {
                  { AOM_CDF4(25687, 32634, 32748), 78 },
                  { AOM_CDF4(950, 32482, 32736), 75 },
              },
              {
                  { AOM_CDF4(21265, 32494, 32735), 75 },
                  { AOM_CDF4(3365, 30027, 32611), 75 },
              },
              {
                  { AOM_CDF4(14858, 30623, 32606), 120 },
                  { AOM_CDF4(614, 25899, 32225), 90 },
              },
              {
                  { AOM_CDF4(11020, 27781, 32373), 6 },
                  { AOM_CDF4(1422, 21158, 30766), 76 },
              },
              {
                  { AOM_CDF4(31900, 32752, 32756), 78 },
                  { AOM_CDF4(32717, 32721, 32748), 83 },
              },
              {
                  { AOM_CDF4(24484, 32572, 32748), 100 },
                  { AOM_CDF4(11600, 31143, 32727), 90 },
              },
              {
                  { AOM_CDF4(20370, 31703, 32646), 5 },
                  { AOM_CDF4(8150, 28986, 32326), 80 },
              },
              {
                  { AOM_CDF4(13954, 28407, 32020), 85 },
                  { AOM_CDF4(3765, 28027, 31931), 10 },
              },
              {
                  { AOM_CDF4(9362, 21065, 30427), 100 },
                  { AOM_CDF4(5783, 21203, 30840), 100 },
              },
          },
          {
              {
                  { AOM_CDF4(25137, 32689, 32748), 0 },
                  { AOM_CDF4(6127, 32348, 32724), 0 },
              },
              {
                  { AOM_CDF4(20809, 32284, 32699), 78 },
                  { AOM_CDF4(1019, 29915, 32595), 5 },
              },
              {
                  { AOM_CDF4(16613, 30801, 32529), 75 },
                  { AOM_CDF4(1173, 26024, 31985), 75 },
              },
              {
                  { AOM_CDF4(12141, 27579, 31620), 75 },
                  { AOM_CDF4(1226, 21070, 30286), 115 },
              },
              {
                  { AOM_CDF4(7083, 18725, 25849), 78 },
                  { AOM_CDF4(384, 13555, 24555), 0 },
              },
              {
                  { AOM_CDF4(27008, 32736, 32748), 1 },
                  { AOM_CDF4(19776, 32128, 32739), 83 },
              },
              {
                  { AOM_CDF4(23453, 32635, 32747), 81 },
                  { AOM_CDF4(3719, 31367, 32695), 80 },
              },
              {
                  { AOM_CDF4(18497, 31026, 32515), 101 },
                  { AOM_CDF4(1990, 28272, 32320), 6 },
              },
              {
                  { AOM_CDF4(12323, 27014, 31453), 75 },
                  { AOM_CDF4(922, 22322, 30653), 75 },
              },
              {
                  { AOM_CDF4(7724, 20172, 27333), 1 },
                  { AOM_CDF4(415, 16481, 25566), 1 },
              },
              {
                  { AOM_CDF4(30828, 32752, 32756), 75 },
                  { AOM_CDF4(32672, 32676, 32748), 115 },
              },
              {
                  { AOM_CDF4(26418, 32658, 32748), 115 },
                  { AOM_CDF4(7696, 32008, 32719), 93 },
              },
              {
                  { AOM_CDF4(22808, 32248, 32688), 90 },
                  { AOM_CDF4(3603, 30161, 32502), 90 },
              },
              {
                  { AOM_CDF4(15554, 30362, 32398), 76 },
                  { AOM_CDF4(1529, 24994, 31482), 93 },
              },
              {
                  { AOM_CDF4(9693, 24090, 30054), 1 },
                  { AOM_CDF4(2310, 18771, 29258), 76 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
          {
              {
                  { AOM_CDF4(26470, 32729, 32748), 7 },
                  { AOM_CDF4(10395, 32344, 32744), 8 },
              },
              {
                  { AOM_CDF4(22143, 32489, 32734), 5 },
                  { AOM_CDF4(990, 30444, 32617), 0 },
              },
              {
                  { AOM_CDF4(17125, 30676, 32518), 75 },
                  { AOM_CDF4(721, 27147, 32180), 75 },
              },
              {
                  { AOM_CDF4(12046, 26628, 31217), 76 },
                  { AOM_CDF4(684, 21586, 30135), 76 },
              },
              {
                  { AOM_CDF4(5901, 17039, 23630), 1 },
                  { AOM_CDF4(818, 11831, 20903), 76 },
              },
              {
                  { AOM_CDF4(28247, 32689, 32748), 31 },
                  { AOM_CDF4(11599, 32409, 32744), 0 },
              },
              {
                  { AOM_CDF4(22247, 32322, 32721), 90 },
                  { AOM_CDF4(2155, 31402, 32701), 31 },
              },
              {
                  { AOM_CDF4(16573, 31360, 32594), 6 },
                  { AOM_CDF4(3791, 27059, 32065), 6 },
              },
              {
                  { AOM_CDF4(10980, 26048, 30979), 1 },
                  { AOM_CDF4(636, 20748, 29525), 1 },
              },
              {
                  { AOM_CDF4(6843, 18269, 25436), 0 },
                  { AOM_CDF4(899, 13081, 23222), 76 },
              },
              {
                  { AOM_CDF4(30221, 32752, 32756), 80 },
                  { AOM_CDF4(32594, 32617, 32748), 100 },
              },
              {
                  { AOM_CDF4(25278, 32628, 32748), 75 },
                  { AOM_CDF4(5271, 31614, 32692), 115 },
              },
              {
                  { AOM_CDF4(20384, 31949, 32664), 75 },
                  { AOM_CDF4(2212, 29000, 32396), 75 },
              },
              {
                  { AOM_CDF4(12723, 28024, 31879), 75 },
                  { AOM_CDF4(3021, 23271, 31033), 75 },
              },
              {
                  { AOM_CDF4(8230, 22265, 28399), 75 },
                  { AOM_CDF4(415, 16233, 26412), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
              {
                  { AOM_CDF4(8192, 16384, 24576), 0 },
                  { AOM_CDF4(8192, 16384, 24576), 0 },
              },
          },
      },
    };

static const aom_cdf_prob av1_default_coeff_base_eob_multi_cdfs
    [TOKEN_CDF_Q_CTXS][TX_SIZES][SIG_COEF_CONTEXTS_EOB]
    [CDF_SIZE(NUM_BASE_LEVELS + 1)] = {
      {
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(27624, 30670), 75 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(31729, 32675), 25 },
              { AOM_CDF3(31286, 32498), 75 },
              { AOM_CDF3(31494, 32509), 93 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32677, 32756), 99 },
              { AOM_CDF3(32737, 32756), 79 },
              { AOM_CDF3(32697, 32748), 123 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32708, 32746), 124 },
              { AOM_CDF3(32257, 32474), 75 },
              { AOM_CDF3(32010, 32492), 110 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32750, 32756), 0 },
              { AOM_CDF3(32725, 32746), 0 },
              { AOM_CDF3(32389, 32710), 0 },
          },
      },
      {
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(29143, 31212), 3 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(31856, 32561), 97 },
              { AOM_CDF3(32584, 32749), 118 },
              { AOM_CDF3(32393, 32666), 103 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32684, 32756), 123 },
              { AOM_CDF3(32739, 32756), 123 },
              { AOM_CDF3(32705, 32754), 118 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32731, 32756), 119 },
              { AOM_CDF3(32176, 32659), 7 },
              { AOM_CDF3(31644, 32588), 1 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32746, 32756), 123 },
              { AOM_CDF3(32723, 32756), 0 },
              { AOM_CDF3(23115, 31638), 62 },
          },
      },
      {
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(30287, 32090), 93 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32023, 32695), 100 },
              { AOM_CDF3(32636, 32754), 123 },
              { AOM_CDF3(32587, 32718), 118 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32696, 32756), 123 },
              { AOM_CDF3(32747, 32756), 123 },
              { AOM_CDF3(32720, 32756), 123 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32742, 32756), 123 },
              { AOM_CDF3(32550, 32740), 1 },
              { AOM_CDF3(32305, 32733), 79 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32744, 32756), 119 },
              { AOM_CDF3(32738, 32756), 75 },
              { AOM_CDF3(30344, 32714), 51 },
          },
      },
      {
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(31110, 32415), 28 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32192, 32743), 50 },
              { AOM_CDF3(32709, 32756), 124 },
              { AOM_CDF3(32430, 32717), 24 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32721, 32756), 124 },
              { AOM_CDF3(32754, 32758), 99 },
              { AOM_CDF3(32712, 32756), 118 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32747, 32756), 118 },
              { AOM_CDF3(32136, 32673), 9 },
              { AOM_CDF3(32008, 32746), 22 },
          },
          {
              { AOM_CDF3(10923, 21845), 0 },
              { AOM_CDF3(32753, 32757), 124 },
              { AOM_CDF3(32493, 32756), 0 },
              { AOM_CDF3(31755, 32756), 25 },
          },
      },
    };

static const aom_cdf_prob
    av1_default_coeff_base_ph_cdfs[TOKEN_CDF_Q_CTXS][COEFF_BASE_PH_CONTEXTS]
                                  [CDF_SIZE(NUM_BASE_LEVELS + 2)] = {
                                    {
                                        { AOM_CDF4(24042, 31545, 32441), 36 },
                                        { AOM_CDF4(19171, 29453, 31810), 6 },
                                        { AOM_CDF4(14480, 25963, 30210), 1 },
                                        { AOM_CDF4(10198, 20797, 26612), 75 },
                                        { AOM_CDF4(5187, 11571, 16603), 78 },
                                    },
                                    {
                                        { AOM_CDF4(25610, 31596, 32279), 31 },
                                        { AOM_CDF4(21997, 30852, 32225), 1 },
                                        { AOM_CDF4(16674, 27594, 30924), 75 },
                                        { AOM_CDF4(11115, 22160, 27593), 75 },
                                        { AOM_CDF4(6144, 13550, 19120), 78 },
                                    },
                                    {
                                        { AOM_CDF4(25832, 31464, 32273), 31 },
                                        { AOM_CDF4(20809, 30221, 32040), 1 },
                                        { AOM_CDF4(16254, 27425, 30735), 75 },
                                        { AOM_CDF4(11807, 22693, 27863), 75 },
                                        { AOM_CDF4(6760, 14792, 20624), 75 },
                                    },
                                    {
                                        { AOM_CDF4(21968, 30672, 32512), 32 },
                                        { AOM_CDF4(18328, 29437, 31826), 76 },
                                        { AOM_CDF4(15459, 27103, 30730), 75 },
                                        { AOM_CDF4(11714, 22917, 28194), 75 },
                                        { AOM_CDF4(7108, 15722, 21972), 90 },
                                    },
                                  };
#if !CONFIG_COEFF_BR_PH_BYPASS
static const aom_cdf_prob
    av1_default_coeff_br_ph_cdfs[TOKEN_CDF_Q_CTXS][COEFF_BR_PH_CONTEXTS]
                                [CDF_SIZE(BR_CDF_SIZE)] = {
                                  {
                                      { AOM_CDF4(17255, 24548, 28308), 87 },
                                      { AOM_CDF4(17725, 25048, 28405), 76 },
                                      { AOM_CDF4(15579, 23101, 27032), 90 },
                                      { AOM_CDF4(13061, 20578, 24977), 115 },
                                      { AOM_CDF4(10615, 17769, 22522), 103 },
                                      { AOM_CDF4(8878, 15539, 20436), 78 },
                                      { AOM_CDF4(5228, 9672, 13468), 93 },
                                  },
                                  {
                                      { AOM_CDF4(17242, 24819, 27877), 12 },
                                      { AOM_CDF4(17393, 24701, 28031), 76 },
                                      { AOM_CDF4(15559, 23020, 26840), 75 },
                                      { AOM_CDF4(13581, 21120, 25422), 90 },
                                      { AOM_CDF4(10900, 18191, 22938), 75 },
                                      { AOM_CDF4(9335, 16198, 21047), 78 },
                                      { AOM_CDF4(5999, 10992, 15117), 78 },
                                  },
                                  {
                                      { AOM_CDF4(17626, 25043, 28029), 37 },
                                      { AOM_CDF4(17779, 25027, 28635), 76 },
                                      { AOM_CDF4(16070, 23674, 27639), 115 },
                                      { AOM_CDF4(14018, 21778, 26190), 118 },
                                      { AOM_CDF4(11608, 19234, 23940), 118 },
                                      { AOM_CDF4(9705, 16640, 21738), 118 },
                                      { AOM_CDF4(6580, 12262, 16818), 118 },
                                  },
                                  {
                                      { AOM_CDF4(17124, 24523, 28963), 47 },
                                      { AOM_CDF4(19988, 27209, 30188), 91 },
                                      { AOM_CDF4(18151, 25973, 29342), 90 },
                                      { AOM_CDF4(15684, 23809, 27929), 93 },
                                      { AOM_CDF4(11775, 20124, 25435), 124 },
                                      { AOM_CDF4(9990, 17532, 22844), 99 },
                                      { AOM_CDF4(6535, 12315, 17263), 101 },
                                  },
                                };
#endif  // !CONFIG_COEFF_BR_PH_BYPASS

static const aom_cdf_prob default_intra_dip_mode_n6_cdf[CDF_SIZE(6)] = {
  AOM_CDF6(5461, 10923, 16384, 21845, 27307)
};

static const aom_cdf_prob
    default_intra_dip_cdf[TOKEN_CDF_Q_CTXS][DIP_CTXS][CDF_SIZE(2)] = {
      { { AOM_CDF2(6048) }, { AOM_CDF2(4529) }, { AOM_CDF2(5181) } },
      { { AOM_CDF2(8596) }, { AOM_CDF2(8006) }, { AOM_CDF2(7168) } },
      { { AOM_CDF2(9008) }, { AOM_CDF2(11031) }, { AOM_CDF2(16384) } },
      { { AOM_CDF2(26870) }, { AOM_CDF2(21845) }, { AOM_CDF2(16384) } }
    };

#endif  // AOM_AV1_COMMON_TOKEN_CDFS_H_
