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

#ifndef AOM_AV1_ENCODER_TX_PRUNE_MODEL_WEIGHTS_H_
#define AOM_AV1_ENCODER_TX_PRUNE_MODEL_WEIGHTS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av1/encoder/ml.h"

/***************************CONFIG_NN_V2 (New)********************************/
#if CONFIG_NN_V2
// Tx type model for 4x4 block.
static float av1_tx_type_nn_4x4_hor_layer0_weights[32] = {
  -1.64947f, -1.54497f, -1.62832f, -0.17774f, -2.89498f, -0.72498f, 0.72036f,
  0.17996f,  1.20000f,  -0.27654f, 0.77396f,  1.21684f,  -1.75909f, -0.51272f,
  -1.25923f, 0.35005f,  -0.04257f, -0.23389f, -0.41841f, -0.08229f, 0.09503f,
  2.73144f,  -0.16875f, -0.23482f, 0.02194f,  -0.26427f, 0.28049f,  0.21260f,
  1.35792f,  0.27733f,  0.88660f,  -0.68304f,
};

static float av1_tx_type_nn_4x4_hor_layer0_bias[8] = {
  1.38742f, 0.59540f,  -1.37622f, 1.92114f,
  0.00000f, -0.38998f, -0.32726f, -0.15650f,
};

static float av1_tx_type_nn_4x4_hor_layer1_weights[32] = {
  1.65254f,  1.00915f,  -0.89318f, -2.05142f, -0.23235f, 0.96781f,  -0.37145f,
  -0.21056f, 1.13891f,  0.38675f,  0.87739f,  -1.42697f, 0.48015f,  0.61883f,
  -0.03979f, 0.11487f,  0.48042f,  0.45200f,  -0.23242f, 0.75166f,  0.55458f,
  0.39452f,  -0.35285f, 1.59120f,  -1.49221f, -0.48349f, -0.64692f, 1.49297f,
  -0.26782f, -0.65416f, -0.10648f, 0.05568f,
};

static float av1_tx_type_nn_4x4_hor_layer1_bias[4] = {
  4.07177f,
  3.26961f,
  0.58083f,
  1.21199f,
};

static float av1_tx_type_nn_4x4_hor_layer0_out[8] = { 0 };
static float av1_tx_type_nn_4x4_hor_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_4x4_hor = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          4,                                      // num_inputs
          8,                                      // num_outputs
          av1_tx_type_nn_4x4_hor_layer0_weights,  // weights
          av1_tx_type_nn_4x4_hor_layer0_bias,     // bias
          RELU,                                   // activation
          av1_tx_type_nn_4x4_hor_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          8,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_4x4_hor_layer1_weights,
          av1_tx_type_nn_4x4_hor_layer1_bias,
          NONE,
          av1_tx_type_nn_4x4_hor_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                  // num_outputs
  av1_tx_type_nn_4x4_hor_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

static float av1_tx_type_nn_4x4_ver_layer0_weights[32] = {
  -0.02032f, 2.61610f,  0.02098f,  -0.30217f, 0.12637f,  0.11017f,  -3.01996f,
  0.35144f,  1.93776f,  -0.20463f, 1.64102f,  -1.41986f, -3.66717f, -0.51655f,
  0.43910f,  0.37778f,  -1.02634f, 0.85337f,  -0.69753f, 1.00206f,  2.11784f,
  1.89427f,  1.92919f,  0.43201f,  -1.67358f, -1.67035f, -1.54623f, 0.16714f,
  -0.06589f, -0.28142f, -0.33118f, 1.72227f,
};

static float av1_tx_type_nn_4x4_ver_layer0_bias[8] = {
  -0.33685f, 0.22025f,  0.28140f, 0.56138f,
  0.93489f,  -1.77048f, 1.34989f, -0.93747f,
};

static float av1_tx_type_nn_4x4_ver_layer1_weights[32] = {
  -1.39506f, -1.06271f, -1.10886f, -1.69719f, 0.19699f,  -2.39850f, -1.26457f,
  0.75328f,  -1.26005f, -0.82738f, -0.12015f, -1.02702f, 1.40828f,  -2.37739f,
  -0.65639f, -0.71992f, -0.90453f, -1.12510f, -2.41362f, -1.16061f, -1.85577f,
  -0.99165f, -1.91366f, 0.16785f,  0.34776f,  0.58154f,  -0.18217f, -0.29257f,
  -0.86315f, -0.53336f, 0.30320f,  -1.32331f,
};

static float av1_tx_type_nn_4x4_ver_layer1_bias[4] = {
  -1.31519f,
  -3.26321f,
  1.71794f,
  -1.90778f,
};

static float av1_tx_type_nn_4x4_ver_layer0_out[8] = { 0 };
static float av1_tx_type_nn_4x4_ver_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_4x4_ver = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          4,                                      // num_inputs
          8,                                      // num_outputs
          av1_tx_type_nn_4x4_ver_layer0_weights,  // weights
          av1_tx_type_nn_4x4_ver_layer0_bias,     // bias
          RELU,                                   // activation
          av1_tx_type_nn_4x4_ver_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          8,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_4x4_ver_layer1_weights,
          av1_tx_type_nn_4x4_ver_layer1_bias,
          NONE,
          av1_tx_type_nn_4x4_ver_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                  // num_outputs
  av1_tx_type_nn_4x4_ver_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};
/******************************************************************************/

// Tx type model for 4x8 block.
static float av1_tx_type_nn_4x8_hor_layer0_weights[32] = {
  0.00218f,  -0.41880f, -0.61215f, -0.92588f, 0.54291f,  -0.10898f, 0.70691f,
  0.46819f,  -1.61598f, -0.08834f, -0.96839f, 1.18489f,  -0.45171f, -0.65445f,
  -0.32179f, -0.10399f, 1.04379f,  0.91895f,  0.85589f,  0.08267f,  1.35388f,
  -2.03096f, 0.08168f,  -0.06372f, -0.26732f, -0.48262f, -0.08682f, 2.44071f,
  -1.35896f, -1.17121f, 1.68866f,  0.10357f,
};

static float av1_tx_type_nn_4x8_hor_layer0_bias[8] = {
  2.93391f,  0.66831f, -0.21419f, 0.00000f,
  -0.72878f, 0.15127f, -1.46755f, 0.16658f,
};

static float av1_tx_type_nn_4x8_hor_layer1_weights[32] = {
  -1.52077f, -1.06243f, 0.35319f,  -0.49207f, 0.54524f,  0.44271f, 1.37117f,
  -0.38957f, -1.28889f, -0.57133f, 0.04658f,  0.62278f,  0.37984f, 0.33247f,
  1.65547f,  -0.56806f, -1.38645f, -0.76258f, 0.67926f,  0.08783f, -0.01443f,
  0.34950f,  1.45812f,  -0.51332f, -1.41331f, -0.16453f, 0.05755f, 0.31405f,
  -0.50191f, 0.18219f,  1.83664f,  -0.75276f,
};

static float av1_tx_type_nn_4x8_hor_layer1_bias[4] = {
  -1.17455f,
  -2.26089f,
  -1.79863f,
  -2.26333f,
};

static float av1_tx_type_nn_4x8_hor_layer0_out[8] = { 0 };
static float av1_tx_type_nn_4x8_hor_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_4x8_hor = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          4,                                      // num_inputs
          8,                                      // num_outputs
          av1_tx_type_nn_4x8_hor_layer0_weights,  // weights
          av1_tx_type_nn_4x8_hor_layer0_bias,     // bias
          RELU,                                   // activation
          av1_tx_type_nn_4x8_hor_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          8,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_4x8_hor_layer1_weights,
          av1_tx_type_nn_4x8_hor_layer1_bias,
          NONE,
          av1_tx_type_nn_4x8_hor_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                  // num_outputs
  av1_tx_type_nn_4x8_hor_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

static float av1_tx_type_nn_4x8_ver_layer0_weights[128] = {
  -0.00952f, -0.98858f, -0.93181f, 1.39594f,  0.96559f,  0.18162f,  -0.76064f,
  -0.06066f, 0.07907f,  -0.09365f, -0.21313f, -0.02187f, -2.61707f, -2.68702f,
  -0.10982f, 0.18559f,  1.17049f,  1.11387f,  1.12697f,  1.05804f,  1.12764f,
  1.06318f,  1.12052f,  0.17406f,  1.83157f,  0.19362f,  0.46910f,  0.39608f,
  0.33342f,  0.40083f,  0.27645f,  1.06864f,  -4.06645f, -0.38775f, -0.11070f,
  0.03781f,  -0.09141f, 0.06185f,  -0.04852f, 0.20163f,  0.16784f,  0.16641f,
  -0.50941f, -0.61087f, 2.07008f,  -0.82381f, -0.85558f, 0.05528f,  -0.10535f,
  -2.81150f, 0.67038f,  0.43643f,  0.49062f,  -0.04465f, 0.90438f,  0.00977f,
  0.46272f,  1.59751f,  0.95234f,  0.35086f,  0.85624f,  0.73149f,  1.67779f,
  -2.21511f, -1.24746f, -1.09014f, -0.92441f, -1.22591f, -1.06961f, -0.95897f,
  -1.24956f, 0.73797f,  1.23275f,  -0.60064f, -0.07851f, 0.14397f,  0.22110f,
  -0.04422f, 0.14350f,  0.75926f,  0.35032f,  0.48104f,  2.81408f,  0.34662f,
  0.42090f,  0.35521f,  -1.36804f, -0.14974f, -0.47696f, -0.07892f, 0.36910f,
  0.32299f,  0.23916f,  0.06032f,  -0.17844f, -0.17558f, -1.42746f, -0.55828f,
  -1.00418f, -0.64823f, -0.73654f, -0.85197f, -1.50989f, 1.69385f,  -0.04973f,
  -0.09273f, 1.04249f,  0.79235f,  1.13229f,  0.99617f,  0.03851f,  0.56334f,
  0.90795f,  1.08296f,  0.58519f,  1.74765f,  0.63971f,  1.35951f,  0.07803f,
  -0.05127f, 0.26514f,  -0.84629f, -0.66343f, -2.10630f, 0.11017f,  2.18528f,
  -0.21958f, 0.05970f,
};

static float av1_tx_type_nn_4x8_ver_layer0_bias[16] = {
  0.04205f, 0.22260f, -1.03870f, -1.19568f, 0.44283f,  0.01143f,
  0.00235f, 4.26772f, 0.44364f,  -0.33199f, -0.39076f, -0.35129f,
  0.08288f, 0.18195f, -0.79890f, 0.10047f,
};

static float av1_tx_type_nn_4x8_ver_layer1_weights[64] = {
  -0.38193f, -0.12095f, 1.57802f,  0.34932f,  -0.47333f, -0.12304f, -0.01736f,
  -2.52445f, 0.18983f,  -0.64707f, -0.60889f, -0.53750f, 0.91666f,  -0.62823f,
  -0.13377f, -0.43594f, -0.38618f, -0.01328f, 0.97457f,  1.48589f,  -1.03238f,
  -0.33459f, -0.35108f, -2.42417f, 0.60229f,  0.06824f,  -0.75495f, 0.26902f,
  0.65311f,  -0.23887f, -0.44604f, -0.55800f, -0.33842f, 0.04259f,  -0.59589f,
  0.49738f,  -0.62301f, -0.30896f, -0.29602f, -2.57052f, 2.00943f,  -0.66490f,
  -0.76312f, 0.28256f,  1.06311f,  -0.38364f, -0.63508f, -0.57609f, -0.88765f,
  -1.04403f, -0.46531f, 0.34084f,  -1.20498f, -0.68352f, -0.72251f, -2.63242f,
  -0.68736f, -0.37904f, -1.32371f, 0.47288f,  1.51904f,  0.78372f,  -1.01830f,
  -1.01848f,
};

static float av1_tx_type_nn_4x8_ver_layer1_bias[4] = {
  -1.45955f,
  -2.08949f,
  -1.24813f,
  -1.55368f,
};

static float av1_tx_type_nn_4x8_ver_layer0_out[16] = { 0 };
static float av1_tx_type_nn_4x8_ver_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_4x8_ver = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                      // num_inputs
          16,                                     // num_outputs
          av1_tx_type_nn_4x8_ver_layer0_weights,  // weights
          av1_tx_type_nn_4x8_ver_layer0_bias,     // bias
          RELU,                                   // activation
          av1_tx_type_nn_4x8_ver_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_4x8_ver_layer1_weights,
          av1_tx_type_nn_4x8_ver_layer1_bias,
          NONE,
          av1_tx_type_nn_4x8_ver_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                  // num_outputs
  av1_tx_type_nn_4x8_ver_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

/******************************************************************************/

// Tx type model for 8x4 block.
static float av1_tx_type_nn_8x4_hor_layer0_weights[128] = {
  -0.22492f, 0.13341f,  -4.03243f, -0.64015f, 0.02783f,  0.60466f,  -0.13335f,
  0.16828f,  0.12336f,  0.52904f,  1.18455f,  -0.32425f, 0.13052f,  0.93810f,
  -3.71165f, 0.02990f,  -4.63558f, 0.05666f,  0.03524f,  -0.07449f, -0.44006f,
  -0.33215f, -0.33713f, 0.08097f,  0.60873f,  0.29582f,  0.21696f,  -0.78729f,
  -0.16757f, -0.26567f, -0.00720f, -1.11226f, 1.58189f,  1.58463f,  1.48536f,
  1.54374f,  1.60069f,  1.46125f,  1.53932f,  0.05974f,  -1.82192f, 0.47043f,
  0.38090f,  0.20833f,  -0.05637f, 0.05183f,  0.01323f,  -0.25662f, 0.78634f,
  -0.55069f, -0.02975f, -1.29294f, -0.77192f, -2.34299f, -1.28074f, 0.77894f,
  -1.69740f, -1.66032f, -1.44323f, -1.55063f, -1.50845f, -1.23690f, -1.80663f,
  0.75079f,  2.32551f,  0.05878f,  0.80438f,  0.88584f,  0.69153f,  0.89060f,
  0.73660f,  0.87259f,  -0.00745f, -1.30044f, -0.59430f, 2.07270f,  1.03307f,
  -0.84697f, -1.19393f, 0.17549f,  -0.24978f, -3.67234f, 0.20781f,  -0.53946f,
  -0.05068f, 0.88274f,  1.30371f,  0.10288f,  0.07585f,  0.12259f,  -0.30815f,
  0.25437f,  -2.82096f, -2.69482f, 0.02370f,  0.12500f,  -0.21019f, -0.49220f,
  0.03638f,  -0.29795f, 0.28645f,  -0.48432f, -0.38584f, -0.32148f, -0.47197f,
  0.32437f,  0.32528f,  -0.19437f, 0.30383f,  -0.31879f, 0.26359f,  -0.12164f,
  -0.43647f, -0.08288f, -0.33438f, -0.63608f, -0.46647f, -0.46574f, 0.47806f,
  -0.49012f, -1.51234f, -1.13502f, -1.20470f, -1.02913f, -1.09182f, -0.93921f,
  -1.85523f, 0.92532f,
};

static float av1_tx_type_nn_8x4_hor_layer0_bias[16] = {
  0.36631f,  0.02901f,  0.64305f,  1.53074f, -1.40229f, 0.03852f,
  -0.05043f, 0.89632f,  -1.23312f, 0.07036f, 0.17070f,  0.56250f,
  -0.28958f, -0.32869f, -0.01704f, 0.68171f,
};

static float av1_tx_type_nn_8x4_hor_layer1_weights[64] = {
  -0.49441f, -0.31960f, -0.84946f, -0.85800f, -2.37767f, 0.81373f,  -0.73172f,
  -0.69337f, 0.88807f,  -0.49242f, -0.44717f, -0.11436f, 0.09978f,  0.15393f,
  0.17083f,  1.44850f,  -0.20582f, -0.04906f, 0.42990f,  -0.61939f, -1.09692f,
  -1.14885f, -1.36879f, -1.30828f, -0.59558f, -0.30903f, -0.08906f, 0.06953f,
  0.15383f,  -0.04193f, -0.54858f, 1.82676f,  -0.22411f, 0.05264f,  -0.45848f,
  -0.72985f, 0.87553f,  0.04116f,  -1.29774f, -2.63018f, 1.09089f,  -0.36048f,
  -0.16725f, 0.11627f,  0.49918f,  0.07539f,  0.00763f,  0.73706f,  0.87800f,
  0.57049f,  0.60969f,  1.02779f,  1.53339f,  -0.35915f, 0.06410f,  1.44582f,
  0.09698f,  0.71888f,  0.60594f,  0.84103f,  -0.50440f, -0.38825f, 0.15626f,
  -1.10654f,
};

static float av1_tx_type_nn_8x4_hor_layer1_bias[4] = {
  -0.92861f,
  -1.45151f,
  -1.33588f,
  -4.33853f,
};

static float av1_tx_type_nn_8x4_hor_layer0_out[16] = { 0 };
static float av1_tx_type_nn_8x4_hor_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_8x4_hor = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                      // num_inputs
          16,                                     // num_outputs
          av1_tx_type_nn_8x4_hor_layer0_weights,  // weights
          av1_tx_type_nn_8x4_hor_layer0_bias,     // bias
          RELU,                                   // activation
          av1_tx_type_nn_8x4_hor_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_8x4_hor_layer1_weights,
          av1_tx_type_nn_8x4_hor_layer1_bias,
          NONE,
          av1_tx_type_nn_8x4_hor_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                  // num_outputs
  av1_tx_type_nn_8x4_hor_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

