/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include "config/av2_rtcd.h"

#include "av2/encoder/erp_ml.h"
#include "av2/encoder/erp_models.h"
#include "av2/encoder/ml.h"

#define MAKE_ERP_MODEL_SWITCH_CASE(bsize)           \
  case bsize:                                       \
    return is_hd ? av2_erp_rect_hd_##bsize##_tflite \
                 : av2_erp_rect_##bsize##_tflite;

#define MAKE_ERP_DNN_MODEL_SWITCH_CASE(bsize)         \
  case bsize:                                         \
    return is_hd ? &av2_erp_rect_hd_nn_config_##bsize \
                 : &av2_erp_rect_nn_config_##bsize;

#define MAKE_ERP_MEAN_SWITCH_CASE(bsize)                \
  case bsize:                                           \
    return is_hd ? av2_erp_rect_hd_feature_mean_##bsize \
                 : av2_erp_rect_feature_mean_##bsize;

#define MAKE_ERP_STD_SWITCH_CASE(bsize)                \
  case bsize:                                          \
    return is_hd ? av2_erp_rect_hd_feature_std_##bsize \
                 : av2_erp_rect_feature_std_##bsize;

static const NN_CONFIG *get_dnn_model(BLOCK_SIZE bsize, bool is_hd) {
  switch (bsize) {
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_128X128)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_128X64)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_64X128)

    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_64X64)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_64X32)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_32X64)

    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_32X32)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_32X16)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_16X32)

    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_16X16)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_16X8)
    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_8X16)

    MAKE_ERP_DNN_MODEL_SWITCH_CASE(BLOCK_8X8)

    default: assert(0 && "Invalid block size!\n"); return NULL;
  }
}

static const float *get_mean(BLOCK_SIZE bsize, bool is_hd) {
  switch (bsize) {
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_128X128)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_128X64)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_64X128)

    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_64X64)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_64X32)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_32X64)

    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_32X32)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_32X16)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_16X32)

    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_16X16)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_16X8)
    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_8X16)

    MAKE_ERP_MEAN_SWITCH_CASE(BLOCK_8X8)

    default: assert(0 && "Invalid block size!\n"); return NULL;
  }
}

static const float *get_std(BLOCK_SIZE bsize, bool is_hd) {
  switch (bsize) {
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_128X128)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_128X64)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_64X128)

    MAKE_ERP_STD_SWITCH_CASE(BLOCK_64X64)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_64X32)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_32X64)

    MAKE_ERP_STD_SWITCH_CASE(BLOCK_32X32)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_32X16)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_16X32)

    MAKE_ERP_STD_SWITCH_CASE(BLOCK_16X16)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_16X8)
    MAKE_ERP_STD_SWITCH_CASE(BLOCK_8X16)

    MAKE_ERP_STD_SWITCH_CASE(BLOCK_8X8)

    default: assert(0 && "Invalid block size!\n"); return NULL;
  }
}
#undef MAKE_ERP_MODEL_SWITCH_CASE

static inline void normalize(float *features_dst, const float *features_src,
                             const float *mean, const float *std,
                             size_t num_features) {
#define EPSILON 0.00001f
  for (size_t idx = 0; idx < num_features; idx++) {
    if (std[idx] <= EPSILON) {
      // Low variance. Assumes a constant
      features_dst[idx] = 0.0f;
    } else {
      features_dst[idx] = (features_src[idx] - mean[idx]) / std[idx];
    }
  }
#undef EPSILON
}

int av2_erp_prune_rect(BLOCK_SIZE bsize, bool is_hd, const float *features,
                       bool *prune_horz, bool *prune_vert) {
  // Prepare input.
  float input[19];
  const float *mean = get_mean(bsize, is_hd);
  const float *std = get_std(bsize, is_hd);
  normalize(input, features, mean, std, 19);

  // Call nn config
  float output[3];
  const NN_CONFIG *nn_config = get_dnn_model(bsize, is_hd);
  av2_nn_predict(input, nn_config, 1, output);

  float probs[3];
  av2_nn_softmax(output, probs, 3);

  static const float threshes[2][5] = {
    // Non-hd
    {
        // 128, 64, 32, 16, 8
        0.00889f,
        0.00268f,
        0.01480f,
        0.03531f,
        0.04103f,
    },
    // HD
    {
        // 128, 64, 32, 16, 8
        0.01911f,
        0.00327f,
        0.00520f,
        0.01669f,
        0.00176f,
    },
  };

  float thresh = 0.0f;
  switch (bsize) {
    case BLOCK_128X128:
    case BLOCK_128X64:
    case BLOCK_64X128: thresh = threshes[is_hd][0]; break;
    case BLOCK_64X64:
    case BLOCK_64X32:
    case BLOCK_32X64: thresh = threshes[is_hd][1]; break;
    case BLOCK_32X32:
    case BLOCK_32X16:
    case BLOCK_16X32: thresh = threshes[is_hd][2]; break;
    case BLOCK_16X16:
    case BLOCK_16X8:
    case BLOCK_8X16: thresh = threshes[is_hd][3]; break;
    case BLOCK_8X8: thresh = threshes[is_hd][4]; break;
    default:
      assert(0 && "Unexpected block size in erp pruning model!\n");
      thresh = 0.0f;
  }

  if (probs[1] < thresh) {
    *prune_horz = true;
  }
  if (probs[2] < thresh) {
    *prune_vert = true;
  }

  return 1;
}