static float av1_tx_type_nn_8x4_ver_layer0_weights[32] = {
  -1.10946f, 1.86574f,  -1.59343f, 0.27018f, -1.70676f, -0.73982f, -0.19021f,
  -1.94208f, -2.29759f, -1.44402f, 0.28700f, -1.18340f, -1.50158f, -0.44175f,
  -1.36831f, 1.00374f,  2.59312f,  0.50291f, -0.71042f, -0.12238f, -0.15901f,
  -0.22807f, -0.67376f, -0.30215f, 0.54407f, -0.45538f, 1.18262f,  2.28687f,
  1.66212f,  1.70826f,  1.55182f,  0.12230f,
};

static float av1_tx_type_nn_8x4_ver_layer0_bias[8] = {
  0.10943f,  2.09789f, 2.16578f, 0.15766f,
  -0.42461f, 0.00000f, 1.22090f, -1.28717f,
};

static float av1_tx_type_nn_8x4_ver_layer1_weights[32] = {
  1.20426f,  -1.23237f, 2.41053f, -0.72488f, 1.25249f,  0.18018f,  -0.09586f,
  2.17901f,  0.15364f,  1.21535f, -0.38263f, -0.74309f, 0.50551f,  -0.54208f,
  0.59139f,  1.16095f,  0.55919f, -0.60183f, 1.18949f,  1.60787f,  0.54002f,
  -0.10712f, -0.16153f, 0.16207f, -0.32338f, 2.68712f,  -2.83483f, -0.27086f,
  -1.15005f, -0.39311f, 1.51236f, -1.68973f,
};

static float av1_tx_type_nn_8x4_ver_layer1_bias[4] = {
  1.81013f,
  1.10517f,
  2.90059f,
  0.95391f,
};

static float av1_tx_type_nn_8x4_ver_layer0_out[8] = { 0 };
static float av1_tx_type_nn_8x4_ver_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_8x4_ver = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          4,                                      // num_inputs
          8,                                      // num_outputs
          av1_tx_type_nn_8x4_ver_layer0_weights,  // weights
          av1_tx_type_nn_8x4_ver_layer0_bias,     // bias
          RELU,                                   // activation
          av1_tx_type_nn_8x4_ver_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          8,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_8x4_ver_layer1_weights,
          av1_tx_type_nn_8x4_ver_layer1_bias,
          NONE,
          av1_tx_type_nn_8x4_ver_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                  // num_outputs
  av1_tx_type_nn_8x4_ver_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};
/******************************************************************************/

// Tx type model for 8x8 block.
static float av1_tx_type_nn_8x8_hor_layer0_weights[128] = {
  -0.85529f, 0.37619f,  0.12754f,  0.08622f,  0.45278f,  0.54929f,  1.60651f,
  -0.62654f, -0.54929f, -0.10131f, -0.17569f, 0.13948f,  0.31695f,  -0.05616f,
  0.20483f,  -0.36448f, 2.27203f,  -0.33087f, 0.47679f,  0.86888f,  0.39370f,
  0.46239f,  0.01113f,  1.50327f,  -1.48226f, -1.69621f, -1.49777f, -1.38885f,
  -1.37753f, -1.22681f, -1.70576f, 0.51329f,  -1.65662f, 1.74197f,  -0.13579f,
  -0.13133f, -0.58396f, -0.55510f, -1.10709f, -2.34975f, 0.22445f,  -0.56491f,
  -0.83432f, 0.13492f,  1.32147f,  2.85285f,  0.13819f,  0.03792f,  -1.30792f,
  0.04155f,  -0.70644f, -0.43430f, -0.16212f, -0.86945f, -1.16976f, 1.68339f,
  0.29540f,  0.01137f,  -0.25335f, -0.16856f, 0.12028f,  0.05207f,  0.39357f,
  -0.01545f, -0.21980f, -1.94091f, -1.01315f, -0.68270f, -0.40590f, -0.67111f,
  2.08283f,  0.19291f,  -4.81426f, -0.65044f, -0.24598f, 0.06371f,  -0.10272f,
  -0.14502f, -0.06821f, 0.45202f,  0.21091f,  -0.80864f, 0.39255f,  1.79189f,
  1.80453f,  1.10484f,  1.17608f,  0.96901f,  -0.35871f, -0.94311f, 0.63147f,
  2.95157f,  0.45917f,  -0.42849f, -0.55643f, -0.06097f, 3.49299f,  -0.50972f,
  0.11075f,  -0.08405f, -0.09274f, -0.22694f, -0.42426f, 0.48632f,  -1.61074f,
  1.82998f,  0.37623f,  -1.20330f, -0.01142f, -1.33307f, -0.27492f, -2.23621f,
  1.38846f,  1.42085f,  1.42568f,  1.36152f,  1.46910f,  1.27473f,  1.34752f,
  0.12753f,  -1.08197f, -1.08280f, -0.79489f, -1.12338f, -1.06795f, -0.87857f,
  -0.99892f, 1.09823f,
};

static float av1_tx_type_nn_8x8_hor_layer0_bias[16] = {
  -0.49232f, -0.29685f, -1.44020f, 1.10940f,  1.16452f, -0.34862f,
  -0.38761f, -0.36243f, 0.21776f,  0.28234f,  2.34269f, -0.04104f,
  -0.26319f, 2.65579f,  -1.30137f, -0.01487f,
};

static float av1_tx_type_nn_8x8_hor_layer1_weights[64] = {
  -0.38058f, -0.41295f, -1.26884f, -0.75560f, -1.57450f, 0.56072f,  -1.42322f,
  -0.29106f, 0.07228f,  0.04391f,  1.61388f,  -0.03055f, 0.81637f,  2.06045f,
  0.27119f,  -0.48328f, -0.45528f, -0.60534f, -1.61209f, -0.78157f, -1.65034f,
  0.60958f,  -1.30523f, 0.25143f,  0.11398f,  0.37860f,  1.54829f,  0.02309f,
  0.67288f,  2.11447f,  0.44845f,  -0.70406f, -0.67897f, -0.38759f, -1.30383f,
  -1.22646f, -1.54571f, 0.60552f,  -1.52565f, 0.11469f,  0.17344f,  0.08622f,
  1.57906f,  -0.00909f, 0.81634f,  2.04909f,  1.26466f,  -1.45741f, -0.75229f,
  0.06200f,  -1.05835f, -0.66257f, -1.73766f, 0.99923f,  -1.87082f, 0.14580f,
  0.49525f,  0.46839f,  1.32203f,  0.33923f,  0.97001f,  2.38584f,  1.58811f,
  0.06161f,
};

static float av1_tx_type_nn_8x8_hor_layer1_bias[4] = {
  1.70385f,
  1.82373f,
  1.78496f,
  1.80826f,
};

static float av1_tx_type_nn_8x8_hor_layer0_out[16] = { 0 };
static float av1_tx_type_nn_8x8_hor_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_8x8_hor = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                      // num_inputs
          16,                                     // num_outputs
          av1_tx_type_nn_8x8_hor_layer0_weights,  // weights
          av1_tx_type_nn_8x8_hor_layer0_bias,     // bias
          RELU,                                   // activation
          av1_tx_type_nn_8x8_hor_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_8x8_hor_layer1_weights,
          av1_tx_type_nn_8x8_hor_layer1_bias,
          NONE,
          av1_tx_type_nn_8x8_hor_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                  // num_outputs
  av1_tx_type_nn_8x8_hor_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

static float av1_tx_type_nn_8x8_ver_layer0_weights[128] = {
  -0.67016f, -1.72366f, -1.86576f, -1.50962f, -1.70419f, -1.73964f, -1.84615f,
  2.09681f,  -0.05081f, -0.61030f, 2.02541f,  0.60222f,  0.99936f,  2.02114f,
  -0.53893f, -0.23757f, 0.73566f,  0.25443f,  0.00132f,  -0.74036f, -0.75351f,
  -0.76964f, -1.71007f, -0.15770f, 1.60982f,  2.17638f,  0.90681f,  0.64973f,
  0.85914f,  0.58786f,  -1.46228f, 0.05187f,  1.18804f,  0.30850f,  0.29512f,
  0.40526f,  0.37635f,  0.32311f,  0.37471f,  1.12346f,  3.41856f,  -0.36653f,
  0.42537f,  -0.19240f, 0.00155f,  0.30826f,  -0.02116f, -0.53435f, -0.34829f,
  -0.52466f, -0.11521f, -0.29163f, -2.05689f, -2.87372f, -0.62626f, 0.09585f,
  -0.75257f, 0.10057f,  1.43474f,  0.89450f,  0.75900f,  1.11147f,  1.00558f,
  0.25886f,  2.22095f,  -0.17926f, 0.57161f,  0.39546f,  0.47846f,  0.40452f,
  0.54298f,  0.45814f,  -3.62788f, -3.02374f, 0.03716f,  -0.13937f, -0.09415f,
  -0.12463f, 0.05682f,  0.03672f,  1.20746f,  1.25003f,  1.27071f,  1.31883f,
  1.27473f,  1.34943f,  1.23158f,  0.09039f,  0.19388f,  0.63420f,  2.79612f,
  0.93803f,  -0.11323f, -0.02027f, 0.41286f,  -0.05979f, -3.80705f, -0.52451f,
  -0.77098f, -0.68132f, -0.65559f, -0.60975f, -1.26165f, 0.25582f,  0.05346f,
  0.61403f,  0.32140f,  -2.39831f, -1.42355f, 1.30541f,  1.02361f,  0.12930f,
  -1.61469f, -0.77036f, -0.59144f, 1.27769f,  1.52068f,  0.82137f,  1.83159f,
  -0.66626f, -0.69806f, -1.00564f, -0.85995f, -0.90889f, -0.84412f, -0.85712f,
  -1.29848f, 0.39308f,
};

static float av1_tx_type_nn_8x8_ver_layer0_bias[16] = {
  -0.14868f, -0.48343f, 3.94416f,  -0.78037f, -1.33789f, -0.60611f,
  0.51793f,  0.44030f,  -0.71563f, 0.22561f,  -1.19083f, -0.46149f,
  0.83015f,  0.06024f,  1.17180f,  0.65122f,
};

static float av1_tx_type_nn_8x8_ver_layer1_weights[64] = {
  -1.42711f, -0.21683f, 2.12061f,  0.20489f,  -0.50228f, -0.24770f, 0.23391f,
  1.03470f,  -0.44847f, -0.63225f, -0.21583f, -0.06467f, -0.21892f, -0.07786f,
  1.43322f,  0.00280f,  -1.53057f, -0.18912f, 1.95333f,  0.31151f,  -2.07601f,
  0.06776f,  0.25529f,  0.94800f,  -1.11453f, -0.20594f, -0.13281f, 0.01485f,
  0.17650f,  -0.07955f, 1.43734f,  -0.23193f, -2.06463f, -0.21238f, 2.13707f,
  0.30351f,  0.27594f,  -0.36245f, 0.19539f,  0.91045f,  -0.24068f, -0.37616f,
  0.88792f,  0.02947f,  -0.16903f, -0.04932f, 1.51293f,  -0.95967f, -1.62903f,
  0.05326f,  2.30703f,  0.64445f,  -1.09464f, -0.16623f, 1.00240f,  0.07548f,
  -0.50406f, 0.63854f,  1.02340f,  0.49833f,  0.13671f,  0.26722f,  2.09516f,
  -0.41305f,
};

static float av1_tx_type_nn_8x8_ver_layer1_bias[4] = {
  2.14067f,
  2.76699f,
  2.04233f,
  1.34803f,
};

static float av1_tx_type_nn_8x8_ver_layer0_out[16] = { 0 };
static float av1_tx_type_nn_8x8_ver_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_8x8_ver = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                      // num_inputs
          16,                                     // num_outputs
          av1_tx_type_nn_8x8_ver_layer0_weights,  // weights
          av1_tx_type_nn_8x8_ver_layer0_bias,     // bias
          RELU,                                   // activation
          av1_tx_type_nn_8x8_ver_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_8x8_ver_layer1_weights,
          av1_tx_type_nn_8x8_ver_layer1_bias,
          NONE,
          av1_tx_type_nn_8x8_ver_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                  // num_outputs
  av1_tx_type_nn_8x8_ver_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};
/******************************************************************************/

// Tx type model for 8x16 block.
static float av1_tx_type_nn_8x16_hor_layer0_weights[128] = {
  -1.61872f, -1.58520f, -1.41236f, -1.53255f, -1.59794f, -1.25769f, -1.90043f,
  0.73431f,  1.10135f,  0.47054f,  0.43230f,  -0.43009f, -0.09135f, -0.07289f,
  -0.38785f, 1.23775f,  -0.35312f, 0.73789f,  0.88864f,  0.75957f,  0.62579f,
  0.46974f,  0.21851f,  1.63821f,  -2.27289f, -0.68522f, -0.69814f, -0.84368f,
  -0.91320f, -0.63055f, -1.03296f, 0.55778f,  -0.00071f, 1.27539f,  1.60068f,
  1.40975f,  0.97372f,  0.92843f,  1.90853f,  0.12626f,  1.71953f,  1.41978f,
  -0.12234f, -1.27058f, 0.76207f,  0.02495f,  -0.67038f, -0.05255f, 1.72923f,
  1.47630f,  1.47058f,  1.47614f,  1.49354f,  1.66131f,  1.50801f,  0.17145f,
  -2.30947f, -2.10850f, -1.25636f, -0.24900f, 0.72602f,  1.26572f,  0.97865f,
  -0.65466f, 1.31129f,  0.26916f,  0.12139f,  -0.12761f, -0.39143f, -0.28134f,
  0.06584f,  2.24418f,  0.22516f,  0.05011f,  -0.01671f, -0.29476f, -0.40326f,
  0.21138f,  -0.11573f, -0.31154f, -0.36828f, 0.03694f,  -0.07172f, -0.63419f,
  -3.14351f, -1.23125f, 0.65311f,  -0.11406f, 1.97287f,  -0.10422f, 0.83896f,
  0.85033f,  0.49724f,  0.80482f,  0.51454f,  1.06447f,  0.76693f,  0.72599f,
  -0.78573f, -0.53950f, 0.40894f,  0.00086f,  0.10784f,  -0.70498f, 1.16395f,
  1.14597f,  1.13496f,  1.12177f,  1.02100f,  -1.37574f, -2.97144f, 0.33899f,
  0.42013f,  0.86327f,  2.31983f,  2.04008f,  0.95503f,  0.15081f,  0.11530f,
  -0.02574f, -4.77119f, 0.13257f,  -0.01704f, -0.23087f, -0.00825f, 0.07029f,
  -0.28136f, 0.42556f,
};

static float av1_tx_type_nn_8x16_hor_layer0_bias[16] = {
  0.93617f,  -0.24000f, -1.26821f, 0.78780f,  0.13690f, -0.21948f,
  -1.45162f, 0.44584f,  -1.92582f, -0.23169f, 0.56004f, -1.19937f,
  1.81560f,  -1.02643f, -0.81690f, 0.08302f,
};

static float av1_tx_type_nn_8x16_hor_layer1_weights[64] = {
  0.06696f,  -0.11538f, -1.42029f, 0.32965f,  0.81046f,  0.01146f,  1.20945f,
  -0.16899f, 0.53224f,  -0.40232f, 0.01786f,  -0.73242f, 1.29750f,  1.95185f,
  0.70143f,  1.43287f,  0.76220f,  0.79937f,  -1.79011f, -1.15178f, 0.42526f,
  -0.67519f, 0.77267f,  -0.30697f, 2.46004f,  -0.49828f, 0.02875f,  1.09972f,
  1.47662f,  0.61719f,  0.61417f,  -0.12363f, 2.53048f,  0.00418f,  -1.38964f,
  0.88117f,  0.39239f,  -0.19347f, -2.58600f, -0.33715f, 1.09323f,  -0.32127f,
  0.02456f,  -0.19125f, 1.12728f,  0.66502f,  0.34296f,  1.14897f,  0.29967f,
  1.19209f,  0.22108f,  -0.11975f, 1.49776f,  -1.34624f, -2.58478f, -1.34632f,
  1.53207f,  0.45634f,  -1.48476f, 0.17489f,  0.71790f,  -2.12086f, -1.21778f,
  -1.31243f,
};

static float av1_tx_type_nn_8x16_hor_layer1_bias[4] = {
  0.83359f,
  1.06875f,
  1.77645f,
  1.49570f,
};

static float av1_tx_type_nn_8x16_hor_layer0_out[16] = { 0 };
static float av1_tx_type_nn_8x16_hor_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_8x16_hor = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                       // num_inputs
          16,                                      // num_outputs
          av1_tx_type_nn_8x16_hor_layer0_weights,  // weights
          av1_tx_type_nn_8x16_hor_layer0_bias,     // bias
          RELU,                                    // activation
          av1_tx_type_nn_8x16_hor_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_8x16_hor_layer1_weights,
          av1_tx_type_nn_8x16_hor_layer1_bias,
          NONE,
          av1_tx_type_nn_8x16_hor_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                   // num_outputs
  av1_tx_type_nn_8x16_hor_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

static float av1_tx_type_nn_8x16_ver_layer0_weights[128] = {
  0.32858f,  -1.28887f, 0.25632f,  -0.05262f, 2.69203f,  -0.07004f, 1.37337f,
  -0.05725f, -0.05659f, 0.05592f,  0.01039f,  -0.29343f, 1.58628f,  -0.30003f,
  -3.43118f, 0.00272f,  1.70928f,  -0.76348f, 0.05889f,  -0.03263f, -0.07724f,
  0.03523f,  -0.19890f, 1.18005f,  -0.03605f, -0.20530f, -4.00733f, 0.10210f,
  -0.05368f, -0.17650f, -0.15317f, 0.06499f,  0.56705f,  1.04341f,  0.62890f,
  0.73451f,  -0.22199f, 0.86659f,  0.78443f,  -0.61664f, -0.50606f, 0.30247f,
  0.14455f,  0.39276f,  0.49203f,  0.65019f,  0.12269f,  1.64080f,  1.68289f,
  1.42694f,  1.60825f,  1.58501f,  1.47252f,  1.62589f,  1.48218f,  0.17726f,
  -0.04884f, 0.35376f,  -0.04796f, 0.32589f,  0.35087f,  0.35258f,  -0.46103f,
  -0.31176f, -0.05203f, 0.07247f,  -0.26756f, 0.22019f,  0.03412f,  0.33773f,
  0.29811f,  -0.11140f, 0.12831f,  -0.44673f, -0.09858f, 0.07889f,  0.15137f,
  0.00347f,  -0.23394f, 0.08886f,  -0.31201f, -0.79912f, -0.51092f, 0.14123f,
  -1.09599f, -4.26020f, -0.68675f, -0.02842f, -1.54538f, -1.28977f, -1.30558f,
  -1.21074f, -1.37142f, -1.14743f, -1.85397f, 0.82985f,  -0.30681f, 0.04494f,
  -0.24023f, -4.18053f, -0.16096f, -0.55492f, -0.27882f, 0.05829f,  -0.41224f,
  -2.52088f, -0.56162f, -1.04547f, -1.70685f, -0.28842f, -1.43673f, -0.01468f,
  -3.20585f, -0.69120f, -0.43931f, -0.46270f, -0.65885f, -0.55884f, -0.75138f,
  0.36381f,  -5.70858f, -0.14548f, -0.15745f, -0.11812f, -0.07605f, -0.07693f,
  -0.12236f, 0.16075f,
};

static float av1_tx_type_nn_8x16_ver_layer0_bias[16] = {
  -0.35385f, 0.30491f,  -0.90011f, 0.42941f,  1.20928f, -0.88331f,
  -1.48818f, -0.34785f, -0.32668f, -0.22695f, 0.89188f, 0.65521f,
  0.57598f,  0.99819f,  0.75175f,  0.17044f,
};

static float av1_tx_type_nn_8x16_ver_layer1_weights[64] = {
  -0.62913f, -0.34304f, 0.42963f,  -0.17440f, -1.44092f, 0.69142f,  -1.36067f,
  0.52211f,  0.44658f,  -0.26501f, -0.41657f, 0.34428f,  -0.34390f, -0.58567f,
  -0.84097f, -1.96311f, -0.37215f, -0.22250f, -1.23811f, -0.07247f, -0.81731f,
  0.58755f,  -1.30559f, 0.39551f,  0.41743f,  -0.09940f, -0.33230f, 0.14458f,
  -0.25139f, -0.54517f, 0.13469f,  -0.38157f, -0.39109f, -0.18205f, 0.06834f,
  -0.08395f, -0.92187f, 0.56724f,  1.44381f,  0.53226f,  -0.22356f, 0.12285f,
  -0.29418f, -1.86749f, -0.22372f, -0.60204f, -0.87746f, -1.16936f, 0.56884f,
  0.62641f,  -0.11823f, 1.00395f,  1.64794f,  -0.64535f, 2.29322f,  -0.23397f,
  0.17251f,  -0.35927f, 0.65631f,  -0.26812f, 0.80128f,  0.85748f,  0.47404f,
  2.20547f,
};

static float av1_tx_type_nn_8x16_ver_layer1_bias[4] = {
  -0.44080f,
  -1.67455f,
  -1.46332f,
  -6.13206f,
};

static float av1_tx_type_nn_8x16_ver_layer0_out[16] = { 0 };
static float av1_tx_type_nn_8x16_ver_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_8x16_ver = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                       // num_inputs
          16,                                      // num_outputs
          av1_tx_type_nn_8x16_ver_layer0_weights,  // weights
          av1_tx_type_nn_8x16_ver_layer0_bias,     // bias
          RELU,                                    // activation
          av1_tx_type_nn_8x16_ver_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_8x16_ver_layer1_weights,
          av1_tx_type_nn_8x16_ver_layer1_bias,
          NONE,
          av1_tx_type_nn_8x16_ver_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                   // num_outputs
  av1_tx_type_nn_8x16_ver_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};
/******************************************************************************/

// Tx type model for 16x8 block.
static float av1_tx_type_nn_16x8_hor_layer0_weights[128] = {
  0.02600f,  0.09786f,  -1.05107f, -0.35594f, -0.15658f, 2.99828f,  -0.07106f,
  -0.10101f, -0.14412f, -0.83790f, -0.19434f, 2.28368f,  1.91727f,  -0.00956f,
  -0.90640f, 0.09174f,  1.58895f,  1.38945f,  1.49431f,  1.51381f,  1.44803f,
  1.53544f,  1.44694f,  0.17753f,  1.69735f,  -0.78652f, 0.31092f,  -0.23736f,
  0.02231f,  -0.09884f, -0.00493f, 1.21189f,  -1.94382f, -0.34629f, -0.58309f,
  0.72291f,  -0.30056f, 0.90660f,  -0.57495f, 3.07809f,  0.73644f,  1.43050f,
  1.34356f,  -0.66554f, 0.50102f,  -0.64305f, 0.42044f,  -1.66165f, -0.05733f,
  -2.51402f, -1.01067f, -0.33390f, -0.32986f, -0.92431f, 1.86281f,  -0.07290f,
  -0.26290f, -0.68941f, 1.81156f,  0.66125f,  -2.09974f, 0.17032f,  -0.67461f,
  -0.00876f, -1.50154f, 1.17153f,  1.00377f,  0.33022f,  0.74689f,  0.42878f,
  0.61725f,  -0.83967f, 0.09467f,  -0.39892f, 0.33863f,  0.10656f,  -0.09249f,
  -0.39757f, 0.48481f,  -0.35162f, 1.47014f,  1.67827f,  -1.84051f, 0.16291f,
  -0.50135f, -2.29911f, -0.42217f, -0.13358f, 1.45899f,  -0.14743f, -0.02763f,
  -0.28003f, -0.01364f, 0.21014f,  -0.29026f, -0.20198f, 1.38782f,  0.56731f,
  0.27489f,  0.43227f,  0.41326f,  0.42721f,  0.87720f,  -1.90067f, -5.04951f,
  -0.17638f, -0.58119f, -0.08954f, -0.13692f, -0.12325f, -0.38548f, 0.66462f,
  -1.42377f, -1.21917f, -1.38193f, -1.36539f, -1.39378f, -1.19629f, -1.59812f,
  0.28689f,  0.32394f,  0.52128f,  0.01013f,  -0.28948f, -0.26293f, -0.44331f,
  -0.36570f, -0.50757f,
};

static float av1_tx_type_nn_16x8_hor_layer0_bias[16] = {
  -0.08696f, -0.22110f, -1.43604f, -1.00451f, -1.51029f, 0.63736f,
  0.45260f,  0.16229f,  4.01393f,  -0.21748f, 0.36411f,  -0.08764f,
  -0.12329f, 0.08986f,  1.08117f,  -0.00220f,
};

static float av1_tx_type_nn_16x8_hor_layer1_weights[64] = {
  0.55824f,  -0.14648f, 0.81947f,  -0.45867f, -1.86078f, -0.17291f, 0.34849f,
  0.15153f,  1.75625f,  -0.25760f, 0.72015f,  -0.30059f, -0.57975f, 0.07609f,
  -0.02036f, 0.07912f,  0.57080f,  -0.13792f, 0.74184f,  -0.87669f, -1.87572f,
  -0.27270f, 0.39751f,  0.19652f,  2.03514f,  -0.32944f, 0.76251f,  0.04399f,
  -0.63175f, 0.37420f,  0.08309f,  0.04466f,  0.60255f,  -0.12820f, 1.66065f,
  -0.59496f, -1.94794f, -0.14847f, 0.39424f,  0.16273f,  1.80587f,  0.41197f,
  0.74691f,  -0.21217f, -0.63173f, 0.09510f,  -0.35538f, -0.04407f, 0.92847f,
  0.20141f,  1.68680f,  -0.56528f, -2.26960f, 0.12978f,  0.73748f,  0.42438f,
  2.00673f,  -0.40189f, 0.95423f,  0.23234f,  -0.80953f, 0.65814f,  0.49444f,
  -0.23347f,
};

static float av1_tx_type_nn_16x8_hor_layer1_bias[4] = {
  3.57175f,
  2.42612f,
  3.31259f,
  2.08287f,
};

static float av1_tx_type_nn_16x8_hor_layer0_out[16] = { 0 };
static float av1_tx_type_nn_16x8_hor_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_16x8_hor = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                       // num_inputs
          16,                                      // num_outputs
          av1_tx_type_nn_16x8_hor_layer0_weights,  // weights
          av1_tx_type_nn_16x8_hor_layer0_bias,     // bias
          RELU,                                    // activation
          av1_tx_type_nn_16x8_hor_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_16x8_hor_layer1_weights,
          av1_tx_type_nn_16x8_hor_layer1_bias,
          NONE,
          av1_tx_type_nn_16x8_hor_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                   // num_outputs
  av1_tx_type_nn_16x8_hor_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

static float av1_tx_type_nn_16x8_ver_layer0_weights[128] = {
  0.46633f,  1.55328f,  -0.11230f, -0.29571f, 0.18814f,  -1.52430f, -2.34660f,
  0.08644f,  -1.97718f, -1.29140f, -1.12262f, -1.12985f, -1.25911f, -0.96506f,
  -1.57129f, 0.96021f,  1.34192f,  1.28623f,  1.21655f,  1.28758f,  1.25482f,
  1.30195f,  1.19190f,  0.09310f,  0.52072f,  0.91487f,  1.24100f,  1.61236f,
  1.72166f,  2.20750f,  1.62379f,  -1.43936f, 0.50665f,  0.40213f,  0.66502f,
  -1.66699f, -3.07618f, 0.05877f,  0.60987f,  -0.09995f, -0.10916f, 0.48049f,
  0.23812f,  0.39847f,  -0.21682f, -0.63455f, 0.33453f,  -0.67939f, -4.14355f,
  -0.62756f, -0.22502f, -0.17215f, 0.01062f,  0.27049f,  -0.10748f, 0.30945f,
  2.72445f,  -0.89181f, -0.06800f, 0.20595f,  -0.73385f, 0.04071f,  -1.30294f,
  1.83507f,  0.92570f,  0.69609f,  0.76285f,  0.69892f,  0.76409f,  0.63104f,
  0.73397f,  1.09575f,  -0.20129f, -0.24022f, -0.24599f, -0.59107f, -0.88755f,
  -0.68987f, -0.75495f, -1.31002f, -1.30237f, -0.94093f, -2.15678f, -1.49303f,
  -1.17498f, -1.39952f, -0.91270f, -0.05587f, 1.02381f,  -0.75580f, -0.65263f,
  -0.78996f, -0.71075f, -0.71018f, -0.70350f, -1.26196f, 2.34208f,  -0.53611f,
  0.19752f,  -0.16842f, -0.24828f, 0.21857f,  0.08222f,  -2.55894f, -1.75702f,
  0.11394f,  1.03083f,  0.79972f,  -1.54112f, -1.82341f, -0.57597f, -0.02077f,
  -0.39616f, -0.00995f, -0.12809f, 0.01188f,  -0.25117f, 0.09202f,  0.09336f,
  -0.05614f, -0.30039f, 0.25834f,  1.19944f,  1.22533f,  0.92330f,  0.75967f,
  -0.81945f, -0.41647f,
};

static float av1_tx_type_nn_16x8_ver_layer0_bias[16] = {
  0.17841f,  0.67315f,  -1.24450f, 3.13859f,  0.16203f, -0.14992f,
  0.29553f,  -1.15567f, -0.71421f, 1.15977f,  1.14585f, 3.02460f,
  -0.04510f, 0.48000f,  -0.09354f, -0.42422f,
};

static float av1_tx_type_nn_16x8_ver_layer1_weights[64] = {
  0.29912f,  -0.10009f, -1.11478f, 1.76812f,  -0.27719f, 0.52148f,  0.17622f,
  -1.17116f, 0.73397f,  -0.69279f, -0.11080f, 1.53751f,  -1.42003f, 0.14731f,
  0.13592f,  -0.04883f, 0.39186f,  -0.13655f, -0.43994f, 1.82759f,  -0.25601f,
  -0.15018f, 0.51920f,  -1.56070f, 0.31683f,  -0.79367f, -0.02904f, 1.28637f,
  -1.15203f, 0.26627f,  0.42828f,  -0.24258f, 0.38647f,  -0.83352f, 0.32553f,
  2.09522f,  -0.26822f, -0.42191f, 0.32825f,  -1.30748f, 1.50551f,  -0.52669f,
  0.20045f,  1.69318f,  -1.47839f, 0.30802f,  -0.07290f, -0.28106f, 0.68192f,
  -0.15522f, 1.12579f,  2.21921f,  0.09720f,  -0.50265f, 0.83165f,  -1.31721f,
  0.72422f,  -1.24952f, 0.61653f,  2.04117f,  -1.42406f, 0.52568f,  -0.46180f,
  -0.00873f,
};

static float av1_tx_type_nn_16x8_ver_layer1_bias[4] = {
  3.34981f,
  3.74710f,
  1.38339f,
  0.45176f,
};

static float av1_tx_type_nn_16x8_ver_layer0_out[16] = { 0 };
static float av1_tx_type_nn_16x8_ver_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_16x8_ver = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                       // num_inputs
          16,                                      // num_outputs
          av1_tx_type_nn_16x8_ver_layer0_weights,  // weights
          av1_tx_type_nn_16x8_ver_layer0_bias,     // bias
          RELU,                                    // activation
          av1_tx_type_nn_16x8_ver_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_16x8_ver_layer1_weights,
          av1_tx_type_nn_16x8_ver_layer1_bias,
          NONE,
          av1_tx_type_nn_16x8_ver_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                   // num_outputs
  av1_tx_type_nn_16x8_ver_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};
/******************************************************************************/

// Tx type model for 16x16 block.
static float av1_tx_type_nn_16x16_layer0_weights[128] = {
  1.26592f,  1.36313f,  1.30956f,  1.29926f,  1.48816f,  1.68851f,  1.32000f,
  0.13321f,  -0.22477f, -0.88906f, -0.19622f, 1.69605f,  1.22180f,  -1.57771f,
  -1.15765f, 0.05710f,  -1.13355f, -0.85486f, -0.99971f, -0.91571f, -1.06031f,
  -0.77952f, -1.15723f, 1.17809f,  1.35602f,  -0.05243f, -0.37596f, 0.26108f,
  0.17611f,  -0.10323f, 0.77279f,  -0.48911f, -0.79308f, 0.55112f,  0.43918f,
  0.27872f,  0.28714f,  0.45830f,  1.05689f,  0.03705f,  -2.49975f, -0.01940f,
  0.05709f,  0.07942f,  -0.13290f, -0.10359f, 0.00143f,  0.37303f,  0.96470f,
  0.53293f,  1.14459f,  0.89185f,  0.43378f,  0.47764f,  0.90924f,  0.15279f,
  -0.15361f, 0.02949f,  0.42240f,  0.68143f,  0.89588f,  0.73754f,  0.10974f,
  1.57755f,  -0.39870f, -0.32914f, 0.35638f,  0.34991f,  -0.00003f, -0.23373f,
  0.29630f,  -0.76699f, -0.01356f, 0.04234f,  0.84253f,  1.92078f,  0.93160f,
  0.71993f,  0.71604f,  0.76455f,  -1.59782f, 0.32332f,  1.11628f,  0.33062f,
  -0.03728f, -0.05710f, 0.80447f,  -0.14719f, 1.34658f,  -0.05718f, 0.64015f,
  0.21926f,  0.41653f,  0.12720f,  0.54092f,  1.39411f,  1.81819f,  -0.24513f,
  0.00955f,  0.38011f,  -0.57787f, -0.41759f, 0.68834f,  -0.31783f, -0.40607f,
  -0.10107f, -0.79374f, 0.75599f,  -0.16282f, -0.14490f, -0.20783f, -0.55019f,
  -0.13793f, -0.22293f, 0.18305f,  0.12445f,  0.56830f,  0.24567f,  0.09278f,
  0.70803f,  0.35803f,  -1.52676f, -0.89624f, 0.77665f,  0.19877f,  0.77175f,
  0.50355f,  0.08592f,
};

static float av1_tx_type_nn_16x16_layer0_bias[16] = {
  -1.31834f, 0.14346f,  -0.10062f, 0.84489f,  0.95617f,  -0.06720f,
  -0.68502f, -0.91442f, -0.31932f, 0.25276f,  -0.15138f, -1.57661f,
  -0.14062f, -0.42120f, 0.94573f,  -0.09287f,
};

static float av1_tx_type_nn_16x16_layer1_weights[64] = {
  -1.80333f, -1.06353f, 0.55139f,  0.74644f,  0.13747f, -0.93018f, -0.10286f,
  0.67133f,  0.24460f,  1.44583f,  0.02173f,  0.26037f, -0.73687f, 0.19566f,
  0.61846f,  -0.58601f, -1.03196f, -0.74415f, 0.30041f, -0.41967f, 1.08740f,
  0.96224f,  -0.59139f, 0.03813f,  0.05403f,  1.33427f, -0.54375f, -1.92181f,
  0.54704f,  0.13608f,  0.22151f,  -0.38076f, 1.18390f, -0.77508f, -1.84283f,
  1.00894f,  0.62318f,  -0.15296f, 1.27600f,  0.22822f, 0.12751f,  0.93910f,
  -0.28502f, 0.53912f,  -0.96889f, 0.10182f,  0.81508f, -0.43028f, 2.67386f,
  0.52204f,  0.49820f,  -0.41711f, 1.05038f,  1.12192f, 0.74349f,  -0.75417f,
  -0.03718f, -0.35769f, 0.89651f,  0.63236f,  0.54215f, -0.07894f, 0.48274f,
  1.08829f,
};

static float av1_tx_type_nn_16x16_layer1_bias[4] = {
  0.81986f,
  1.26865f,
  0.11118f,
  2.48404f,
};

static float av1_tx_type_nn_16x16_layer0_out[16] = { 0 };
static float av1_tx_type_nn_16x16_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_16x16 = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                    // num_inputs
          16,                                   // num_outputs
          av1_tx_type_nn_16x16_layer0_weights,  // weights
          av1_tx_type_nn_16x16_layer0_bias,     // bias
          RELU,                                 // activation
          av1_tx_type_nn_16x16_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_16x16_layer1_weights,
          av1_tx_type_nn_16x16_layer1_bias,
          NONE,
          av1_tx_type_nn_16x16_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                // num_outputs
  av1_tx_type_nn_16x16_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};
/******************************************************************************/

// Tx type model for 4x16 block.
static float av1_tx_type_nn_4x16_hor_layer0_weights[32] = {
  0.36539f,  0.25667f,  0.01491f,  -0.21959f, 2.55105f,  0.17615f, 1.79884f,
  1.65936f,  -0.44363f, 0.00706f,  -0.68004f, -0.64360f, 1.75760f, 1.91906f,
  1.47682f,  0.09650f,  -3.59244f, -0.35004f, 0.93295f,  0.25806f, -0.08154f,
  0.79332f,  0.79535f,  1.09467f,  1.57855f,  -0.51359f, 0.90553f, -1.67744f,
  -1.74563f, -0.88830f, -1.77603f, 2.15935f,
};

static float av1_tx_type_nn_4x16_hor_layer0_bias[8] = {
  -0.36435f, -2.22731f, -0.00837f, -1.34546f,
  0.62806f,  -0.20675f, 4.91940f,  -0.56079f,
};

static float av1_tx_type_nn_4x16_hor_layer1_weights[32] = {
  -0.57191f, -1.46418f, 0.67331f,  -1.15027f, 0.46288f,  0.81251f,  2.51768f,
  -0.27147f, 0.00761f,  -2.15214f, -0.69650f, -0.50808f, 0.92832f,  0.45668f,
  2.34201f,  -0.52941f, 0.51008f,  -1.55496f, -0.01371f, -0.12356f, 0.66624f,
  0.88043f,  2.64862f,  -1.28024f, -0.17578f, -1.80034f, -0.32217f, 0.89519f,
  1.28413f,  -0.30326f, 2.45329f,  -0.83335f,
};

static float av1_tx_type_nn_4x16_hor_layer1_bias[4] = {
  2.33198f,
  3.36245f,
  1.62603f,
  2.91056f,
};

static float av1_tx_type_nn_4x16_hor_layer0_out[8] = { 0 };
static float av1_tx_type_nn_4x16_hor_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_4x16_hor = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          4,                                       // num_inputs
          8,                                       // num_outputs
          av1_tx_type_nn_4x16_hor_layer0_weights,  // weights
          av1_tx_type_nn_4x16_hor_layer0_bias,     // bias
          RELU,                                    // activation
          av1_tx_type_nn_4x16_hor_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          8,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_4x16_hor_layer1_weights,
          av1_tx_type_nn_4x16_hor_layer1_bias,
          NONE,
          av1_tx_type_nn_4x16_hor_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                   // num_outputs
  av1_tx_type_nn_4x16_hor_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

static float av1_tx_type_nn_4x16_ver_layer0_weights[128] = {
  1.61392f,  1.41239f,  1.47646f,  1.47325f,  1.46110f,  1.49208f,  1.49414f,
  0.12835f,  -0.76986f, 0.07087f,  -0.24572f, -0.93168f, 3.07935f,  -0.18183f,
  -0.09831f, -0.07703f, -0.03222f, -0.25473f, -0.06090f, 2.93713f,  -0.38711f,
  -0.12884f, -0.18329f, -0.06262f, -0.00327f, -0.02930f, -0.01641f, -0.00622f,
  -0.03305f, -4.07069f, -2.76643f, 0.04413f,  -1.03176f, -0.19217f, -0.44980f,
  -2.48615f, -2.58112f, -0.87695f, 0.16187f,  -0.04891f, -0.06854f, 1.08104f,
  0.75245f,  1.49302f,  0.63363f,  1.45715f,  0.92574f,  1.72029f,  0.33326f,
  3.86646f,  0.04422f,  0.41019f,  0.36212f,  0.56600f,  -1.01552f, 0.05128f,
  0.40454f,  -1.05100f, -0.47461f, -1.33168f, -0.46145f, -1.36870f, -0.88838f,
  -1.05358f, -0.18537f, -0.34357f, -0.03698f, 0.68905f,  0.41010f,  0.31223f,
  -0.43382f, -0.74715f, 2.03366f,  -0.30419f, 0.45747f,  0.09526f,  0.31678f,
  0.22915f,  0.21832f,  1.26385f,  -0.06814f, -0.71417f, -1.18947f, 0.03762f,
  0.10936f,  2.97396f,  -0.42638f, -0.03123f, -5.49756f, -0.17029f, -0.11323f,
  0.05173f,  -0.44274f, -0.15738f, 0.11311f,  0.43872f,  0.16837f,  -0.52849f,
  2.90050f,  -0.54735f, -0.29591f, 1.24030f,  0.21696f,  -0.04443f, -1.60877f,
  -1.36365f, -1.27432f, -1.52060f, -1.34397f, -1.13371f, -1.87554f, 0.80123f,
  0.42820f,  -0.14157f, -2.73963f, -0.68040f, -0.35236f, 0.14490f,  2.23477f,
  0.01370f,  -0.20426f, -1.51411f, -0.72293f, 0.64516f,  0.97638f,  0.32616f,
  -0.27975f, -0.01149f,
};

static float av1_tx_type_nn_4x16_ver_layer0_bias[16] = {
  -1.37863f, -0.05763f, -0.07041f, 0.15306f,  0.96026f,  -1.42105f,
  -0.55822f, 1.04845f,  -0.17662f, -1.25345f, -0.11927f, 0.49845f,
  -0.32530f, 0.73483f,  0.08322f,  -0.23890f,
};

static float av1_tx_type_nn_4x16_ver_layer1_weights[64] = {
  0.27194f,  0.50607f,  0.49229f,  -0.48192f, 0.15667f,  -1.38891f, 0.38102f,
  -0.58825f, -0.07337f, -0.52909f, 0.36975f,  0.28710f,  0.34992f,  -0.73630f,
  0.30386f,  -0.58822f, 0.36127f,  0.57950f,  0.55878f,  -0.42796f, 0.19967f,
  -1.45517f, 0.42529f,  -0.54630f, -0.38169f, -0.84899f, 0.41622f,  0.46935f,
  0.39077f,  -0.75448f, 0.31698f,  -0.76187f, 0.97765f,  0.57052f,  0.55825f,
  -0.54273f, 0.20466f,  -1.46347f, 0.41813f,  -0.55019f, -0.19948f, -0.57982f,
  0.41206f,  0.32373f,  0.38537f,  -1.11657f, 0.32887f,  -0.76911f, 1.12259f,
  0.72163f,  0.82603f,  0.37786f,  0.34976f,  -1.86642f, 0.59961f,  -0.16329f,
  -0.36631f, -0.56814f, 0.60410f,  0.53158f,  0.56389f,  -0.70508f, 0.51009f,
  -0.56513f,
};

static float av1_tx_type_nn_4x16_ver_layer1_bias[4] = {
  4.60896f,
  4.53551f,
  4.53124f,
  4.27435f,
};

static float av1_tx_type_nn_4x16_ver_layer0_out[16] = { 0 };
static float av1_tx_type_nn_4x16_ver_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_4x16_ver = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                       // num_inputs
          16,                                      // num_outputs
          av1_tx_type_nn_4x16_ver_layer0_weights,  // weights
          av1_tx_type_nn_4x16_ver_layer0_bias,     // bias
          RELU,                                    // activation
          av1_tx_type_nn_4x16_ver_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_4x16_ver_layer1_weights,
          av1_tx_type_nn_4x16_ver_layer1_bias,
          NONE,
          av1_tx_type_nn_4x16_ver_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                   // num_outputs
  av1_tx_type_nn_4x16_ver_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};
/******************************************************************************/

// Tx type model for 16x4 block.
static float av1_tx_type_nn_16x4_hor_layer0_weights[128] = {
  1.45347f,  -0.15743f, 0.44236f,  0.25808f,  0.33944f,  0.38678f,  0.24428f,
  1.67287f,  0.09539f,  -0.42940f, -0.31507f, -0.00154f, -2.98755f, -2.27744f,
  -0.49183f, 0.09333f,  -0.99026f, -0.22157f, 0.53701f,  0.60447f,  0.15686f,
  -0.04646f, 0.26341f,  2.12361f,  0.27090f,  -1.14716f, -0.64146f, -0.91604f,
  -0.75335f, -0.60056f, -1.25084f, 1.68473f,  -3.24075f, -4.03867f, -2.07877f,
  -0.02347f, 0.00333f,  -0.01259f, -0.00465f, 0.02526f,  0.36286f,  -0.10324f,
  2.12780f,  -0.74584f, -1.05052f, 1.78467f,  -0.55065f, -0.03326f, 2.46781f,
  1.18349f,  0.96015f,  1.01696f,  1.10584f,  1.07263f,  1.11531f,  -1.06413f,
  0.32389f,  -1.87360f, -0.14435f, 1.77926f,  1.09966f,  -0.12680f, -0.61386f,
  -0.09724f, -0.33095f, 1.12122f,  1.00791f,  1.52416f,  1.35004f,  1.32657f,
  0.60950f,  -1.13538f, -0.38654f, 0.06473f,  2.10669f,  0.27734f,  -0.38359f,
  -1.91455f, -1.22676f, 0.05786f,  0.97432f,  2.19967f,  0.50457f,  0.78976f,
  0.95183f,  -0.32414f, 0.49437f,  -0.04506f, 0.18993f,  -0.07971f, 0.23889f,
  -0.09872f, -0.66036f, 0.05377f,  2.69638f,  -0.08259f, -0.69210f, -1.08296f,
  -1.96504f, -2.31947f, -0.80161f, -0.80456f, -1.35556f, -0.05323f, -4.42658f,
  -0.30732f, -0.12043f, 0.11126f,  0.10771f,  -0.14956f, -0.02218f, 0.41016f,
  1.16599f,  1.14629f,  1.12881f,  1.18676f,  1.24677f,  1.28695f,  1.11270f,
  0.08233f,  1.75440f,  0.49228f,  -0.34858f, -0.17032f, 0.29288f,  0.47175f,
  0.19055f,  -1.56413f,
};

static float av1_tx_type_nn_16x4_hor_layer0_bias[16] = {
  -1.71227f, 0.47291f, -0.97536f, -0.66216f, 0.11729f,  -0.21451f,
  2.75281f,  0.04318f, 2.03965f,  0.14618f,  -0.70483f, -0.24517f,
  1.14048f,  0.33308f, -1.10886f, 0.41184f,
};

static float av1_tx_type_nn_16x4_hor_layer1_weights[64] = {
  -1.17079f, 0.19096f,  -1.05753f, -0.30803f, -1.21680f, -0.67255f, 1.60115f,
  0.05972f,  1.44759f,  -0.04068f, -0.26331f, 0.31400f,  0.96923f,  0.33443f,
  -0.77215f, -0.91316f, -1.78928f, 0.21483f,  -1.24008f, -0.46190f, -0.12127f,
  -0.62144f, 1.37593f,  0.08373f,  1.56215f,  0.00279f,  -0.14556f, 0.38710f,
  0.96228f,  0.66433f,  -0.51798f, -0.80738f, -0.18539f, 0.19377f,  -1.03090f,
  -1.51044f, -0.59485f, -0.62589f, 1.90742f,  0.09078f,  1.49113f,  0.00205f,
  -0.15918f, 0.40827f,  1.08553f,  0.43431f,  0.33519f,  -1.12669f, -1.10274f,
  0.80004f,  -1.83599f, -0.53134f, 2.00515f,  -0.32670f, 1.37124f,  0.51136f,
  1.62563f,  0.24787f,  0.31757f,  0.81751f,  1.57262f,  0.83214f,  1.04661f,
  -0.43819f,
};

static float av1_tx_type_nn_16x4_hor_layer1_bias[4] = {
  2.32575f,
  2.75703f,
  1.12304f,
  2.15567f,
};

static float av1_tx_type_nn_16x4_hor_layer0_out[16] = { 0 };
static float av1_tx_type_nn_16x4_hor_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_16x4_hor = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          8,                                       // num_inputs
          16,                                      // num_outputs
          av1_tx_type_nn_16x4_hor_layer0_weights,  // weights
          av1_tx_type_nn_16x4_hor_layer0_bias,     // bias
          RELU,                                    // activation
          av1_tx_type_nn_16x4_hor_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          16,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_16x4_hor_layer1_weights,
          av1_tx_type_nn_16x4_hor_layer1_bias,
          NONE,
          av1_tx_type_nn_16x4_hor_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                   // num_outputs
  av1_tx_type_nn_16x4_hor_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};

static float av1_tx_type_nn_16x4_ver_layer0_weights[32] = {
  0.26047f,  0.99930f,  1.16484f,  -0.28196f, -2.67483f, -0.21456f, -0.16854f,
  0.46375f,  1.47951f,  1.13735f,  1.12356f,  0.27385f,  0.50978f,  2.09967f,
  -1.47386f, 0.01950f,  -0.06362f, 0.26014f,  1.04544f,  -0.03099f, 0.07478f,
  -0.39701f, 0.05545f,  2.73633f,  -0.56305f, -0.02208f, -0.44517f, -0.00897f,
  -0.17967f, -0.96622f, 0.42635f,  -1.04784f,
};

static float av1_tx_type_nn_16x4_ver_layer0_bias[8] = {
  -0.52088f, 0.52844f,  -1.03655f, -0.30974f,
  2.59952f,  -1.93604f, 0.00000f,  2.51787f,
};

static float av1_tx_type_nn_16x4_ver_layer1_weights[32] = {
  0.10916f,  -0.21219f, -0.51340f, 0.69161f,  1.45988f,  -1.36942f, -0.40899f,
  1.05136f,  -0.08486f, 0.10008f,  -0.55304f, 0.88012f,  1.61177f,  -1.64507f,
  0.63428f,  1.15130f,  -0.17287f, -0.18592f, -0.01143f, 0.88293f,  1.73326f,
  -1.63624f, 0.09359f,  1.18393f,  0.26531f,  0.22378f,  0.15170f,  1.06965f,
  1.26814f,  -1.93873f, -0.00768f, 1.58309f,
};

static float av1_tx_type_nn_16x4_ver_layer1_bias[4] = {
  2.34713f,
  1.68667f,
  1.25488f,
  1.69812f,
};

static float av1_tx_type_nn_16x4_ver_layer0_out[8] = { 0 };
static float av1_tx_type_nn_16x4_ver_layer1_out[4] = { 0 };

static NN_CONFIG_V2 av1_tx_type_nnconfig_16x4_ver = {
  1,  // num_hidden_layers
  {
      // fc layer setting
      {
          // layer 0
          4,                                       // num_inputs
          8,                                       // num_outputs
          av1_tx_type_nn_16x4_ver_layer0_weights,  // weights
          av1_tx_type_nn_16x4_ver_layer0_bias,     // bias
          RELU,                                    // activation
          av1_tx_type_nn_16x4_ver_layer0_out,      // output
          NULL,
          NULL,
          NULL,
      },
      {
          8,  // num_inputs (!!same as num_outputs of last layer)
          4,
          av1_tx_type_nn_16x4_ver_layer1_weights,
          av1_tx_type_nn_16x4_ver_layer1_bias,
          NONE,
          av1_tx_type_nn_16x4_ver_layer1_out,
          NULL,
          NULL,
          NULL,
      },
  },
  4,                                   // num_outputs
  av1_tx_type_nn_16x4_ver_layer1_out,  // logits (!!same as last layer output)
  SOFTMAX_CROSS_ENTROPY,
};
/******************************************************************************/

// Map tx_size to its corresponding neural net model for tx type prediction.
static NN_CONFIG_V2 *av1_tx_type_nnconfig_map_hor[] = {
  &av1_tx_type_nnconfig_4x4_hor,   // 4x4 transform
  &av1_tx_type_nnconfig_8x8_hor,   // 8x8 transform
  &av1_tx_type_nnconfig_16x16,     // 16x16 transform
  NULL,                            // 32x32 transform
  NULL,                            // 64x64 transform
  &av1_tx_type_nnconfig_4x8_hor,   // 4x8 transform
  &av1_tx_type_nnconfig_8x4_hor,   // 8x4 transform
  &av1_tx_type_nnconfig_8x16_hor,  // 8x16 transform
  &av1_tx_type_nnconfig_16x8_hor,  // 16x8 transform
  NULL,                            // 16x32 transform
  NULL,                            // 32x16 transform
  NULL,                            // 32x64 transform
  NULL,                            // 64x32 transform
  &av1_tx_type_nnconfig_4x16_hor,  // 4x16 transform
  &av1_tx_type_nnconfig_16x4_hor,  // 16x4 transform
  NULL,                            // 8x32 transform
  NULL,                            // 32x8 transform
  NULL,                            // 16x64 transform
  NULL,                            // 64x16 transform
  NULL,                            // 4x32 transform
  NULL,                            // 32x4 transform
  NULL,                            // 8x64 transform
  NULL,                            // 64x8 transform
  NULL,                            // 4x64 transform
  NULL,                            // 64x4 transform
};

static NN_CONFIG_V2 *av1_tx_type_nnconfig_map_ver[] = {
  &av1_tx_type_nnconfig_4x4_ver,   // 4x4 transform
  &av1_tx_type_nnconfig_8x8_ver,   // 8x8 transform
  &av1_tx_type_nnconfig_16x16,     // 16x16 transform
  NULL,                            // 32x32 transform
  NULL,                            // 64x64 transform
  &av1_tx_type_nnconfig_4x8_ver,   // 4x8 transform
  &av1_tx_type_nnconfig_8x4_ver,   // 8x4 transform
  &av1_tx_type_nnconfig_8x16_ver,  // 8x16 transform
  &av1_tx_type_nnconfig_16x8_ver,  // 16x8 transform
  NULL,                            // 16x32 transform
  NULL,                            // 32x16 transform
  NULL,                            // 32x64 transform
  NULL,                            // 64x32 transform
  &av1_tx_type_nnconfig_4x16_ver,  // 4x16 transform
  &av1_tx_type_nnconfig_16x4_ver,  // 16x4 transform
  NULL,                            // 8x32 transform
  NULL,                            // 32x8 transform
  NULL,                            // 16x64 transform
  NULL,                            // 64x16 transform
  NULL,                            // 4x32 transform
  NULL,                            // 32x4 transform
  NULL,                            // 8x64 transform
  NULL,                            // 64x8 transform
  NULL,                            // 4x64 transform
  NULL,                            // 64x4 transform
};
#else
/******************************CONFIG_NN***************************************/
// Tx type model for 4x4 block.
static const float av1_tx_type_nn_weights_4x4_hor_layer0[32] = {
  -1.64947f, -1.54497f, -1.62832f, -0.17774f, -2.89498f, -0.72498f, 0.72036f,
  0.17996f,  1.20000f,  -0.27654f, 0.77396f,  1.21684f,  -1.75909f, -0.51272f,
  -1.25923f, 0.35005f,  -0.04257f, -0.23389f, -0.41841f, -0.08229f, 0.09503f,
  2.73144f,  -0.16875f, -0.23482f, 0.02194f,  -0.26427f, 0.28049f,  0.21260f,
  1.35792f,  0.27733f,  0.88660f,  -0.68304f,
};

static const float av1_tx_type_nn_bias_4x4_hor_layer0[8] = {
  1.38742f, 0.59540f,  -1.37622f, 1.92114f,
  0.00000f, -0.38998f, -0.32726f, -0.15650f,
};

static const float av1_tx_type_nn_weights_4x4_hor_layer1[32] = {
  1.65254f,  1.00915f,  -0.89318f, -2.05142f, -0.23235f, 0.96781f,  -0.37145f,
  -0.21056f, 1.13891f,  0.38675f,  0.87739f,  -1.42697f, 0.48015f,  0.61883f,
  -0.03979f, 0.11487f,  0.48042f,  0.45200f,  -0.23242f, 0.75166f,  0.55458f,
  0.39452f,  -0.35285f, 1.59120f,  -1.49221f, -0.48349f, -0.64692f, 1.49297f,
  -0.26782f, -0.65416f, -0.10648f, 0.05568f,
};

static const float av1_tx_type_nn_bias_4x4_hor_layer1[4] = {
  4.07177f,
  3.26961f,
  0.58083f,
  1.21199f,
};

static const NN_CONFIG av1_tx_type_nnconfig_4x4_hor = {
  4,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      8,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_4x4_hor_layer0,
    av1_tx_type_nn_weights_4x4_hor_layer1 },
  { av1_tx_type_nn_bias_4x4_hor_layer0, av1_tx_type_nn_bias_4x4_hor_layer1 }
};

static const float av1_tx_type_nn_weights_4x4_ver_layer0[32] = {
  -0.02032f, 2.61610f,  0.02098f,  -0.30217f, 0.12637f,  0.11017f,  -3.01996f,
  0.35144f,  1.93776f,  -0.20463f, 1.64102f,  -1.41986f, -3.66717f, -0.51655f,
  0.43910f,  0.37778f,  -1.02634f, 0.85337f,  -0.69753f, 1.00206f,  2.11784f,
  1.89427f,  1.92919f,  0.43201f,  -1.67358f, -1.67035f, -1.54623f, 0.16714f,
  -0.06589f, -0.28142f, -0.33118f, 1.72227f,
};

static const float av1_tx_type_nn_bias_4x4_ver_layer0[8] = {
  -0.33685f, 0.22025f,  0.28140f, 0.56138f,
  0.93489f,  -1.77048f, 1.34989f, -0.93747f,
};

static const float av1_tx_type_nn_weights_4x4_ver_layer1[32] = {
  -1.39506f, -1.06271f, -1.10886f, -1.69719f, 0.19699f,  -2.39850f, -1.26457f,
  0.75328f,  -1.26005f, -0.82738f, -0.12015f, -1.02702f, 1.40828f,  -2.37739f,
  -0.65639f, -0.71992f, -0.90453f, -1.12510f, -2.41362f, -1.16061f, -1.85577f,
  -0.99165f, -1.91366f, 0.16785f,  0.34776f,  0.58154f,  -0.18217f, -0.29257f,
  -0.86315f, -0.53336f, 0.30320f,  -1.32331f,
};

static const float av1_tx_type_nn_bias_4x4_ver_layer1[4] = {
  -1.31519f,
  -3.26321f,
  1.71794f,
  -1.90778f,
};

static const NN_CONFIG av1_tx_type_nnconfig_4x4_ver = {
  4,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      8,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_4x4_ver_layer0,
    av1_tx_type_nn_weights_4x4_ver_layer1 },
  { av1_tx_type_nn_bias_4x4_ver_layer0, av1_tx_type_nn_bias_4x4_ver_layer1 }
};
/******************************************************************************/

// Tx type model for 4x8 block.
static const float av1_tx_type_nn_weights_4x8_hor_layer0[32] = {
  0.00218f,  -0.41880f, -0.61215f, -0.92588f, 0.54291f,  -0.10898f, 0.70691f,
  0.46819f,  -1.61598f, -0.08834f, -0.96839f, 1.18489f,  -0.45171f, -0.65445f,
  -0.32179f, -0.10399f, 1.04379f,  0.91895f,  0.85589f,  0.08267f,  1.35388f,
  -2.03096f, 0.08168f,  -0.06372f, -0.26732f, -0.48262f, -0.08682f, 2.44071f,
  -1.35896f, -1.17121f, 1.68866f,  0.10357f,
};

static const float av1_tx_type_nn_bias_4x8_hor_layer0[8] = {
  2.93391f,  0.66831f, -0.21419f, 0.00000f,
  -0.72878f, 0.15127f, -1.46755f, 0.16658f,
};

static const float av1_tx_type_nn_weights_4x8_hor_layer1[32] = {
  -1.52077f, -1.06243f, 0.35319f,  -0.49207f, 0.54524f,  0.44271f, 1.37117f,
  -0.38957f, -1.28889f, -0.57133f, 0.04658f,  0.62278f,  0.37984f, 0.33247f,
  1.65547f,  -0.56806f, -1.38645f, -0.76258f, 0.67926f,  0.08783f, -0.01443f,
  0.34950f,  1.45812f,  -0.51332f, -1.41331f, -0.16453f, 0.05755f, 0.31405f,
  -0.50191f, 0.18219f,  1.83664f,  -0.75276f,
};

static const float av1_tx_type_nn_bias_4x8_hor_layer1[4] = {
  -1.17455f,
  -2.26089f,
  -1.79863f,
  -2.26333f,
};

static const NN_CONFIG av1_tx_type_nnconfig_4x8_hor = {
  4,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      8,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_4x8_hor_layer0,
    av1_tx_type_nn_weights_4x8_hor_layer1 },
  { av1_tx_type_nn_bias_4x8_hor_layer0, av1_tx_type_nn_bias_4x8_hor_layer1 }
};

static const float av1_tx_type_nn_weights_4x8_ver_layer0[128] = {
  -0.00952f, -0.98858f, -0.93181f, 1.39594f,  0.96559f,  0.18162f,  -0.76064f,
  -0.06066f, 0.07907f,  -0.09365f, -0.21313f, -0.02187f, -2.61707f, -2.68702f,
  -0.10982f, 0.18559f,  1.17049f,  1.11387f,  1.12697f,  1.05804f,  1.12764f,
  1.06318f,  1.12052f,  0.17406f,  1.83157f,  0.19362f,  0.46910f,  0.39608f,
  0.33342f,  0.40083f,  0.27645f,  1.06864f,  -4.06645f, -0.38775f, -0.11070f,
  0.03781f,  -0.09141f, 0.06185f,  -0.04852f, 0.20163f,  0.16784f,  0.16641f,
  -0.50941f, -0.61087f, 2.07008f,  -0.82381f, -0.85558f, 0.05528f,  -0.10535f,
  -2.81150f, 0.67038f,  0.43643f,  0.49062f,  -0.04465f, 0.90438f,  0.00977f,
  0.46272f,  1.59751f,  0.95234f,  0.35086f,  0.85624f,  0.73149f,  1.67779f,
  -2.21511f, -1.24746f, -1.09014f, -0.92441f, -1.22591f, -1.06961f, -0.95897f,
  -1.24956f, 0.73797f,  1.23275f,  -0.60064f, -0.07851f, 0.14397f,  0.22110f,
  -0.04422f, 0.14350f,  0.75926f,  0.35032f,  0.48104f,  2.81408f,  0.34662f,
  0.42090f,  0.35521f,  -1.36804f, -0.14974f, -0.47696f, -0.07892f, 0.36910f,
  0.32299f,  0.23916f,  0.06032f,  -0.17844f, -0.17558f, -1.42746f, -0.55828f,
  -1.00418f, -0.64823f, -0.73654f, -0.85197f, -1.50989f, 1.69385f,  -0.04973f,
  -0.09273f, 1.04249f,  0.79235f,  1.13229f,  0.99617f,  0.03851f,  0.56334f,
  0.90795f,  1.08296f,  0.58519f,  1.74765f,  0.63971f,  1.35951f,  0.07803f,
  -0.05127f, 0.26514f,  -0.84629f, -0.66343f, -2.10630f, 0.11017f,  2.18528f,
  -0.21958f, 0.05970f,
};

static const float av1_tx_type_nn_bias_4x8_ver_layer0[16] = {
  0.04205f, 0.22260f, -1.03870f, -1.19568f, 0.44283f,  0.01143f,
  0.00235f, 4.26772f, 0.44364f,  -0.33199f, -0.39076f, -0.35129f,
  0.08288f, 0.18195f, -0.79890f, 0.10047f,
};

static const float av1_tx_type_nn_weights_4x8_ver_layer1[64] = {
  -0.38193f, -0.12095f, 1.57802f,  0.34932f,  -0.47333f, -0.12304f, -0.01736f,
  -2.52445f, 0.18983f,  -0.64707f, -0.60889f, -0.53750f, 0.91666f,  -0.62823f,
  -0.13377f, -0.43594f, -0.38618f, -0.01328f, 0.97457f,  1.48589f,  -1.03238f,
  -0.33459f, -0.35108f, -2.42417f, 0.60229f,  0.06824f,  -0.75495f, 0.26902f,
  0.65311f,  -0.23887f, -0.44604f, -0.55800f, -0.33842f, 0.04259f,  -0.59589f,
  0.49738f,  -0.62301f, -0.30896f, -0.29602f, -2.57052f, 2.00943f,  -0.66490f,
  -0.76312f, 0.28256f,  1.06311f,  -0.38364f, -0.63508f, -0.57609f, -0.88765f,
  -1.04403f, -0.46531f, 0.34084f,  -1.20498f, -0.68352f, -0.72251f, -2.63242f,
  -0.68736f, -0.37904f, -1.32371f, 0.47288f,  1.51904f,  0.78372f,  -1.01830f,
  -1.01848f,
};

static const float av1_tx_type_nn_bias_4x8_ver_layer1[4] = {
  -1.45955f,
  -2.08949f,
  -1.24813f,
  -1.55368f,
};

static const NN_CONFIG av1_tx_type_nnconfig_4x8_ver = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_4x8_ver_layer0,
    av1_tx_type_nn_weights_4x8_ver_layer1 },
  { av1_tx_type_nn_bias_4x8_ver_layer0, av1_tx_type_nn_bias_4x8_ver_layer1 }
};
/******************************************************************************/

// Tx type model for 8x4 block.
static const float av1_tx_type_nn_weights_8x4_hor_layer0[128] = {
  -0.22492f, 0.13341f,  -4.03243f, -0.64015f, 0.02783f,  0.60466f,  -0.13335f,
  0.16828f,  0.12336f,  0.52904f,  1.18455f,  -0.32425f, 0.13052f,  0.93810f,
  -3.71165f, 0.02990f,  -4.63558f, 0.05666f,  0.03524f,  -0.07449f, -0.44006f,
  -0.33215f, -0.33713f, 0.08097f,  0.60873f,  0.29582f,  0.21696f,  -0.78729f,
  -0.16757f, -0.26567f, -0.00720f, -1.11226f, 1.58189f,  1.58463f,  1.48536f,
  1.54374f,  1.60069f,  1.46125f,  1.53932f,  0.05974f,  -1.82192f, 0.47043f,
  0.38090f,  0.20833f,  -0.05637f, 0.05183f,  0.01323f,  -0.25662f, 0.78634f,
  -0.55069f, -0.02975f, -1.29294f, -0.77192f, -2.34299f, -1.28074f, 0.77894f,
  -1.69740f, -1.66032f, -1.44323f, -1.55063f, -1.50845f, -1.23690f, -1.80663f,
  0.75079f,  2.32551f,  0.05878f,  0.80438f,  0.88584f,  0.69153f,  0.89060f,
  0.73660f,  0.87259f,  -0.00745f, -1.30044f, -0.59430f, 2.07270f,  1.03307f,
  -0.84697f, -1.19393f, 0.17549f,  -0.24978f, -3.67234f, 0.20781f,  -0.53946f,
  -0.05068f, 0.88274f,  1.30371f,  0.10288f,  0.07585f,  0.12259f,  -0.30815f,
  0.25437f,  -2.82096f, -2.69482f, 0.02370f,  0.12500f,  -0.21019f, -0.49220f,
  0.03638f,  -0.29795f, 0.28645f,  -0.48432f, -0.38584f, -0.32148f, -0.47197f,
  0.32437f,  0.32528f,  -0.19437f, 0.30383f,  -0.31879f, 0.26359f,  -0.12164f,
  -0.43647f, -0.08288f, -0.33438f, -0.63608f, -0.46647f, -0.46574f, 0.47806f,
  -0.49012f, -1.51234f, -1.13502f, -1.20470f, -1.02913f, -1.09182f, -0.93921f,
  -1.85523f, 0.92532f,
};

static const float av1_tx_type_nn_bias_8x4_hor_layer0[16] = {
  0.36631f,  0.02901f,  0.64305f,  1.53074f, -1.40229f, 0.03852f,
  -0.05043f, 0.89632f,  -1.23312f, 0.07036f, 0.17070f,  0.56250f,
  -0.28958f, -0.32869f, -0.01704f, 0.68171f,
};

static const float av1_tx_type_nn_weights_8x4_hor_layer1[64] = {
  -0.49441f, -0.31960f, -0.84946f, -0.85800f, -2.37767f, 0.81373f,  -0.73172f,
  -0.69337f, 0.88807f,  -0.49242f, -0.44717f, -0.11436f, 0.09978f,  0.15393f,
  0.17083f,  1.44850f,  -0.20582f, -0.04906f, 0.42990f,  -0.61939f, -1.09692f,
  -1.14885f, -1.36879f, -1.30828f, -0.59558f, -0.30903f, -0.08906f, 0.06953f,
  0.15383f,  -0.04193f, -0.54858f, 1.82676f,  -0.22411f, 0.05264f,  -0.45848f,
  -0.72985f, 0.87553f,  0.04116f,  -1.29774f, -2.63018f, 1.09089f,  -0.36048f,
  -0.16725f, 0.11627f,  0.49918f,  0.07539f,  0.00763f,  0.73706f,  0.87800f,
  0.57049f,  0.60969f,  1.02779f,  1.53339f,  -0.35915f, 0.06410f,  1.44582f,
  0.09698f,  0.71888f,  0.60594f,  0.84103f,  -0.50440f, -0.38825f, 0.15626f,
  -1.10654f,
};

static const float av1_tx_type_nn_bias_8x4_hor_layer1[4] = {
  -0.92861f,
  -1.45151f,
  -1.33588f,
  -4.33853f,
};

static const NN_CONFIG av1_tx_type_nnconfig_8x4_hor = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_8x4_hor_layer0,
    av1_tx_type_nn_weights_8x4_hor_layer1 },
  { av1_tx_type_nn_bias_8x4_hor_layer0, av1_tx_type_nn_bias_8x4_hor_layer1 }
};

static const float av1_tx_type_nn_weights_8x4_ver_layer0[32] = {
  -1.10946f, 1.86574f,  -1.59343f, 0.27018f, -1.70676f, -0.73982f, -0.19021f,
  -1.94208f, -2.29759f, -1.44402f, 0.28700f, -1.18340f, -1.50158f, -0.44175f,
  -1.36831f, 1.00374f,  2.59312f,  0.50291f, -0.71042f, -0.12238f, -0.15901f,
  -0.22807f, -0.67376f, -0.30215f, 0.54407f, -0.45538f, 1.18262f,  2.28687f,
  1.66212f,  1.70826f,  1.55182f,  0.12230f,
};

static const float av1_tx_type_nn_bias_8x4_ver_layer0[8] = {
  0.10943f,  2.09789f, 2.16578f, 0.15766f,
  -0.42461f, 0.00000f, 1.22090f, -1.28717f,
};

static const float av1_tx_type_nn_weights_8x4_ver_layer1[32] = {
  1.20426f,  -1.23237f, 2.41053f, -0.72488f, 1.25249f,  0.18018f,  -0.09586f,
  2.17901f,  0.15364f,  1.21535f, -0.38263f, -0.74309f, 0.50551f,  -0.54208f,
  0.59139f,  1.16095f,  0.55919f, -0.60183f, 1.18949f,  1.60787f,  0.54002f,
  -0.10712f, -0.16153f, 0.16207f, -0.32338f, 2.68712f,  -2.83483f, -0.27086f,
  -1.15005f, -0.39311f, 1.51236f, -1.68973f,
};

static const float av1_tx_type_nn_bias_8x4_ver_layer1[4] = {
  1.81013f,
  1.10517f,
  2.90059f,
  0.95391f,
};

static const NN_CONFIG av1_tx_type_nnconfig_8x4_ver = {
  4,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      8,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_8x4_ver_layer0,
    av1_tx_type_nn_weights_8x4_ver_layer1 },
  { av1_tx_type_nn_bias_8x4_ver_layer0, av1_tx_type_nn_bias_8x4_ver_layer1 }
};
/******************************************************************************/

// Tx type model for 8x8 block.
static const float av1_tx_type_nn_weights_8x8_hor_layer0[128] = {
  -0.85529f, 0.37619f,  0.12754f,  0.08622f,  0.45278f,  0.54929f,  1.60651f,
  -0.62654f, -0.54929f, -0.10131f, -0.17569f, 0.13948f,  0.31695f,  -0.05616f,
  0.20483f,  -0.36448f, 2.27203f,  -0.33087f, 0.47679f,  0.86888f,  0.39370f,
  0.46239f,  0.01113f,  1.50327f,  -1.48226f, -1.69621f, -1.49777f, -1.38885f,
  -1.37753f, -1.22681f, -1.70576f, 0.51329f,  -1.65662f, 1.74197f,  -0.13579f,
  -0.13133f, -0.58396f, -0.55510f, -1.10709f, -2.34975f, 0.22445f,  -0.56491f,
  -0.83432f, 0.13492f,  1.32147f,  2.85285f,  0.13819f,  0.03792f,  -1.30792f,
  0.04155f,  -0.70644f, -0.43430f, -0.16212f, -0.86945f, -1.16976f, 1.68339f,
  0.29540f,  0.01137f,  -0.25335f, -0.16856f, 0.12028f,  0.05207f,  0.39357f,
  -0.01545f, -0.21980f, -1.94091f, -1.01315f, -0.68270f, -0.40590f, -0.67111f,
  2.08283f,  0.19291f,  -4.81426f, -0.65044f, -0.24598f, 0.06371f,  -0.10272f,
  -0.14502f, -0.06821f, 0.45202f,  0.21091f,  -0.80864f, 0.39255f,  1.79189f,
  1.80453f,  1.10484f,  1.17608f,  0.96901f,  -0.35871f, -0.94311f, 0.63147f,
  2.95157f,  0.45917f,  -0.42849f, -0.55643f, -0.06097f, 3.49299f,  -0.50972f,
  0.11075f,  -0.08405f, -0.09274f, -0.22694f, -0.42426f, 0.48632f,  -1.61074f,
  1.82998f,  0.37623f,  -1.20330f, -0.01142f, -1.33307f, -0.27492f, -2.23621f,
  1.38846f,  1.42085f,  1.42568f,  1.36152f,  1.46910f,  1.27473f,  1.34752f,
  0.12753f,  -1.08197f, -1.08280f, -0.79489f, -1.12338f, -1.06795f, -0.87857f,
  -0.99892f, 1.09823f,
};

static const float av1_tx_type_nn_bias_8x8_hor_layer0[16] = {
  -0.49232f, -0.29685f, -1.44020f, 1.10940f,  1.16452f, -0.34862f,
  -0.38761f, -0.36243f, 0.21776f,  0.28234f,  2.34269f, -0.04104f,
  -0.26319f, 2.65579f,  -1.30137f, -0.01487f,
};

static const float av1_tx_type_nn_weights_8x8_hor_layer1[64] = {
  -0.38058f, -0.41295f, -1.26884f, -0.75560f, -1.57450f, 0.56072f,  -1.42322f,
  -0.29106f, 0.07228f,  0.04391f,  1.61388f,  -0.03055f, 0.81637f,  2.06045f,
  0.27119f,  -0.48328f, -0.45528f, -0.60534f, -1.61209f, -0.78157f, -1.65034f,
  0.60958f,  -1.30523f, 0.25143f,  0.11398f,  0.37860f,  1.54829f,  0.02309f,
  0.67288f,  2.11447f,  0.44845f,  -0.70406f, -0.67897f, -0.38759f, -1.30383f,
  -1.22646f, -1.54571f, 0.60552f,  -1.52565f, 0.11469f,  0.17344f,  0.08622f,
  1.57906f,  -0.00909f, 0.81634f,  2.04909f,  1.26466f,  -1.45741f, -0.75229f,
  0.06200f,  -1.05835f, -0.66257f, -1.73766f, 0.99923f,  -1.87082f, 0.14580f,
  0.49525f,  0.46839f,  1.32203f,  0.33923f,  0.97001f,  2.38584f,  1.58811f,
  0.06161f,
};

static const float av1_tx_type_nn_bias_8x8_hor_layer1[4] = {
  1.70385f,
  1.82373f,
  1.78496f,
  1.80826f,
};

static const NN_CONFIG av1_tx_type_nnconfig_8x8_hor = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_8x8_hor_layer0,
    av1_tx_type_nn_weights_8x8_hor_layer1 },
  { av1_tx_type_nn_bias_8x8_hor_layer0, av1_tx_type_nn_bias_8x8_hor_layer1 }
};

static const float av1_tx_type_nn_weights_8x8_ver_layer0[128] = {
  -0.67016f, -1.72366f, -1.86576f, -1.50962f, -1.70419f, -1.73964f, -1.84615f,
  2.09681f,  -0.05081f, -0.61030f, 2.02541f,  0.60222f,  0.99936f,  2.02114f,
  -0.53893f, -0.23757f, 0.73566f,  0.25443f,  0.00132f,  -0.74036f, -0.75351f,
  -0.76964f, -1.71007f, -0.15770f, 1.60982f,  2.17638f,  0.90681f,  0.64973f,
  0.85914f,  0.58786f,  -1.46228f, 0.05187f,  1.18804f,  0.30850f,  0.29512f,
  0.40526f,  0.37635f,  0.32311f,  0.37471f,  1.12346f,  3.41856f,  -0.36653f,
  0.42537f,  -0.19240f, 0.00155f,  0.30826f,  -0.02116f, -0.53435f, -0.34829f,
  -0.52466f, -0.11521f, -0.29163f, -2.05689f, -2.87372f, -0.62626f, 0.09585f,
  -0.75257f, 0.10057f,  1.43474f,  0.89450f,  0.75900f,  1.11147f,  1.00558f,
  0.25886f,  2.22095f,  -0.17926f, 0.57161f,  0.39546f,  0.47846f,  0.40452f,
  0.54298f,  0.45814f,  -3.62788f, -3.02374f, 0.03716f,  -0.13937f, -0.09415f,
  -0.12463f, 0.05682f,  0.03672f,  1.20746f,  1.25003f,  1.27071f,  1.31883f,
  1.27473f,  1.34943f,  1.23158f,  0.09039f,  0.19388f,  0.63420f,  2.79612f,
  0.93803f,  -0.11323f, -0.02027f, 0.41286f,  -0.05979f, -3.80705f, -0.52451f,
  -0.77098f, -0.68132f, -0.65559f, -0.60975f, -1.26165f, 0.25582f,  0.05346f,
  0.61403f,  0.32140f,  -2.39831f, -1.42355f, 1.30541f,  1.02361f,  0.12930f,
  -1.61469f, -0.77036f, -0.59144f, 1.27769f,  1.52068f,  0.82137f,  1.83159f,
  -0.66626f, -0.69806f, -1.00564f, -0.85995f, -0.90889f, -0.84412f, -0.85712f,
  -1.29848f, 0.39308f,
};

static const float av1_tx_type_nn_bias_8x8_ver_layer0[16] = {
  -0.14868f, -0.48343f, 3.94416f,  -0.78037f, -1.33789f, -0.60611f,
  0.51793f,  0.44030f,  -0.71563f, 0.22561f,  -1.19083f, -0.46149f,
  0.83015f,  0.06024f,  1.17180f,  0.65122f,
};

static const float av1_tx_type_nn_weights_8x8_ver_layer1[64] = {
  -1.42711f, -0.21683f, 2.12061f,  0.20489f,  -0.50228f, -0.24770f, 0.23391f,
  1.03470f,  -0.44847f, -0.63225f, -0.21583f, -0.06467f, -0.21892f, -0.07786f,
  1.43322f,  0.00280f,  -1.53057f, -0.18912f, 1.95333f,  0.31151f,  -2.07601f,
  0.06776f,  0.25529f,  0.94800f,  -1.11453f, -0.20594f, -0.13281f, 0.01485f,
  0.17650f,  -0.07955f, 1.43734f,  -0.23193f, -2.06463f, -0.21238f, 2.13707f,
  0.30351f,  0.27594f,  -0.36245f, 0.19539f,  0.91045f,  -0.24068f, -0.37616f,
  0.88792f,  0.02947f,  -0.16903f, -0.04932f, 1.51293f,  -0.95967f, -1.62903f,
  0.05326f,  2.30703f,  0.64445f,  -1.09464f, -0.16623f, 1.00240f,  0.07548f,
  -0.50406f, 0.63854f,  1.02340f,  0.49833f,  0.13671f,  0.26722f,  2.09516f,
  -0.41305f,
};

static const float av1_tx_type_nn_bias_8x8_ver_layer1[4] = {
  2.14067f,
  2.76699f,
  2.04233f,
  1.34803f,
};

static const NN_CONFIG av1_tx_type_nnconfig_8x8_ver = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_8x8_ver_layer0,
    av1_tx_type_nn_weights_8x8_ver_layer1 },
  { av1_tx_type_nn_bias_8x8_ver_layer0, av1_tx_type_nn_bias_8x8_ver_layer1 }
};
/******************************************************************************/

// Tx type model for 8x16 block.
static const float av1_tx_type_nn_weights_8x16_hor_layer0[128] = {
  -1.61872f, -1.58520f, -1.41236f, -1.53255f, -1.59794f, -1.25769f, -1.90043f,
  0.73431f,  1.10135f,  0.47054f,  0.43230f,  -0.43009f, -0.09135f, -0.07289f,
  -0.38785f, 1.23775f,  -0.35312f, 0.73789f,  0.88864f,  0.75957f,  0.62579f,
  0.46974f,  0.21851f,  1.63821f,  -2.27289f, -0.68522f, -0.69814f, -0.84368f,
  -0.91320f, -0.63055f, -1.03296f, 0.55778f,  -0.00071f, 1.27539f,  1.60068f,
  1.40975f,  0.97372f,  0.92843f,  1.90853f,  0.12626f,  1.71953f,  1.41978f,
  -0.12234f, -1.27058f, 0.76207f,  0.02495f,  -0.67038f, -0.05255f, 1.72923f,
  1.47630f,  1.47058f,  1.47614f,  1.49354f,  1.66131f,  1.50801f,  0.17145f,
  -2.30947f, -2.10850f, -1.25636f, -0.24900f, 0.72602f,  1.26572f,  0.97865f,
  -0.65466f, 1.31129f,  0.26916f,  0.12139f,  -0.12761f, -0.39143f, -0.28134f,
  0.06584f,  2.24418f,  0.22516f,  0.05011f,  -0.01671f, -0.29476f, -0.40326f,
  0.21138f,  -0.11573f, -0.31154f, -0.36828f, 0.03694f,  -0.07172f, -0.63419f,
  -3.14351f, -1.23125f, 0.65311f,  -0.11406f, 1.97287f,  -0.10422f, 0.83896f,
  0.85033f,  0.49724f,  0.80482f,  0.51454f,  1.06447f,  0.76693f,  0.72599f,
  -0.78573f, -0.53950f, 0.40894f,  0.00086f,  0.10784f,  -0.70498f, 1.16395f,
  1.14597f,  1.13496f,  1.12177f,  1.02100f,  -1.37574f, -2.97144f, 0.33899f,
  0.42013f,  0.86327f,  2.31983f,  2.04008f,  0.95503f,  0.15081f,  0.11530f,
  -0.02574f, -4.77119f, 0.13257f,  -0.01704f, -0.23087f, -0.00825f, 0.07029f,
  -0.28136f, 0.42556f,
};

static const float av1_tx_type_nn_bias_8x16_hor_layer0[16] = {
  0.93617f,  -0.24000f, -1.26821f, 0.78780f,  0.13690f, -0.21948f,
  -1.45162f, 0.44584f,  -1.92582f, -0.23169f, 0.56004f, -1.19937f,
  1.81560f,  -1.02643f, -0.81690f, 0.08302f,
};

static const float av1_tx_type_nn_weights_8x16_hor_layer1[64] = {
  0.06696f,  -0.11538f, -1.42029f, 0.32965f,  0.81046f,  0.01146f,  1.20945f,
  -0.16899f, 0.53224f,  -0.40232f, 0.01786f,  -0.73242f, 1.29750f,  1.95185f,
  0.70143f,  1.43287f,  0.76220f,  0.79937f,  -1.79011f, -1.15178f, 0.42526f,
  -0.67519f, 0.77267f,  -0.30697f, 2.46004f,  -0.49828f, 0.02875f,  1.09972f,
  1.47662f,  0.61719f,  0.61417f,  -0.12363f, 2.53048f,  0.00418f,  -1.38964f,
  0.88117f,  0.39239f,  -0.19347f, -2.58600f, -0.33715f, 1.09323f,  -0.32127f,
  0.02456f,  -0.19125f, 1.12728f,  0.66502f,  0.34296f,  1.14897f,  0.29967f,
  1.19209f,  0.22108f,  -0.11975f, 1.49776f,  -1.34624f, -2.58478f, -1.34632f,
  1.53207f,  0.45634f,  -1.48476f, 0.17489f,  0.71790f,  -2.12086f, -1.21778f,
  -1.31243f,
};

static const float av1_tx_type_nn_bias_8x16_hor_layer1[4] = {
  0.83359f,
  1.06875f,
  1.77645f,
  1.49570f,
};

static const NN_CONFIG av1_tx_type_nnconfig_8x16_hor = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_8x16_hor_layer0,
    av1_tx_type_nn_weights_8x16_hor_layer1 },
  { av1_tx_type_nn_bias_8x16_hor_layer0, av1_tx_type_nn_bias_8x16_hor_layer1 }
};

static const float av1_tx_type_nn_weights_8x16_ver_layer0[128] = {
  0.32858f,  -1.28887f, 0.25632f,  -0.05262f, 2.69203f,  -0.07004f, 1.37337f,
  -0.05725f, -0.05659f, 0.05592f,  0.01039f,  -0.29343f, 1.58628f,  -0.30003f,
  -3.43118f, 0.00272f,  1.70928f,  -0.76348f, 0.05889f,  -0.03263f, -0.07724f,
  0.03523f,  -0.19890f, 1.18005f,  -0.03605f, -0.20530f, -4.00733f, 0.10210f,
  -0.05368f, -0.17650f, -0.15317f, 0.06499f,  0.56705f,  1.04341f,  0.62890f,
  0.73451f,  -0.22199f, 0.86659f,  0.78443f,  -0.61664f, -0.50606f, 0.30247f,
  0.14455f,  0.39276f,  0.49203f,  0.65019f,  0.12269f,  1.64080f,  1.68289f,
  1.42694f,  1.60825f,  1.58501f,  1.47252f,  1.62589f,  1.48218f,  0.17726f,
  -0.04884f, 0.35376f,  -0.04796f, 0.32589f,  0.35087f,  0.35258f,  -0.46103f,
  -0.31176f, -0.05203f, 0.07247f,  -0.26756f, 0.22019f,  0.03412f,  0.33773f,
  0.29811f,  -0.11140f, 0.12831f,  -0.44673f, -0.09858f, 0.07889f,  0.15137f,
  0.00347f,  -0.23394f, 0.08886f,  -0.31201f, -0.79912f, -0.51092f, 0.14123f,
  -1.09599f, -4.26020f, -0.68675f, -0.02842f, -1.54538f, -1.28977f, -1.30558f,
  -1.21074f, -1.37142f, -1.14743f, -1.85397f, 0.82985f,  -0.30681f, 0.04494f,
  -0.24023f, -4.18053f, -0.16096f, -0.55492f, -0.27882f, 0.05829f,  -0.41224f,
  -2.52088f, -0.56162f, -1.04547f, -1.70685f, -0.28842f, -1.43673f, -0.01468f,
  -3.20585f, -0.69120f, -0.43931f, -0.46270f, -0.65885f, -0.55884f, -0.75138f,
  0.36381f,  -5.70858f, -0.14548f, -0.15745f, -0.11812f, -0.07605f, -0.07693f,
  -0.12236f, 0.16075f,
};

static const float av1_tx_type_nn_bias_8x16_ver_layer0[16] = {
  -0.35385f, 0.30491f,  -0.90011f, 0.42941f,  1.20928f, -0.88331f,
  -1.48818f, -0.34785f, -0.32668f, -0.22695f, 0.89188f, 0.65521f,
  0.57598f,  0.99819f,  0.75175f,  0.17044f,
};

static const float av1_tx_type_nn_weights_8x16_ver_layer1[64] = {
  -0.62913f, -0.34304f, 0.42963f,  -0.17440f, -1.44092f, 0.69142f,  -1.36067f,
  0.52211f,  0.44658f,  -0.26501f, -0.41657f, 0.34428f,  -0.34390f, -0.58567f,
  -0.84097f, -1.96311f, -0.37215f, -0.22250f, -1.23811f, -0.07247f, -0.81731f,
  0.58755f,  -1.30559f, 0.39551f,  0.41743f,  -0.09940f, -0.33230f, 0.14458f,
  -0.25139f, -0.54517f, 0.13469f,  -0.38157f, -0.39109f, -0.18205f, 0.06834f,
  -0.08395f, -0.92187f, 0.56724f,  1.44381f,  0.53226f,  -0.22356f, 0.12285f,
  -0.29418f, -1.86749f, -0.22372f, -0.60204f, -0.87746f, -1.16936f, 0.56884f,
  0.62641f,  -0.11823f, 1.00395f,  1.64794f,  -0.64535f, 2.29322f,  -0.23397f,
  0.17251f,  -0.35927f, 0.65631f,  -0.26812f, 0.80128f,  0.85748f,  0.47404f,
  2.20547f,
};

static const float av1_tx_type_nn_bias_8x16_ver_layer1[4] = {
  -0.44080f,
  -1.67455f,
  -1.46332f,
  -6.13206f,
};

static const NN_CONFIG av1_tx_type_nnconfig_8x16_ver = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_8x16_ver_layer0,
    av1_tx_type_nn_weights_8x16_ver_layer1 },
  { av1_tx_type_nn_bias_8x16_ver_layer0, av1_tx_type_nn_bias_8x16_ver_layer1 }
};
/******************************************************************************/

// Tx type model for 16x8 block.
static const float av1_tx_type_nn_weights_16x8_hor_layer0[128] = {
  0.02600f,  0.09786f,  -1.05107f, -0.35594f, -0.15658f, 2.99828f,  -0.07106f,
  -0.10101f, -0.14412f, -0.83790f, -0.19434f, 2.28368f,  1.91727f,  -0.00956f,
  -0.90640f, 0.09174f,  1.58895f,  1.38945f,  1.49431f,  1.51381f,  1.44803f,
  1.53544f,  1.44694f,  0.17753f,  1.69735f,  -0.78652f, 0.31092f,  -0.23736f,
  0.02231f,  -0.09884f, -0.00493f, 1.21189f,  -1.94382f, -0.34629f, -0.58309f,
  0.72291f,  -0.30056f, 0.90660f,  -0.57495f, 3.07809f,  0.73644f,  1.43050f,
  1.34356f,  -0.66554f, 0.50102f,  -0.64305f, 0.42044f,  -1.66165f, -0.05733f,
  -2.51402f, -1.01067f, -0.33390f, -0.32986f, -0.92431f, 1.86281f,  -0.07290f,
  -0.26290f, -0.68941f, 1.81156f,  0.66125f,  -2.09974f, 0.17032f,  -0.67461f,
  -0.00876f, -1.50154f, 1.17153f,  1.00377f,  0.33022f,  0.74689f,  0.42878f,
  0.61725f,  -0.83967f, 0.09467f,  -0.39892f, 0.33863f,  0.10656f,  -0.09249f,
  -0.39757f, 0.48481f,  -0.35162f, 1.47014f,  1.67827f,  -1.84051f, 0.16291f,
  -0.50135f, -2.29911f, -0.42217f, -0.13358f, 1.45899f,  -0.14743f, -0.02763f,
  -0.28003f, -0.01364f, 0.21014f,  -0.29026f, -0.20198f, 1.38782f,  0.56731f,
  0.27489f,  0.43227f,  0.41326f,  0.42721f,  0.87720f,  -1.90067f, -5.04951f,
  -0.17638f, -0.58119f, -0.08954f, -0.13692f, -0.12325f, -0.38548f, 0.66462f,
  -1.42377f, -1.21917f, -1.38193f, -1.36539f, -1.39378f, -1.19629f, -1.59812f,
  0.28689f,  0.32394f,  0.52128f,  0.01013f,  -0.28948f, -0.26293f, -0.44331f,
  -0.36570f, -0.50757f,
};

static const float av1_tx_type_nn_bias_16x8_hor_layer0[16] = {
  -0.08696f, -0.22110f, -1.43604f, -1.00451f, -1.51029f, 0.63736f,
  0.45260f,  0.16229f,  4.01393f,  -0.21748f, 0.36411f,  -0.08764f,
  -0.12329f, 0.08986f,  1.08117f,  -0.00220f,
};

static const float av1_tx_type_nn_weights_16x8_hor_layer1[64] = {
  0.55824f,  -0.14648f, 0.81947f,  -0.45867f, -1.86078f, -0.17291f, 0.34849f,
  0.15153f,  1.75625f,  -0.25760f, 0.72015f,  -0.30059f, -0.57975f, 0.07609f,
  -0.02036f, 0.07912f,  0.57080f,  -0.13792f, 0.74184f,  -0.87669f, -1.87572f,
  -0.27270f, 0.39751f,  0.19652f,  2.03514f,  -0.32944f, 0.76251f,  0.04399f,
  -0.63175f, 0.37420f,  0.08309f,  0.04466f,  0.60255f,  -0.12820f, 1.66065f,
  -0.59496f, -1.94794f, -0.14847f, 0.39424f,  0.16273f,  1.80587f,  0.41197f,
  0.74691f,  -0.21217f, -0.63173f, 0.09510f,  -0.35538f, -0.04407f, 0.92847f,
  0.20141f,  1.68680f,  -0.56528f, -2.26960f, 0.12978f,  0.73748f,  0.42438f,
  2.00673f,  -0.40189f, 0.95423f,  0.23234f,  -0.80953f, 0.65814f,  0.49444f,
  -0.23347f,
};

static const float av1_tx_type_nn_bias_16x8_hor_layer1[4] = {
  3.57175f,
  2.42612f,
  3.31259f,
  2.08287f,
};

static const NN_CONFIG av1_tx_type_nnconfig_16x8_hor = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_16x8_hor_layer0,
    av1_tx_type_nn_weights_16x8_hor_layer1 },
  { av1_tx_type_nn_bias_16x8_hor_layer0, av1_tx_type_nn_bias_16x8_hor_layer1 }
};

static const float av1_tx_type_nn_weights_16x8_ver_layer0[128] = {
  0.46633f,  1.55328f,  -0.11230f, -0.29571f, 0.18814f,  -1.52430f, -2.34660f,
  0.08644f,  -1.97718f, -1.29140f, -1.12262f, -1.12985f, -1.25911f, -0.96506f,
  -1.57129f, 0.96021f,  1.34192f,  1.28623f,  1.21655f,  1.28758f,  1.25482f,
  1.30195f,  1.19190f,  0.09310f,  0.52072f,  0.91487f,  1.24100f,  1.61236f,
  1.72166f,  2.20750f,  1.62379f,  -1.43936f, 0.50665f,  0.40213f,  0.66502f,
  -1.66699f, -3.07618f, 0.05877f,  0.60987f,  -0.09995f, -0.10916f, 0.48049f,
  0.23812f,  0.39847f,  -0.21682f, -0.63455f, 0.33453f,  -0.67939f, -4.14355f,
  -0.62756f, -0.22502f, -0.17215f, 0.01062f,  0.27049f,  -0.10748f, 0.30945f,
  2.72445f,  -0.89181f, -0.06800f, 0.20595f,  -0.73385f, 0.04071f,  -1.30294f,
  1.83507f,  0.92570f,  0.69609f,  0.76285f,  0.69892f,  0.76409f,  0.63104f,
  0.73397f,  1.09575f,  -0.20129f, -0.24022f, -0.24599f, -0.59107f, -0.88755f,
  -0.68987f, -0.75495f, -1.31002f, -1.30237f, -0.94093f, -2.15678f, -1.49303f,
  -1.17498f, -1.39952f, -0.91270f, -0.05587f, 1.02381f,  -0.75580f, -0.65263f,
  -0.78996f, -0.71075f, -0.71018f, -0.70350f, -1.26196f, 2.34208f,  -0.53611f,
  0.19752f,  -0.16842f, -0.24828f, 0.21857f,  0.08222f,  -2.55894f, -1.75702f,
  0.11394f,  1.03083f,  0.79972f,  -1.54112f, -1.82341f, -0.57597f, -0.02077f,
  -0.39616f, -0.00995f, -0.12809f, 0.01188f,  -0.25117f, 0.09202f,  0.09336f,
  -0.05614f, -0.30039f, 0.25834f,  1.19944f,  1.22533f,  0.92330f,  0.75967f,
  -0.81945f, -0.41647f,
};

static const float av1_tx_type_nn_bias_16x8_ver_layer0[16] = {
  0.17841f,  0.67315f,  -1.24450f, 3.13859f,  0.16203f, -0.14992f,
  0.29553f,  -1.15567f, -0.71421f, 1.15977f,  1.14585f, 3.02460f,
  -0.04510f, 0.48000f,  -0.09354f, -0.42422f,
};

static const float av1_tx_type_nn_weights_16x8_ver_layer1[64] = {
  0.29912f,  -0.10009f, -1.11478f, 1.76812f,  -0.27719f, 0.52148f,  0.17622f,
  -1.17116f, 0.73397f,  -0.69279f, -0.11080f, 1.53751f,  -1.42003f, 0.14731f,
  0.13592f,  -0.04883f, 0.39186f,  -0.13655f, -0.43994f, 1.82759f,  -0.25601f,
  -0.15018f, 0.51920f,  -1.56070f, 0.31683f,  -0.79367f, -0.02904f, 1.28637f,
  -1.15203f, 0.26627f,  0.42828f,  -0.24258f, 0.38647f,  -0.83352f, 0.32553f,
  2.09522f,  -0.26822f, -0.42191f, 0.32825f,  -1.30748f, 1.50551f,  -0.52669f,
  0.20045f,  1.69318f,  -1.47839f, 0.30802f,  -0.07290f, -0.28106f, 0.68192f,
  -0.15522f, 1.12579f,  2.21921f,  0.09720f,  -0.50265f, 0.83165f,  -1.31721f,
  0.72422f,  -1.24952f, 0.61653f,  2.04117f,  -1.42406f, 0.52568f,  -0.46180f,
  -0.00873f,
};

static const float av1_tx_type_nn_bias_16x8_ver_layer1[4] = {
  3.34981f,
  3.74710f,
  1.38339f,
  0.45176f,
};

static const NN_CONFIG av1_tx_type_nnconfig_16x8_ver = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_16x8_ver_layer0,
    av1_tx_type_nn_weights_16x8_ver_layer1 },
  { av1_tx_type_nn_bias_16x8_ver_layer0, av1_tx_type_nn_bias_16x8_ver_layer1 }
};
/******************************************************************************/

// Tx type model for 16x16 block.
static const float av1_tx_type_nn_weights_16x16_layer0[128] = {
  1.26592f,  1.36313f,  1.30956f,  1.29926f,  1.48816f,  1.68851f,  1.32000f,
  0.13321f,  -0.22477f, -0.88906f, -0.19622f, 1.69605f,  1.22180f,  -1.57771f,
  -1.15765f, 0.05710f,  -1.13355f, -0.85486f, -0.99971f, -0.91571f, -1.06031f,
  -0.77952f, -1.15723f, 1.17809f,  1.35602f,  -0.05243f, -0.37596f, 0.26108f,
  0.17611f,  -0.10323f, 0.77279f,  -0.48911f, -0.79308f, 0.55112f,  0.43918f,
  0.27872f,  0.28714f,  0.45830f,  1.05689f,  0.03705f,  -2.49975f, -0.01940f,
  0.05709f,  0.07942f,  -0.13290f, -0.10359f, 0.00143f,  0.37303f,  0.96470f,
  0.53293f,  1.14459f,  0.89185f,  0.43378f,  0.47764f,  0.90924f,  0.15279f,
  -0.15361f, 0.02949f,  0.42240f,  0.68143f,  0.89588f,  0.73754f,  0.10974f,
  1.57755f,  -0.39870f, -0.32914f, 0.35638f,  0.34991f,  -0.00003f, -0.23373f,
  0.29630f,  -0.76699f, -0.01356f, 0.04234f,  0.84253f,  1.92078f,  0.93160f,
  0.71993f,  0.71604f,  0.76455f,  -1.59782f, 0.32332f,  1.11628f,  0.33062f,
  -0.03728f, -0.05710f, 0.80447f,  -0.14719f, 1.34658f,  -0.05718f, 0.64015f,
  0.21926f,  0.41653f,  0.12720f,  0.54092f,  1.39411f,  1.81819f,  -0.24513f,
  0.00955f,  0.38011f,  -0.57787f, -0.41759f, 0.68834f,  -0.31783f, -0.40607f,
  -0.10107f, -0.79374f, 0.75599f,  -0.16282f, -0.14490f, -0.20783f, -0.55019f,
  -0.13793f, -0.22293f, 0.18305f,  0.12445f,  0.56830f,  0.24567f,  0.09278f,
  0.70803f,  0.35803f,  -1.52676f, -0.89624f, 0.77665f,  0.19877f,  0.77175f,
  0.50355f,  0.08592f,
};

static const float av1_tx_type_nn_bias_16x16_layer0[16] = {
  -1.31834f, 0.14346f,  -0.10062f, 0.84489f,  0.95617f,  -0.06720f,
  -0.68502f, -0.91442f, -0.31932f, 0.25276f,  -0.15138f, -1.57661f,
  -0.14062f, -0.42120f, 0.94573f,  -0.09287f,
};

static const float av1_tx_type_nn_weights_16x16_layer1[64] = {
  -1.80333f, -1.06353f, 0.55139f,  0.74644f,  0.13747f, -0.93018f, -0.10286f,
  0.67133f,  0.24460f,  1.44583f,  0.02173f,  0.26037f, -0.73687f, 0.19566f,
  0.61846f,  -0.58601f, -1.03196f, -0.74415f, 0.30041f, -0.41967f, 1.08740f,
  0.96224f,  -0.59139f, 0.03813f,  0.05403f,  1.33427f, -0.54375f, -1.92181f,
  0.54704f,  0.13608f,  0.22151f,  -0.38076f, 1.18390f, -0.77508f, -1.84283f,
  1.00894f,  0.62318f,  -0.15296f, 1.27600f,  0.22822f, 0.12751f,  0.93910f,
  -0.28502f, 0.53912f,  -0.96889f, 0.10182f,  0.81508f, -0.43028f, 2.67386f,
  0.52204f,  0.49820f,  -0.41711f, 1.05038f,  1.12192f, 0.74349f,  -0.75417f,
  -0.03718f, -0.35769f, 0.89651f,  0.63236f,  0.54215f, -0.07894f, 0.48274f,
  1.08829f,
};

static const float av1_tx_type_nn_bias_16x16_layer1[4] = {
  0.81986f,
  1.26865f,
  0.11118f,
  2.48404f,
};

static const NN_CONFIG av1_tx_type_nnconfig_16x16 = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  {
      av1_tx_type_nn_weights_16x16_layer0,
      av1_tx_type_nn_weights_16x16_layer1,
  },
  {
      av1_tx_type_nn_bias_16x16_layer0,
      av1_tx_type_nn_bias_16x16_layer1,
  },
};
/******************************************************************************/

// Tx type model for 4x16 block.
static const float av1_tx_type_nn_weights_4x16_hor_layer0[32] = {
  0.36539f,  0.25667f,  0.01491f,  -0.21959f, 2.55105f,  0.17615f, 1.79884f,
  1.65936f,  -0.44363f, 0.00706f,  -0.68004f, -0.64360f, 1.75760f, 1.91906f,
  1.47682f,  0.09650f,  -3.59244f, -0.35004f, 0.93295f,  0.25806f, -0.08154f,
  0.79332f,  0.79535f,  1.09467f,  1.57855f,  -0.51359f, 0.90553f, -1.67744f,
  -1.74563f, -0.88830f, -1.77603f, 2.15935f,
};

static const float av1_tx_type_nn_bias_4x16_hor_layer0[8] = {
  -0.36435f, -2.22731f, -0.00837f, -1.34546f,
  0.62806f,  -0.20675f, 4.91940f,  -0.56079f,
};

static const float av1_tx_type_nn_weights_4x16_hor_layer1[32] = {
  -0.57191f, -1.46418f, 0.67331f,  -1.15027f, 0.46288f,  0.81251f,  2.51768f,
  -0.27147f, 0.00761f,  -2.15214f, -0.69650f, -0.50808f, 0.92832f,  0.45668f,
  2.34201f,  -0.52941f, 0.51008f,  -1.55496f, -0.01371f, -0.12356f, 0.66624f,
  0.88043f,  2.64862f,  -1.28024f, -0.17578f, -1.80034f, -0.32217f, 0.89519f,
  1.28413f,  -0.30326f, 2.45329f,  -0.83335f,
};

static const float av1_tx_type_nn_bias_4x16_hor_layer1[4] = {
  2.33198f,
  3.36245f,
  1.62603f,
  2.91056f,
};

static const NN_CONFIG av1_tx_type_nnconfig_4x16_hor = {
  4,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      8,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_4x16_hor_layer0,
    av1_tx_type_nn_weights_4x16_hor_layer1 },
  { av1_tx_type_nn_bias_4x16_hor_layer0, av1_tx_type_nn_bias_4x16_hor_layer1 }
};

static const float av1_tx_type_nn_weights_4x16_ver_layer0[128] = {
  1.61392f,  1.41239f,  1.47646f,  1.47325f,  1.46110f,  1.49208f,  1.49414f,
  0.12835f,  -0.76986f, 0.07087f,  -0.24572f, -0.93168f, 3.07935f,  -0.18183f,
  -0.09831f, -0.07703f, -0.03222f, -0.25473f, -0.06090f, 2.93713f,  -0.38711f,
  -0.12884f, -0.18329f, -0.06262f, -0.00327f, -0.02930f, -0.01641f, -0.00622f,
  -0.03305f, -4.07069f, -2.76643f, 0.04413f,  -1.03176f, -0.19217f, -0.44980f,
  -2.48615f, -2.58112f, -0.87695f, 0.16187f,  -0.04891f, -0.06854f, 1.08104f,
  0.75245f,  1.49302f,  0.63363f,  1.45715f,  0.92574f,  1.72029f,  0.33326f,
  3.86646f,  0.04422f,  0.41019f,  0.36212f,  0.56600f,  -1.01552f, 0.05128f,
  0.40454f,  -1.05100f, -0.47461f, -1.33168f, -0.46145f, -1.36870f, -0.88838f,
  -1.05358f, -0.18537f, -0.34357f, -0.03698f, 0.68905f,  0.41010f,  0.31223f,
  -0.43382f, -0.74715f, 2.03366f,  -0.30419f, 0.45747f,  0.09526f,  0.31678f,
  0.22915f,  0.21832f,  1.26385f,  -0.06814f, -0.71417f, -1.18947f, 0.03762f,
  0.10936f,  2.97396f,  -0.42638f, -0.03123f, -5.49756f, -0.17029f, -0.11323f,
  0.05173f,  -0.44274f, -0.15738f, 0.11311f,  0.43872f,  0.16837f,  -0.52849f,
  2.90050f,  -0.54735f, -0.29591f, 1.24030f,  0.21696f,  -0.04443f, -1.60877f,
  -1.36365f, -1.27432f, -1.52060f, -1.34397f, -1.13371f, -1.87554f, 0.80123f,
  0.42820f,  -0.14157f, -2.73963f, -0.68040f, -0.35236f, 0.14490f,  2.23477f,
  0.01370f,  -0.20426f, -1.51411f, -0.72293f, 0.64516f,  0.97638f,  0.32616f,
  -0.27975f, -0.01149f,
};

static const float av1_tx_type_nn_bias_4x16_ver_layer0[16] = {
  -1.37863f, -0.05763f, -0.07041f, 0.15306f,  0.96026f,  -1.42105f,
  -0.55822f, 1.04845f,  -0.17662f, -1.25345f, -0.11927f, 0.49845f,
  -0.32530f, 0.73483f,  0.08322f,  -0.23890f,
};

static const float av1_tx_type_nn_weights_4x16_ver_layer1[64] = {
  0.27194f,  0.50607f,  0.49229f,  -0.48192f, 0.15667f,  -1.38891f, 0.38102f,
  -0.58825f, -0.07337f, -0.52909f, 0.36975f,  0.28710f,  0.34992f,  -0.73630f,
  0.30386f,  -0.58822f, 0.36127f,  0.57950f,  0.55878f,  -0.42796f, 0.19967f,
  -1.45517f, 0.42529f,  -0.54630f, -0.38169f, -0.84899f, 0.41622f,  0.46935f,
  0.39077f,  -0.75448f, 0.31698f,  -0.76187f, 0.97765f,  0.57052f,  0.55825f,
  -0.54273f, 0.20466f,  -1.46347f, 0.41813f,  -0.55019f, -0.19948f, -0.57982f,
  0.41206f,  0.32373f,  0.38537f,  -1.11657f, 0.32887f,  -0.76911f, 1.12259f,
  0.72163f,  0.82603f,  0.37786f,  0.34976f,  -1.86642f, 0.59961f,  -0.16329f,
  -0.36631f, -0.56814f, 0.60410f,  0.53158f,  0.56389f,  -0.70508f, 0.51009f,
  -0.56513f,
};

static const float av1_tx_type_nn_bias_4x16_ver_layer1[4] = {
  4.60896f,
  4.53551f,
  4.53124f,
  4.27435f,
};

static const NN_CONFIG av1_tx_type_nnconfig_4x16_ver = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_4x16_ver_layer0,
    av1_tx_type_nn_weights_4x16_ver_layer1 },
  { av1_tx_type_nn_bias_4x16_ver_layer0, av1_tx_type_nn_bias_4x16_ver_layer1 }
};
/******************************************************************************/

// Tx type model for 16x4 block.
static const float av1_tx_type_nn_weights_16x4_hor_layer0[128] = {
  1.45347f,  -0.15743f, 0.44236f,  0.25808f,  0.33944f,  0.38678f,  0.24428f,
  1.67287f,  0.09539f,  -0.42940f, -0.31507f, -0.00154f, -2.98755f, -2.27744f,
  -0.49183f, 0.09333f,  -0.99026f, -0.22157f, 0.53701f,  0.60447f,  0.15686f,
  -0.04646f, 0.26341f,  2.12361f,  0.27090f,  -1.14716f, -0.64146f, -0.91604f,
  -0.75335f, -0.60056f, -1.25084f, 1.68473f,  -3.24075f, -4.03867f, -2.07877f,
  -0.02347f, 0.00333f,  -0.01259f, -0.00465f, 0.02526f,  0.36286f,  -0.10324f,
  2.12780f,  -0.74584f, -1.05052f, 1.78467f,  -0.55065f, -0.03326f, 2.46781f,
  1.18349f,  0.96015f,  1.01696f,  1.10584f,  1.07263f,  1.11531f,  -1.06413f,
  0.32389f,  -1.87360f, -0.14435f, 1.77926f,  1.09966f,  -0.12680f, -0.61386f,
  -0.09724f, -0.33095f, 1.12122f,  1.00791f,  1.52416f,  1.35004f,  1.32657f,
  0.60950f,  -1.13538f, -0.38654f, 0.06473f,  2.10669f,  0.27734f,  -0.38359f,
  -1.91455f, -1.22676f, 0.05786f,  0.97432f,  2.19967f,  0.50457f,  0.78976f,
  0.95183f,  -0.32414f, 0.49437f,  -0.04506f, 0.18993f,  -0.07971f, 0.23889f,
  -0.09872f, -0.66036f, 0.05377f,  2.69638f,  -0.08259f, -0.69210f, -1.08296f,
  -1.96504f, -2.31947f, -0.80161f, -0.80456f, -1.35556f, -0.05323f, -4.42658f,
  -0.30732f, -0.12043f, 0.11126f,  0.10771f,  -0.14956f, -0.02218f, 0.41016f,
  1.16599f,  1.14629f,  1.12881f,  1.18676f,  1.24677f,  1.28695f,  1.11270f,
  0.08233f,  1.75440f,  0.49228f,  -0.34858f, -0.17032f, 0.29288f,  0.47175f,
  0.19055f,  -1.56413f,
};

static const float av1_tx_type_nn_bias_16x4_hor_layer0[16] = {
  -1.71227f, 0.47291f, -0.97536f, -0.66216f, 0.11729f,  -0.21451f,
  2.75281f,  0.04318f, 2.03965f,  0.14618f,  -0.70483f, -0.24517f,
  1.14048f,  0.33308f, -1.10886f, 0.41184f,
};

static const float av1_tx_type_nn_weights_16x4_hor_layer1[64] = {
  -1.17079f, 0.19096f,  -1.05753f, -0.30803f, -1.21680f, -0.67255f, 1.60115f,
  0.05972f,  1.44759f,  -0.04068f, -0.26331f, 0.31400f,  0.96923f,  0.33443f,
  -0.77215f, -0.91316f, -1.78928f, 0.21483f,  -1.24008f, -0.46190f, -0.12127f,
  -0.62144f, 1.37593f,  0.08373f,  1.56215f,  0.00279f,  -0.14556f, 0.38710f,
  0.96228f,  0.66433f,  -0.51798f, -0.80738f, -0.18539f, 0.19377f,  -1.03090f,
  -1.51044f, -0.59485f, -0.62589f, 1.90742f,  0.09078f,  1.49113f,  0.00205f,
  -0.15918f, 0.40827f,  1.08553f,  0.43431f,  0.33519f,  -1.12669f, -1.10274f,
  0.80004f,  -1.83599f, -0.53134f, 2.00515f,  -0.32670f, 1.37124f,  0.51136f,
  1.62563f,  0.24787f,  0.31757f,  0.81751f,  1.57262f,  0.83214f,  1.04661f,
  -0.43819f,
};

static const float av1_tx_type_nn_bias_16x4_hor_layer1[4] = {
  2.32575f,
  2.75703f,
  1.12304f,
  2.15567f,
};

static const NN_CONFIG av1_tx_type_nnconfig_16x4_hor = {
  8,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      16,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_16x4_hor_layer0,
    av1_tx_type_nn_weights_16x4_hor_layer1 },
  { av1_tx_type_nn_bias_16x4_hor_layer0, av1_tx_type_nn_bias_16x4_hor_layer1 }
};

static const float av1_tx_type_nn_weights_16x4_ver_layer0[32] = {
  0.26047f,  0.99930f,  1.16484f,  -0.28196f, -2.67483f, -0.21456f, -0.16854f,
  0.46375f,  1.47951f,  1.13735f,  1.12356f,  0.27385f,  0.50978f,  2.09967f,
  -1.47386f, 0.01950f,  -0.06362f, 0.26014f,  1.04544f,  -0.03099f, 0.07478f,
  -0.39701f, 0.05545f,  2.73633f,  -0.56305f, -0.02208f, -0.44517f, -0.00897f,
  -0.17967f, -0.96622f, 0.42635f,  -1.04784f,
};

static const float av1_tx_type_nn_bias_16x4_ver_layer0[8] = {
  -0.52088f, 0.52844f,  -1.03655f, -0.30974f,
  2.59952f,  -1.93604f, 0.00000f,  2.51787f,
};

static const float av1_tx_type_nn_weights_16x4_ver_layer1[32] = {
  0.10916f,  -0.21219f, -0.51340f, 0.69161f,  1.45988f,  -1.36942f, -0.40899f,
  1.05136f,  -0.08486f, 0.10008f,  -0.55304f, 0.88012f,  1.61177f,  -1.64507f,
  0.63428f,  1.15130f,  -0.17287f, -0.18592f, -0.01143f, 0.88293f,  1.73326f,
  -1.63624f, 0.09359f,  1.18393f,  0.26531f,  0.22378f,  0.15170f,  1.06965f,
  1.26814f,  -1.93873f, -0.00768f, 1.58309f,
};

static const float av1_tx_type_nn_bias_16x4_ver_layer1[4] = {
  2.34713f,
  1.68667f,
  1.25488f,
  1.69812f,
};

static const NN_CONFIG av1_tx_type_nnconfig_16x4_ver = {
  4,  // num_inputs
  4,  // num_outputs
  1,  // num_hidden_layers
  {
      8,
  },  // num_hidden_nodes
  { av1_tx_type_nn_weights_16x4_ver_layer0,
    av1_tx_type_nn_weights_16x4_ver_layer1 },
  { av1_tx_type_nn_bias_16x4_ver_layer0, av1_tx_type_nn_bias_16x4_ver_layer1 }
};
/******************************************************************************/

// Map tx_size to its corresponding neural net model for tx type prediction.
static const NN_CONFIG *av1_tx_type_nnconfig_map_hor[] = {
  &av1_tx_type_nnconfig_4x4_hor,   // 4x4 transform
  &av1_tx_type_nnconfig_8x8_hor,   // 8x8 transform
  &av1_tx_type_nnconfig_16x16,     // 16x16 transform
  NULL,                            // 32x32 transform
  NULL,                            // 64x64 transform
  &av1_tx_type_nnconfig_4x8_hor,   // 4x8 transform
  &av1_tx_type_nnconfig_8x4_hor,   // 8x4 transform
  &av1_tx_type_nnconfig_8x16_hor,  // 8x16 transform
  &av1_tx_type_nnconfig_16x8_hor,  // 16x8 transform
  NULL,                            // 16x32 transform
  NULL,                            // 32x16 transform
  NULL,                            // 32x64 transform
  NULL,                            // 64x32 transform
  &av1_tx_type_nnconfig_4x16_hor,  // 4x16 transform
  &av1_tx_type_nnconfig_16x4_hor,  // 16x4 transform
  NULL,                            // 8x32 transform
  NULL,                            // 32x8 transform
  NULL,                            // 16x64 transform
  NULL,                            // 64x16 transform
  NULL,                            // 4x32 transform
  NULL,                            // 32x4 transform
  NULL,                            // 8x64 transform
  NULL,                            // 64x8 transform
  NULL,                            // 4x64 transform
  NULL,                            // 64x4 transform
};

static const NN_CONFIG *av1_tx_type_nnconfig_map_ver[] = {
  &av1_tx_type_nnconfig_4x4_ver,   // 4x4 transform
  &av1_tx_type_nnconfig_8x8_ver,   // 8x8 transform
  &av1_tx_type_nnconfig_16x16,     // 16x16 transform
  NULL,                            // 32x32 transform
  NULL,                            // 64x64 transform
  &av1_tx_type_nnconfig_4x8_ver,   // 4x8 transform
  &av1_tx_type_nnconfig_8x4_ver,   // 8x4 transform
  &av1_tx_type_nnconfig_8x16_ver,  // 8x16 transform
  &av1_tx_type_nnconfig_16x8_ver,  // 16x8 transform
  NULL,                            // 16x32 transform
  NULL,                            // 32x16 transform
  NULL,                            // 32x64 transform
  NULL,                            // 64x32 transform
  &av1_tx_type_nnconfig_4x16_ver,  // 4x16 transform
  &av1_tx_type_nnconfig_16x4_ver,  // 16x4 transform
  NULL,                            // 8x32 transform
  NULL,                            // 32x8 transform
  NULL,                            // 16x64 transform
  NULL,                            // 64x16 transform
  NULL,                            // 4x32 transform
  NULL,                            // 32x4 transform
  NULL,                            // 8x64 transform
  NULL,                            // 64x8 transform
  NULL,                            // 4x64 transform
  NULL,                            // 64x4 transform
};
#endif  // CONFIG_NN_V2

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_TX_PRUNE_MODEL_WEIGHTS_H_
