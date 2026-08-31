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

#include <float.h>

#include "av2/encoder/context_tree.h"
#include "av2/encoder/encodeframe_utils.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_ports/system_state.h"

#include "av2/common/enums.h"
#include "av2/common/reconinter.h"
#include "av2/common/reconintra.h"

#include "av2/encoder/cnn.h"
#include "av2/encoder/partition_model_weights.h"
#include "av2/encoder/partition_cnn_weights.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/reconinter_enc.h"

#include "av2/encoder/motion_search_facade.h"
#include "av2/encoder/partition_search.h"
#include "av2/encoder/rdopt.h"
#include "av2/common/idct.h"
#include "av2/encoder/hybrid_fwd_txfm.h"

#if CONFIG_ML_PART_SPLIT
#include "av2/encoder/part_split_prune_tflite.h"
#include "av2/encoder/partition_ml.h"
#endif  // CONFIG_ML_PART_SPLIT

#include "av2/encoder/partition_sms.h"

static AVM_INLINE void simple_motion_search_prune_part_features(
    AV2_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, float *features,
    int features_to_get);

static INLINE int convert_bsize_to_idx(BLOCK_SIZE bsize) {
  switch (bsize) {
    case BLOCK_128X128: return 0;
    case BLOCK_64X64: return 1;
    case BLOCK_32X32: return 2;
    case BLOCK_16X16: return 3;
    case BLOCK_8X8: return 4;
    default: assert(0 && "Invalid bsize"); return -1;
  }
}

// TODO(chiyotsai@google.com): This is very much a work in progress. We still
// need to the following:
//   -- add support for hdres
//   -- add support for pruning rectangular partitions
//   -- use reconstructed pixels instead of source pixels for padding
//   -- use chroma pixels in addition to luma pixels
void av2_intra_mode_cnn_partition(const AV2_COMMON *const cm, MACROBLOCK *x,
                                  BLOCK_SIZE bsize, int quad_tree_idx,
                                  PartitionSearchState *partition_search_state,
                                  bool *do_square_split) {
  assert(cm->sb_size >= BLOCK_64X64 && "Invalid sb_size for intra_cnn!");
  const int bsize_idx = convert_bsize_to_idx(bsize);

  if (bsize == BLOCK_128X128) {
    return;
  }

  PartitionSearchInfo *part_info = &x->part_search_info;

  // Precompute the CNN part and cache the result in MACROBLOCK
  if (bsize == BLOCK_64X64 && !part_info->cnn_output_valid) {
    avm_clear_system_state();
    const CNN_CONFIG *cnn_config = &av2_intra_mode_cnn_partition_cnn_config;

    // Prepare the output
    const CNN_THREAD_DATA thread_data = { .num_workers = 1, .workers = NULL };
    const int num_outputs = 4;
    const int output_dims[4] = { 1, 2, 4, 8 };
    const int out_chs[4] = { CNN_BRANCH_0_OUT_CH, CNN_BRANCH_1_OUT_CH,
                             CNN_BRANCH_2_OUT_CH, CNN_BRANCH_3_OUT_CH };
    float *output_buffer[CNN_TOT_OUT_CH];

    float **cur_output_buf = output_buffer;
    float *curr_buf_ptr = part_info->cnn_buffer;
    for (int output_idx = 0; output_idx < num_outputs; output_idx++) {
      const int num_chs = out_chs[output_idx];
      const int ch_size = output_dims[output_idx] * output_dims[output_idx];
      for (int ch = 0; ch < num_chs; ch++) {
        cur_output_buf[ch] = curr_buf_ptr;
        curr_buf_ptr += ch_size;
      }
      cur_output_buf += num_chs;
    }

    CNN_MULTI_OUT output = {
      .num_outputs = 4,
      .output_channels = out_chs,
      .output_strides = output_dims,
      .output_buffer = output_buffer,
    };

    // Prepare the input
    const MACROBLOCKD *xd = &x->e_mbd;
    const int bit_depth = xd->bd;
    const int dc_q =
        av2_dc_quant_QTX(x->qindex, 0, cm->seq_params.base_y_dc_delta_q,
                         bit_depth) >>
        (bit_depth - 8);
    part_info->log_q = logf(1.0f + (float)((int64_t)dc_q * (int64_t)dc_q) /
                                       (256 << (2 * QUANT_TABLE_BITS)));
    part_info->log_q =
        (part_info->log_q - av2_intra_mode_cnn_partition_mean[0]) /
        av2_intra_mode_cnn_partition_std[0];

    const int width = 65, height = 65,
              stride = x->plane[AVM_PLANE_Y].src.stride;

    uint16_t *image[1] = { x->plane[AVM_PLANE_Y].src.buf - stride - 1 };

    av2_cnn_predict_img_multi_out_highbd(image, width, height, stride,
                                         cnn_config, &thread_data, bit_depth,
                                         &output);

    part_info->cnn_output_valid = 1;
  }

  if (!part_info->cnn_output_valid) {
    return;
  }

  const NN_CONFIG *dnn_configs[5] = {
    NULL,
    &av2_intra_mode_cnn_partition_branch_0_dnn_config,
    &av2_intra_mode_cnn_partition_branch_1_dnn_config,
    &av2_intra_mode_cnn_partition_branch_2_dnn_config,
    &av2_intra_mode_cnn_partition_branch_3_dnn_config,
  };

  const NN_CONFIG *dnn_config = dnn_configs[bsize_idx];

  avm_clear_system_state();
  float dnn_features[100];
  float logits[4] = { 0.0f };

  const float *branch_0 = part_info->cnn_buffer;
  const float *branch_1 = branch_0 + CNN_BRANCH_0_OUT_SIZE;
  const float *branch_2 = branch_1 + CNN_BRANCH_1_OUT_SIZE;
  const float *branch_3 = branch_2 + CNN_BRANCH_2_OUT_SIZE;

  if (bsize == BLOCK_64X64) {
    int f_idx = 0;
    for (int ch_idx = 0; ch_idx < CNN_BRANCH_0_OUT_CH; ch_idx++) {
      dnn_features[f_idx++] = branch_0[ch_idx];
    }

    const int spa_stride = 2 * 2;
    for (int lin_idx = 0; lin_idx < spa_stride; lin_idx++) {
      for (int ch_idx = 0; ch_idx < CNN_BRANCH_1_OUT_CH; ch_idx++) {
        dnn_features[f_idx++] = branch_1[lin_idx + ch_idx * spa_stride];
      }
    }
    dnn_features[f_idx++] = part_info->log_q;
  } else if (bsize == BLOCK_32X32) {
    int f_idx = 0;
    for (int idx = 0; idx < CNN_BRANCH_0_OUT_CH; idx++) {
      dnn_features[f_idx++] = branch_0[idx];
    }

    const int curr_lin_idx = quad_to_linear_1[quad_tree_idx - 1];
    const int spa_stride = 2 * 2;
    for (int ch_idx = 0; ch_idx < CNN_BRANCH_1_OUT_CH; ch_idx++) {
      dnn_features[f_idx++] = branch_1[curr_lin_idx + ch_idx * spa_stride];
    }
    dnn_features[f_idx++] = part_info->log_q;
  } else if (bsize == BLOCK_16X16) {
    int f_idx = 0;
    const int prev_quad_idx = (quad_tree_idx - 1) / 4;
    const int prev_lin_idx = quad_to_linear_1[prev_quad_idx - 1];
    const int prev_spa_stride = 2 * 2;
    for (int ch_idx = 0; ch_idx < CNN_BRANCH_1_OUT_CH; ch_idx++) {
      dnn_features[f_idx++] = branch_1[prev_lin_idx + ch_idx * prev_spa_stride];
    }

    const int curr_lin_idx = quad_to_linear_2[quad_tree_idx - 5];
    const int spa_stride = 4 * 4;
    for (int ch_idx = 0; ch_idx < CNN_BRANCH_2_OUT_CH; ch_idx++) {
      dnn_features[f_idx++] = branch_2[curr_lin_idx + ch_idx * spa_stride];
    }
    dnn_features[f_idx++] = part_info->log_q;
  } else if (bsize == BLOCK_8X8) {
    int f_idx = 0;
    const int prev_quad_idx = (quad_tree_idx - 1) / 4;
    const int prev_lin_idx = quad_to_linear_2[prev_quad_idx - 5];
    const int prev_spa_stride = 4 * 4;
    for (int ch_idx = 0; ch_idx < CNN_BRANCH_2_OUT_CH; ch_idx++) {
      dnn_features[f_idx++] = branch_2[prev_lin_idx + ch_idx * prev_spa_stride];
    }

    const int curr_lin_idx = quad_to_linear_3[quad_tree_idx - 21];
    const int spa_stride = 8 * 8;
    for (int ch_idx = 0; ch_idx < CNN_BRANCH_3_OUT_CH; ch_idx++) {
      dnn_features[f_idx++] = branch_3[curr_lin_idx + ch_idx * spa_stride];
    }
    dnn_features[f_idx++] = part_info->log_q;
  } else {
    assert(0 && "Invalid bsize in intra_cnn partition");
  }

  // Make decision
  av2_nn_predict(dnn_features, dnn_config, 1, logits);
  avm_clear_system_state();

  const int is_720p_or_larger = AVMMIN(cm->width, cm->height) >= 720;
  const int is_480p_or_larger = AVMMIN(cm->width, cm->height) >= 480;
  float split_only_thresh = 100.0f, no_split_thresh = -100.0f;
  if (is_720p_or_larger) {
    split_only_thresh =
        av2_intra_mode_cnn_partition_split_thresh_hdres[bsize_idx];
    no_split_thresh =
        av2_intra_mode_cnn_partition_no_split_thresh_hdres[bsize_idx];
  } else if (is_480p_or_larger) {
    split_only_thresh =
        av2_intra_mode_cnn_partition_split_thresh_midres[bsize_idx];
    no_split_thresh =
        av2_intra_mode_cnn_partition_no_split_thresh_midres[bsize_idx];
  } else {
    split_only_thresh =
        av2_intra_mode_cnn_partition_split_thresh_lowres[bsize_idx];
    no_split_thresh =
        av2_intra_mode_cnn_partition_no_split_thresh_lowres[bsize_idx];
  }

  if (logits[0] > split_only_thresh) {
    partition_search_state->partition_allowed[PARTITION_NONE] = false;
    partition_search_state->partition_allowed[PARTITION_HORZ] = false;
    partition_search_state->partition_allowed[PARTITION_VERT] = false;
    partition_search_state->do_rectangular_split = false;
  }

  if (logits[0] < no_split_thresh) {
    *do_square_split = false;
  }
}

void av2_simple_motion_search_based_split(
    AV2_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize,
    PartitionSearchState *partition_search_state, bool *do_square_split) {
  avm_clear_system_state();

  const AV2_COMMON *const cm = &cpi->common;
  const int bsize_idx = convert_bsize_to_idx(bsize);
  const int is_720p_or_larger = AVMMIN(cm->width, cm->height) >= 720;
  const int is_480p_or_larger = AVMMIN(cm->width, cm->height) >= 480;
  // res_idx is 0 for res < 480p, 1 for 480p, 2 for 720p+
  const int res_idx = is_480p_or_larger + is_720p_or_larger;

  assert(bsize_idx >= 0 && bsize_idx <= 4 &&
         "Invalid bsize in simple_motion_search_based_split");

  const float *ml_mean = av2_simple_motion_search_split_mean[bsize_idx];
  const float *ml_std = av2_simple_motion_search_split_std[bsize_idx];
  const NN_CONFIG *nn_config =
      av2_simple_motion_search_split_nn_config[bsize_idx];
  const int agg = cpi->sf.part_sf.simple_motion_search_prune_agg;

  const float split_only_thresh =
      av2_simple_motion_search_split_thresh[agg][res_idx][bsize_idx];
  const float no_split_thresh =
      av2_simple_motion_search_no_split_thresh[agg][res_idx][bsize_idx];

  float features[FEATURE_SIZE_SMS_SPLIT] = { 0.0f };
  simple_motion_search_prune_part_features(cpi, x, sms_tree, mi_row, mi_col,
                                           bsize, features,
                                           FEATURE_SMS_SPLIT_MODEL_FLAG);
  for (int idx = 0; idx < FEATURE_SIZE_SMS_SPLIT; idx++) {
    features[idx] = (features[idx] - ml_mean[idx]) / ml_std[idx];
  }

  float score = 0.0f;

  av2_nn_predict(features, nn_config, 1, &score);
  avm_clear_system_state();

  if (score > split_only_thresh) {
    partition_search_state->partition_allowed[PARTITION_NONE] = false;
  }

  if (cpi->sf.part_sf.simple_motion_search_split >= 2 &&
      score < no_split_thresh) {
    *do_square_split = false;
  }
}

// Given a list of ref frames in refs, performs simple_motion_search on each of
// the refs and returns the ref with the smallest sse. Returns -1 if none of the
// ref in the list is available. Also stores the best sse and var in best_sse,
// best_var, respectively. If save_mv is 0, don't update mv_ref_fulls in
// sms_tree. If save_mv is 1, update mv_ref_fulls under sms_tree and the
// subtrees.
static int simple_motion_search_get_best_ref(
    AV2_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, const int *const refs,
    int num_refs, int use_subpixel, int save_mv, unsigned int *best_sse,
    unsigned int *best_var) {
  const AV2_COMMON *const cm = &cpi->common;
  int best_ref = -1;

  if (mi_col >= cm->mi_params.mi_cols || mi_row >= cm->mi_params.mi_rows) {
    // If the whole block is outside of the image, set the var and sse to 0.
    *best_var = 0;
    *best_sse = 0;

    return best_ref;
  }

  // Otherwise do loop through the reference frames and find the one with the
  // minimum SSE
  const MACROBLOCKD *xd = &x->e_mbd;

  const int num_planes = 1;

  *best_sse = INT_MAX;

  for (int ref_idx = 0; ref_idx < num_refs; ref_idx++) {
    const int ref = refs[ref_idx];

    if (ref == INVALID_IDX) continue;
    if (cm->ref_frame_flags & (1 << ref)) {
      const FULLPEL_MV *start_mvs = sms_tree->start_mvs;
      unsigned int curr_sse = 0, curr_var = 0;
      int_mv best_mv =
          av2_simple_motion_search(cpi, x, mi_row, mi_col, bsize, ref,
                                   start_mvs[ref], num_planes, use_subpixel);
      curr_var = cpi->fn_ptr[bsize].vf(
          x->plane[0].src.buf, x->plane[0].src.stride, xd->plane[0].dst.buf,
          xd->plane[0].dst.stride, &curr_sse);
      if (curr_sse < *best_sse) {
        *best_sse = curr_sse;
        *best_var = curr_var;
        best_ref = ref;
      }

      if (save_mv) {
        sms_tree->start_mvs[ref].row = best_mv.as_mv.row / 8;
        sms_tree->start_mvs[ref].col = best_mv.as_mv.col / 8;

        if (bsize >= BLOCK_8X8) {
          for (int r_idx = 0; r_idx < SUB_PARTITIONS_SPLIT; r_idx++) {
            // Propagate the new motion vectors to a lower level
            SIMPLE_MOTION_DATA_TREE *sub_tree = sms_tree->split[r_idx];
            if (sub_tree) {
              sub_tree->start_mvs[ref] = sms_tree->start_mvs[ref];
            }
          }
        }
      }
    }
  }

  return best_ref;
}

// Collects features using simple_motion_search and store them in features. The
// features are also cached in SIMPLE_MOTION_DATA_TREE. By default, the features
// collected are the sse and var from the subblocks flagged by features_to_get.
// Furthermore, if features is not NULL, then 7 more features are appended to
// the end of features:
//  - log(1.0 + dc_q ** 2)
//  - whether an above macroblock exists
//  - width of above macroblock
//  - height of above macroblock
//  - whether a left marcoblock exists
//  - width of left macroblock
//  - height of left macroblock
static AVM_INLINE void simple_motion_search_prune_part_features(
    AV2_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, float *features,
    int features_to_get) {
  const int w_mi = mi_size_wide[bsize];
  const int h_mi = mi_size_high[bsize];
  assert(mi_size_wide[bsize] == mi_size_high[bsize]);
  // Setting up motion search
  int ref_list[1];
  ref_list[0] = get_closest_pastcur_ref_or_ref0(&cpi->common);

  const int num_refs = 1;
  const int use_subpixel = 1;

  // Doing whole block first to update the mv
  if (!sms_tree->sms_none_valid && features_to_get & FEATURE_SMS_NONE_FLAG) {
    simple_motion_search_get_best_ref(cpi, x, sms_tree, mi_row, mi_col, bsize,
                                      ref_list, num_refs, use_subpixel, 1,
                                      &sms_tree->sms_none_feat[0],
                                      &sms_tree->sms_none_feat[1]);
    sms_tree->sms_none_valid = 1;
  }

  // Split subblocks
  if (features_to_get & FEATURE_SMS_SPLIT_FLAG) {
    const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
    for (int r_idx = 0; r_idx < SUB_PARTITIONS_SPLIT; r_idx++) {
      const int sub_mi_col = mi_col + (r_idx & 1) * w_mi / 2;
      const int sub_mi_row = mi_row + (r_idx >> 1) * h_mi / 2;
      SIMPLE_MOTION_DATA_TREE *sub_tree = sms_tree->split[r_idx];

      if (!sub_tree->sms_none_valid) {
        simple_motion_search_get_best_ref(
            cpi, x, sub_tree, sub_mi_row, sub_mi_col, subsize, ref_list,
            num_refs, use_subpixel, 1, &sub_tree->sms_none_feat[0],
            &sub_tree->sms_none_feat[1]);
        sub_tree->sms_none_valid = 1;
      }
    }
  }

  // Rectangular subblocks
  if (!sms_tree->sms_rect_valid && features_to_get & FEATURE_SMS_RECT_FLAG) {
    // Horz subblock
    BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_HORZ);
    for (int r_idx = 0; r_idx < SUB_PARTITIONS_RECT; r_idx++) {
      const int sub_mi_col = mi_col + 0;
      const int sub_mi_row = mi_row + r_idx * h_mi / 2;

      simple_motion_search_get_best_ref(
          cpi, x, sms_tree, sub_mi_row, sub_mi_col, subsize, ref_list, num_refs,
          use_subpixel, 0, &sms_tree->sms_rect_feat[2 * r_idx],
          &sms_tree->sms_rect_feat[2 * r_idx + 1]);
    }

    // Vert subblock
    subsize = get_partition_subsize(bsize, PARTITION_VERT);
    for (int r_idx = 0; r_idx < SUB_PARTITIONS_RECT; r_idx++) {
      const int sub_mi_col = mi_col + r_idx * w_mi / 2;
      const int sub_mi_row = mi_row + 0;

      simple_motion_search_get_best_ref(
          cpi, x, sms_tree, sub_mi_row, sub_mi_col, subsize, ref_list, num_refs,
          use_subpixel, 0, &sms_tree->sms_rect_feat[4 + 2 * r_idx],
          &sms_tree->sms_rect_feat[4 + 2 * r_idx + 1]);
    }
    sms_tree->sms_rect_valid = 1;
  }

  if (!features) return;

  avm_clear_system_state();
  int f_idx = 0;
  if (features_to_get & FEATURE_SMS_NONE_FLAG) {
    for (int sub_idx = 0; sub_idx < 2; sub_idx++) {
      features[f_idx++] = logf(1.0f + sms_tree->sms_none_feat[sub_idx]);
    }
  }

  if (features_to_get & FEATURE_SMS_SPLIT_FLAG) {
    for (int sub_idx = 0; sub_idx < SUB_PARTITIONS_SPLIT; sub_idx++) {
      SIMPLE_MOTION_DATA_TREE *sub_tree = sms_tree->split[sub_idx];
      features[f_idx++] = logf(1.0f + sub_tree->sms_none_feat[0]);
      features[f_idx++] = logf(1.0f + sub_tree->sms_none_feat[1]);
    }
  }

  if (features_to_get & FEATURE_SMS_RECT_FLAG) {
    for (int sub_idx = 0; sub_idx < 8; sub_idx++) {
      features[f_idx++] = logf(1.0f + sms_tree->sms_rect_feat[sub_idx]);
    }
  }
  avm_clear_system_state();

  const MACROBLOCKD *xd = &x->e_mbd;
  set_offsets_for_motion_search(cpi, x, mi_row, mi_col, bsize);

  // Q_INDEX
  const int dc_q =
      av2_dc_quant_QTX(x->qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
                       xd->bd) >>
      (xd->bd - 8);
  features[f_idx++] = logf(1.0f + (float)((int64_t)dc_q * (int64_t)dc_q) /
                                      (256 << (2 * QUANT_TABLE_BITS)));

  // Neighbor stuff
  const int has_above = !!xd->above_mbmi;
  const int has_left = !!xd->left_mbmi;
  const BLOCK_SIZE above_bsize =
      has_above ? xd->above_mbmi->sb_type[xd->tree_type == CHROMA_PART] : bsize;
  const BLOCK_SIZE left_bsize =
      has_left ? xd->left_mbmi->sb_type[xd->tree_type == CHROMA_PART] : bsize;
  features[f_idx++] = (float)has_above;
  features[f_idx++] = (float)mi_size_wide_log2[above_bsize];
  features[f_idx++] = (float)mi_size_high_log2[above_bsize];
  features[f_idx++] = (float)has_left;
  features[f_idx++] = (float)mi_size_wide_log2[left_bsize];
  features[f_idx++] = (float)mi_size_high_log2[left_bsize];
}

void av2_simple_motion_search_prune_rect(
    AV2_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize,
    PartitionSearchState *partition_search_state) {
  // TODO(urvang): Need to change for uneven 4-way partition support.
  assert(0 && "Not implemented");
  avm_clear_system_state();
  const AV2_COMMON *const cm = &cpi->common;
  const int bsize_idx = convert_bsize_to_idx(bsize);
  const int is_720p_or_larger = AVMMIN(cm->width, cm->height) >= 720;
  const int is_480p_or_larger = AVMMIN(cm->width, cm->height) >= 480;
  // res_idx is 0 for lowres, 1 for 48p, 2 for 720p+
  const int res_idx = is_480p_or_larger + is_720p_or_larger;

  // Get model parameters
  const NN_CONFIG *nn_config =
      av2_simple_motion_search_prune_rect_nn_config[bsize_idx];
  const float *ml_mean = av2_simple_motion_search_prune_rect_mean[bsize_idx],
              *ml_std = av2_simple_motion_search_prune_rect_std[bsize_idx];

  const int agg = cpi->sf.part_sf.simple_motion_search_prune_agg;
  const float prune_thresh =
      av2_simple_motion_search_prune_rect_thresh[agg][res_idx][bsize_idx];

  // If there is no valid threshold, return immediately.
  if (!nn_config || prune_thresh == 0.0f) {
    return;
  }

  // Get features
  float features[FEATURE_SIZE_SMS_PRUNE_PART] = { 0.0f };
  simple_motion_search_prune_part_features(cpi, x, sms_tree, mi_row, mi_col,
                                           bsize, features,
                                           FEATURE_SMS_PRUNE_PART_FLAG);
  for (int f_idx = 0; f_idx < FEATURE_SIZE_SMS_PRUNE_PART; f_idx++) {
    features[f_idx] = (features[f_idx] - ml_mean[f_idx]) / ml_std[f_idx];
  }

  // Get probabilities
  float scores[EXT_PARTITION_TYPES] = { 0.0f },
        probs[EXT_PARTITION_TYPES] = { 0.0f };
  const int num_classes = (bsize == BLOCK_128X128 || bsize == BLOCK_8X8)
                              ? PARTITION_TYPES
                              : EXT_PARTITION_TYPES;

  av2_nn_predict(features, nn_config, 1, scores);
  avm_clear_system_state();

  av2_nn_softmax(scores, probs, num_classes);

  // Determine if we should prune rectangular partitions.
  if (cpi->sf.part_sf.simple_motion_search_prune_rect &&
      !frame_is_intra_only(cm) &&
      (partition_search_state->partition_allowed[PARTITION_HORZ] ||
       partition_search_state->partition_allowed[PARTITION_VERT]) &&
      bsize >= BLOCK_8X8) {
    partition_search_state->prune_partition[PARTITION_HORZ] =
        probs[PARTITION_HORZ] <= prune_thresh;
    partition_search_state->prune_partition[PARTITION_VERT] =
        probs[PARTITION_VERT] <= prune_thresh;
  }
}

// Early terminates PARTITION_NONE using simple_motion_search features and the
// rate, distortion, and rdcost of PARTITION_NONE. This is only called when:
//  - The frame is a show frame
//  - The frame is not intra only
//  - The current bsize is > BLOCK_8X8
//  - blk_row + blk_height/2 < total_rows and blk_col + blk_width/2 < total_cols
void av2_simple_motion_search_early_term_none(
    AV2_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, const RD_STATS *none_rdc,
    bool *early_terminate) {
  // TODO(chiyotsai@google.com): There are other features we can extract from
  // PARTITION_NONE. Play with this later.
  float features[FEATURE_SIZE_SMS_TERM_NONE] = { 0.0f };
  simple_motion_search_prune_part_features(cpi, x, sms_tree, mi_row, mi_col,
                                           bsize, features,
                                           FEATURE_SMS_PRUNE_PART_FLAG);
  int f_idx = FEATURE_SIZE_SMS_PRUNE_PART;

  features[f_idx++] = logf(1.0f + (float)none_rdc->rate);
  features[f_idx++] = logf(1.0f + (float)none_rdc->dist);
  features[f_idx++] = logf(1.0f + (float)none_rdc->rdcost);

  assert(f_idx == FEATURE_SIZE_SMS_TERM_NONE);

  const float *ml_mean = NULL;
  const float *ml_std = NULL;
  const float *ml_model = NULL;

  if (bsize == BLOCK_128X128) {
    ml_mean = av2_simple_motion_search_term_none_mean_128;
    ml_std = av2_simple_motion_search_term_none_std_128;
    ml_model = av2_simple_motion_search_term_none_model_128;
  } else if (bsize == BLOCK_64X64) {
    ml_mean = av2_simple_motion_search_term_none_mean_64;
    ml_std = av2_simple_motion_search_term_none_std_64;
    ml_model = av2_simple_motion_search_term_none_model_64;
  } else if (bsize == BLOCK_32X32) {
    ml_mean = av2_simple_motion_search_term_none_mean_32;
    ml_std = av2_simple_motion_search_term_none_std_32;
    ml_model = av2_simple_motion_search_term_none_model_32;
  } else if (bsize == BLOCK_16X16) {
    ml_mean = av2_simple_motion_search_term_none_mean_16;
    ml_std = av2_simple_motion_search_term_none_std_16;
    ml_model = av2_simple_motion_search_term_none_model_16;
  } else if (bsize == BLOCK_256X256) {
    return;
  } else {
    assert(0 && "Unexpected block size in simple_motion_term_none");
  }

  if (ml_model) {
    float score = 0.0f;
    for (f_idx = 0; f_idx < FEATURE_SIZE_SMS_TERM_NONE; f_idx++) {
      score +=
          ml_model[f_idx] * (features[f_idx] - ml_mean[f_idx]) / ml_std[f_idx];
    }
    score += ml_model[FEATURE_SIZE_SMS_TERM_NONE];

    if (score >= 0.0f) {
      *early_terminate = true;
    }
  }
}

void av2_get_max_min_partition_features(AV2_COMP *const cpi, MACROBLOCK *x,
                                        int mi_row, int mi_col,
                                        float *features) {
  AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  const BLOCK_SIZE sb_size = cm->sb_size;

  assert(sb_size == BLOCK_128X128);

  int f_idx = 0;
  const int dc_q = av2_dc_quant_QTX(x->qindex, 0,
                                    cm->seq_params.base_y_dc_delta_q, xd->bd) >>
                   (xd->bd - 8);
  avm_clear_system_state();
  const float log_q_sq = logf(1.0f + (float)((int64_t)dc_q * (int64_t)dc_q) /
                                         (256 << (2 * QUANT_TABLE_BITS)));

  // Perform full-pixel single motion search in Y plane of 16x16 mbs in the sb
  float sum_mv_row_sq = 0;
  float sum_mv_row = 0;
  float min_abs_mv_row = FLT_MAX;
  float max_abs_mv_row = 0;

  float sum_mv_col_sq = 0;
  float sum_mv_col = 0;
  float min_abs_mv_col = FLT_MAX;
  float max_abs_mv_col = 0;

  float sum_log_sse_sq = 0;
  float sum_log_sse = 0;
  float min_log_sse = FLT_MAX;
  float max_log_sse = 0;

  const BLOCK_SIZE mb_size = BLOCK_16X16;
  const int mb_rows = block_size_high[sb_size] / block_size_high[mb_size];
  const int mb_cols = block_size_wide[sb_size] / block_size_wide[mb_size];
  const int mb_in_mi_size_high_log2 = mi_size_high_log2[mb_size];
  const int mb_in_mi_size_wide_log2 = mi_size_wide_log2[mb_size];

  for (int mb_row = 0; mb_row < mb_rows; mb_row++)
    for (int mb_col = 0; mb_col < mb_cols; mb_col++) {
      const int this_mi_row = mi_row + (mb_row << mb_in_mi_size_high_log2);
      const int this_mi_col = mi_col + (mb_col << mb_in_mi_size_wide_log2);
      unsigned int sse = 0;
      unsigned int var = 0;
      const FULLPEL_MV start_mv = kZeroFullMv;
      int_mv best_mv = av2_simple_motion_sse_var(
          cpi, x, this_mi_row, this_mi_col, mb_size, start_mv, 0, &sse, &var);

      avm_clear_system_state();
      const float mv_row = (float)(best_mv.as_mv.row / 8);
      const float mv_col = (float)(best_mv.as_mv.col / 8);
      const float log_sse = logf(1.0f + (float)sse);
      const float abs_mv_row = fabsf(mv_row);
      const float abs_mv_col = fabsf(mv_col);

      sum_mv_row_sq += mv_row * mv_row;
      sum_mv_row += mv_row;
      sum_mv_col_sq += mv_col * mv_col;
      sum_mv_col += mv_col;

      if (abs_mv_row < min_abs_mv_row) min_abs_mv_row = abs_mv_row;
      if (abs_mv_row > max_abs_mv_row) max_abs_mv_row = abs_mv_row;
      if (abs_mv_col < min_abs_mv_col) min_abs_mv_col = abs_mv_col;
      if (abs_mv_col > max_abs_mv_col) max_abs_mv_col = abs_mv_col;

      sum_log_sse_sq += log_sse * log_sse;
      sum_log_sse += log_sse;
      if (log_sse < min_log_sse) min_log_sse = log_sse;
      if (log_sse > max_log_sse) max_log_sse = log_sse;
    }
  avm_clear_system_state();
  const float avg_mv_row = sum_mv_row / 64.0f;
  const float var_mv_row = sum_mv_row_sq / 64.0f - avg_mv_row * avg_mv_row;

  const float avg_mv_col = sum_mv_col / 64.0f;
  const float var_mv_col = sum_mv_col_sq / 64.0f - avg_mv_col * avg_mv_col;

  const float avg_log_sse = sum_log_sse / 64.0f;
  const float var_log_sse = sum_log_sse_sq / 64.0f - avg_log_sse * avg_log_sse;

  features[f_idx++] = avg_log_sse;
  features[f_idx++] = avg_mv_col;
  features[f_idx++] = avg_mv_row;
  features[f_idx++] = log_q_sq;
  features[f_idx++] = max_abs_mv_col;
  features[f_idx++] = max_abs_mv_row;
  features[f_idx++] = max_log_sse;
  features[f_idx++] = min_abs_mv_col;
  features[f_idx++] = min_abs_mv_row;
  features[f_idx++] = min_log_sse;
  features[f_idx++] = var_log_sse;
  features[f_idx++] = var_mv_col;
  features[f_idx++] = var_mv_row;

  assert(f_idx == FEATURE_SIZE_MAX_MIN_PART_PRED);
}

BLOCK_SIZE av2_predict_max_partition(const AV2_COMP *const cpi,
                                     const MACROBLOCK *const x,
                                     const float *features) {
  float scores[MAX_NUM_CLASSES_MAX_MIN_PART_PRED] = { 0.0f },
        probs[MAX_NUM_CLASSES_MAX_MIN_PART_PRED] = { 0.0f };
  const NN_CONFIG *nn_config = &av2_max_part_pred_nn_config;

  assert(cpi->sf.part_sf.auto_max_partition_based_on_simple_motion !=
         NOT_IN_USE);

  avm_clear_system_state();
  av2_nn_predict(features, nn_config, 1, scores);
  av2_nn_softmax(scores, probs, MAX_NUM_CLASSES_MAX_MIN_PART_PRED);

  int result = MAX_NUM_CLASSES_MAX_MIN_PART_PRED - 1;
  if (cpi->sf.part_sf.auto_max_partition_based_on_simple_motion ==
      DIRECT_PRED) {
    result = 0;
    float max_prob = probs[0];
    for (int i = 1; i < MAX_NUM_CLASSES_MAX_MIN_PART_PRED; ++i) {
      if (probs[i] > max_prob) {
        max_prob = probs[i];
        result = i;
      }
    }
  } else if (cpi->sf.part_sf.auto_max_partition_based_on_simple_motion ==
             RELAXED_PRED) {
    for (result = MAX_NUM_CLASSES_MAX_MIN_PART_PRED - 1; result >= 0;
         --result) {
      if (result < MAX_NUM_CLASSES_MAX_MIN_PART_PRED - 1) {
        probs[result] += probs[result + 1];
      }
      if (probs[result] > 0.2) break;
    }
  } else if (cpi->sf.part_sf.auto_max_partition_based_on_simple_motion ==
             ADAPT_PRED) {
    const BLOCK_SIZE sb_size = cpi->common.sb_size;
    const MACROBLOCKD *const xd = &x->e_mbd;
    // TODO(debargha): x->source_variance is unavailable at this point,
    // so compute. The redundant recomputation later can be removed.
    const unsigned int source_variance = av2_high_get_sby_perpixel_variance(
        cpi, &x->plane[0].src, sb_size, xd->bd);
    if (source_variance > 16) {
      const double thresh = source_variance < 128 ? 0.05 : 0.1;
      for (result = MAX_NUM_CLASSES_MAX_MIN_PART_PRED - 1; result >= 0;
           --result) {
        if (result < MAX_NUM_CLASSES_MAX_MIN_PART_PRED - 1) {
          probs[result] += probs[result + 1];
        }
        if (probs[result] > thresh) break;
      }
    }
  }

  return (BLOCK_SIZE)((result + 2) * 3);
}

#define FEATURES 4
int av2_ml_predict_breakout(const AV2_COMP *const cpi, BLOCK_SIZE bsize,
                            const MACROBLOCK *const x,
                            const RD_STATS *const rd_stats,
                            unsigned int pb_source_variance) {
  const NN_CONFIG *nn_config = NULL;
  int thresh = 0;
  switch (bsize) {
    case BLOCK_8X8:
      nn_config = &av2_partition_breakout_nnconfig_8;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[0];
      break;
    case BLOCK_16X16:
      nn_config = &av2_partition_breakout_nnconfig_16;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[1];
      break;
    case BLOCK_32X32:
      nn_config = &av2_partition_breakout_nnconfig_32;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[2];
      break;
    case BLOCK_64X64:
      nn_config = &av2_partition_breakout_nnconfig_64;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[3];
      break;
    case BLOCK_128X128:
      nn_config = &av2_partition_breakout_nnconfig_128;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[4];
      break;
    case BLOCK_256X256: return 0; break;
    default: assert(0 && "Unexpected bsize.");
  }
  if (!nn_config || thresh < 0) return 0;

  // Generate feature values.
  float features[FEATURES];
  int feature_index = 0;
  avm_clear_system_state();

  const int num_pels_log2 = num_pels_log2_lookup[bsize];
  float rate_f = (float)AVMMIN(rd_stats->rate, INT_MAX);
  rate_f = ((float)x->rdmult / 128.0f / 512.0f / (float)(1 << num_pels_log2)) *
           rate_f;
  features[feature_index++] = rate_f;

  const float dist_f =
      (float)(AVMMIN(rd_stats->dist, INT_MAX) >> num_pels_log2);
  features[feature_index++] = dist_f;

  features[feature_index++] = (float)pb_source_variance;

  const int dc_q = (int)x->plane[0].dequant_QTX[0];
  features[feature_index++] =
      ((float)dc_q * (float)dc_q) / (256 << (2 * QUANT_TABLE_BITS));

  assert(feature_index == FEATURES);

  // Calculate score using the NN model.
  float score = 0.0f;
  av2_nn_predict(features, nn_config, 1, &score);
  avm_clear_system_state();

  // Make decision.
  return (int)(score * 100) >= thresh;
}
#undef FEATURES

void av2_sms_run_motion_search(AV2_COMP *const cpi, MACROBLOCK *x,
                               SIMPLE_MOTION_DATA_TREE *sms_tree, int mi_row,
                               int mi_col, BLOCK_SIZE bsize) {
  if (!sms_tree || sms_tree->sms_none_valid) return;
  if (frame_is_intra_only(&cpi->common)) return;
  if (cpi->is_screen_content_type) return;
  if (!is_square_block(bsize) || bsize == BLOCK_256X256) return;
  simple_motion_search_prune_part_features(cpi, x, sms_tree, mi_row, mi_col,
                                           bsize, NULL,
                                           FEATURE_SMS_PRUNE_PART_FLAG);
}

void av2_prune_partitions_before_search(
    AV2_COMP *const cpi, MACROBLOCK *const x, int mi_row, int mi_col,
    BLOCK_SIZE bsize, SIMPLE_MOTION_DATA_TREE *const sms_tree,
    PartitionSearchState *partition_search_state, bool *do_square_split,
    const PC_TREE *pc_tree) {
  const AV2_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCKD *const xd = &x->e_mbd;
  if (!bru_is_sb_active(cm, mi_col, mi_row)) return;

  if (cpi->sf.part_sf.sms_unified_prune && sms_tree &&
      !sms_tree->sms_unified_valid && !frame_is_intra_only(cm) &&
      !cpi->is_screen_content_type && is_square_block(bsize) &&
      bsize >= BLOCK_8X8) {
    av2_sms_unified_compute(cpi, x, sms_tree, mi_row, mi_col, bsize);
  }

  if (cpi->sf.part_sf.sms_unified_prune && sms_tree) {
    av2_sms_unified_prune_rect(cpi, sms_tree, partition_search_state);
  }

  // A CNN-based speed feature pruning out either split or all non-split
  // partition in INTRA frame coding.
  const int try_intra_cnn_split =
      !cpi->is_screen_content_type && frame_is_intra_only(cm) &&
      cpi->sf.part_sf.intra_cnn_split && xd->tree_type != CHROMA_PART &&
      cm->sb_size >= BLOCK_64X64 && bsize <= BLOCK_64X64 &&
      bsize >= BLOCK_8X8 &&
      mi_row + mi_size_high[bsize] <= mi_params->mi_rows &&
      mi_col + mi_size_wide[bsize] <= mi_params->mi_cols;

  if (try_intra_cnn_split) {
    av2_intra_mode_cnn_partition(&cpi->common, x, bsize,
                                 x->part_search_info.quad_tree_idx,
                                 partition_search_state, do_square_split);
  }

  // Use simple motion search to prune out split or non-split partitions. This
  // must be done prior to PARTITION_SPLIT to propagate the initial mvs to a
  // smaller blocksize.
  // TODO (any) : Train the ML model for 'BLOCK_256X256' to enable optimization
  // for NONE partition.
  const int try_split_only =
      !cpi->is_screen_content_type &&
      cpi->sf.part_sf.simple_motion_search_split &&
      av2_partition_ml_pruning_active(
          x, partition_search_state->forced_partition) &&
      *do_square_split && bsize >= BLOCK_8X8 &&
      mi_row + mi_size_high[bsize] <= mi_params->mi_rows &&
      bsize < BLOCK_256X256 &&
      mi_col + mi_size_wide[bsize] <= mi_params->mi_cols &&
      !frame_is_intra_only(cm) && is_square_block(bsize) && sms_tree &&
      partition_search_state->partition_allowed[PARTITION_NONE];

  if (try_split_only) {
    av2_simple_motion_search_based_split(cpi, x, sms_tree, mi_row, mi_col,
                                         bsize, partition_search_state,
                                         do_square_split);
    if (!partition_search_state->partition_allowed[PARTITION_NONE]) {
      av2_cache_best_partition(x->sms_bufs, mi_row, mi_col, bsize, cm->sb_size,
                               PARTITION_HORZ, (int8_t)pc_tree->region_type);
      const int mi_step = block_size_high[bsize] / 2;
      BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_HORZ);
      av2_cache_best_partition(x->sms_bufs, mi_row, mi_col, subsize,
                               cm->sb_size, PARTITION_VERT,
                               (int8_t)pc_tree->region_type);
      av2_cache_best_partition(x->sms_bufs, mi_row + mi_step, mi_col, subsize,
                               cm->sb_size, PARTITION_VERT,
                               (int8_t)pc_tree->region_type);
    }
    (void)pc_tree;
  }

  // Use simple motion search to prune out rectangular partition in some
  // direction. The results are stored in prune_horz and prune_vert in order to
  // bypass future related pruning checks if a pruning decision has been made.
  const int try_prune_rect =
      !cpi->is_screen_content_type &&
      cpi->sf.part_sf.simple_motion_search_prune_rect &&
      !frame_is_intra_only(cm) &&
      partition_search_state->do_rectangular_split &&
      (*do_square_split ||
       partition_search_state->partition_allowed[PARTITION_NONE] ||
       (partition_search_state->prune_partition[PARTITION_HORZ] &&
        partition_search_state->prune_partition[PARTITION_VERT])) &&
      (partition_search_state->partition_allowed[PARTITION_HORZ] ||
       partition_search_state->partition_allowed[PARTITION_VERT]) &&
      bsize >= BLOCK_8X8;

  if (try_prune_rect) {
    av2_simple_motion_search_prune_rect(cpi, x, sms_tree, mi_row, mi_col, bsize,
                                        partition_search_state);
  }
}

void av2_prune_partitions_by_max_min_bsize(
    SuperBlockEnc *sb_enc, BLOCK_SIZE bsize, int is_not_edge_block,
    PartitionSearchState *partition_search_state, bool *do_square_split) {
  if (!is_partition_point(bsize) ||
      partition_search_state->forced_partition != PARTITION_INVALID) {
    // Special case. We can't enforce min/max constraints here.
    return;
  }

  assert(is_bsize_square(sb_enc->max_partition_size));
  assert(is_bsize_square(sb_enc->min_partition_size));
  assert(sb_enc->min_partition_size <= sb_enc->max_partition_size);
  const int max_partition_size_1d = block_size_wide[sb_enc->max_partition_size];

  assert(is_bsize_geq(sb_enc->max_partition_size, sb_enc->min_partition_size));
  const int block_height = block_size_high[bsize];
  const int block_width = block_size_wide[bsize];
  const int is_le_min_sq_part = is_bsize_geq(sb_enc->min_partition_size, bsize);
  const int is_gt_max_sq_part = (block_height > max_partition_size_1d) ||
                                (block_width > max_partition_size_1d);
  (void)do_square_split;
  (void)is_not_edge_block;

  if (is_gt_max_sq_part) {  // current block size is larger than max size.
    // Disable some partition types to partition down to max allowed size.
    partition_search_state->prune_partition[PARTITION_NONE] = true;
    partition_search_state->prune_partition[PARTITION_HORZ_3] = true;
    partition_search_state->prune_partition[PARTITION_VERT_3] = true;
    partition_search_state->prune_partition[PARTITION_HORZ_4A] = true;
    partition_search_state->prune_partition[PARTITION_VERT_4A] = true;
    partition_search_state->prune_partition[PARTITION_HORZ_4B] = true;
    partition_search_state->prune_partition[PARTITION_VERT_4B] = true;
    if (partition_search_state->partition_allowed[PARTITION_SPLIT]) {
      // only allow split
      partition_search_state->prune_partition[PARTITION_HORZ] = true;
      partition_search_state->prune_partition[PARTITION_VERT] = true;
    } else {  // only allow one of horz or vert
      assert(partition_search_state->partition_allowed[PARTITION_HORZ] ||
             partition_search_state->partition_allowed[PARTITION_VERT]);
      if (partition_search_state->prune_partition[PARTITION_HORZ] &&
          partition_search_state->prune_partition[PARTITION_VERT]) {
        if (partition_search_state->partition_allowed[PARTITION_HORZ]) {
          partition_search_state->prune_partition[PARTITION_HORZ] = false;
        }
        if (partition_search_state->partition_allowed[PARTITION_VERT]) {
          partition_search_state->prune_partition[PARTITION_VERT] = false;
        }
      }
      if (partition_search_state->partition_allowed[PARTITION_HORZ] &&
          partition_search_state->partition_allowed[PARTITION_VERT] &&
          !partition_search_state->prune_partition[PARTITION_HORZ] &&
          !partition_search_state->prune_partition[PARTITION_VERT]) {
        if (is_wide_block(bsize)) {  // Allow VERT partition only.
          partition_search_state->prune_partition[PARTITION_HORZ] = true;
        } else {  // Allow HORZ partition only.
          assert(is_square_block(bsize) || is_tall_block(bsize));
          partition_search_state->prune_partition[PARTITION_VERT] = true;
        }
      }
    }
  } else if (is_le_min_sq_part) {  // current block size is less or equal to min
    // Disallow all 2-way partitions.
    partition_search_state->prune_partition[PARTITION_HORZ] = true;
    partition_search_state->prune_partition[PARTITION_VERT] = true;
    // Disallow all H and uneven-4way partitions.
    partition_search_state->prune_partition[PARTITION_HORZ_3] = true;
    partition_search_state->prune_partition[PARTITION_VERT_3] = true;
    partition_search_state->prune_partition[PARTITION_HORZ_4A] = true;
    partition_search_state->prune_partition[PARTITION_VERT_4A] = true;
    partition_search_state->prune_partition[PARTITION_HORZ_4B] = true;
    partition_search_state->prune_partition[PARTITION_VERT_4B] = true;
  }
}

// Gets the number of sms data in a single dimension
static INLINE int get_sms_count_from_length(int mi_length) {
  switch (mi_length) {
    case 64: return BLOCK_256_COUNT;
    case 32: return BLOCK_128_COUNT;
    case 16: return BLOCK_64_COUNT;
    case 8: return BLOCK_32_COUNT;
    case 4: return BLOCK_16_COUNT;
    case 2: return BLOCK_8_COUNT;
    case 1: return BLOCK_4_COUNT;
    default: assert(0 && "Invalid mi_width"); return -1;
  }
}

// Gets the linear index corresponds to the current block.

static INLINE int get_sms_arr_1d_idx(int mi_bsize, int mi_in_sb) {
  int idx = -1;
  if (mi_bsize <= 2) {
    idx = mi_in_sb;
  } else if (mi_bsize <= 8) {
    if (mi_in_sb % (mi_bsize / 4) != 0) return -1;
    idx = mi_in_sb / (mi_bsize / 4);
  } else {
    if (mi_in_sb % (mi_bsize / 2) != 0) return -1;
    idx = mi_in_sb / (mi_bsize / 2);
  }
  assert(idx >= 0 && idx < get_sms_count_from_length(mi_bsize));

  return idx;
}

#define MAKE_SMS_ARR_SWITCH_CASE(width, height, sdp_flag) \
  case BLOCK_##width##X##height: {                        \
    return sms_bufs->b_##width##x##height##_##sdp_flag;   \
  }

// Returns the buffer in SimpleMotionDataBufs that correspond to bsize.
static INLINE SimpleMotionData *get_sms_arr(SimpleMotionDataBufs *sms_bufs,
                                            BLOCK_SIZE bsize,
                                            int8_t region_type) {
  if (region_type == 1) {
    switch (bsize) {
      // Square blocks
      MAKE_SMS_ARR_SWITCH_CASE(256, 256, 1);
      MAKE_SMS_ARR_SWITCH_CASE(128, 128, 1);
      MAKE_SMS_ARR_SWITCH_CASE(64, 64, 1);
      MAKE_SMS_ARR_SWITCH_CASE(32, 32, 1);
      MAKE_SMS_ARR_SWITCH_CASE(16, 16, 1);
      MAKE_SMS_ARR_SWITCH_CASE(8, 8, 1);
      MAKE_SMS_ARR_SWITCH_CASE(4, 4, 1);

      // 1:2 blocks
      MAKE_SMS_ARR_SWITCH_CASE(128, 256, 1);
      MAKE_SMS_ARR_SWITCH_CASE(64, 128, 1);
      MAKE_SMS_ARR_SWITCH_CASE(32, 64, 1);
      MAKE_SMS_ARR_SWITCH_CASE(16, 32, 1);
      MAKE_SMS_ARR_SWITCH_CASE(8, 16, 1);
      MAKE_SMS_ARR_SWITCH_CASE(4, 8, 1);

      // 2:1 blocks
      MAKE_SMS_ARR_SWITCH_CASE(256, 128, 1);
      MAKE_SMS_ARR_SWITCH_CASE(128, 64, 1);
      MAKE_SMS_ARR_SWITCH_CASE(64, 32, 1);
      MAKE_SMS_ARR_SWITCH_CASE(32, 16, 1);
      MAKE_SMS_ARR_SWITCH_CASE(16, 8, 1);
      MAKE_SMS_ARR_SWITCH_CASE(8, 4, 1);

      // 1:4 blocks
      MAKE_SMS_ARR_SWITCH_CASE(16, 64, 1);
      MAKE_SMS_ARR_SWITCH_CASE(8, 32, 1);
      MAKE_SMS_ARR_SWITCH_CASE(4, 16, 1);

      // 4:1 blocks
      MAKE_SMS_ARR_SWITCH_CASE(64, 16, 1);
      MAKE_SMS_ARR_SWITCH_CASE(32, 8, 1);
      MAKE_SMS_ARR_SWITCH_CASE(16, 4, 1);

      // 1:8 blocks
      MAKE_SMS_ARR_SWITCH_CASE(8, 64, 1);
      MAKE_SMS_ARR_SWITCH_CASE(4, 32, 1);

      // 8:1 blocks
      MAKE_SMS_ARR_SWITCH_CASE(64, 8, 1);
      MAKE_SMS_ARR_SWITCH_CASE(32, 4, 1);

      default: assert(0 && "Invalid bsize"); return NULL;
    }
  } else {  // region_type = 0
    switch (bsize) {
      // Square blocks
      MAKE_SMS_ARR_SWITCH_CASE(256, 256, 0);
      MAKE_SMS_ARR_SWITCH_CASE(128, 128, 0);
      MAKE_SMS_ARR_SWITCH_CASE(64, 64, 0);
      MAKE_SMS_ARR_SWITCH_CASE(32, 32, 0);
      MAKE_SMS_ARR_SWITCH_CASE(16, 16, 0);
      MAKE_SMS_ARR_SWITCH_CASE(8, 8, 0);
      MAKE_SMS_ARR_SWITCH_CASE(4, 4, 0);

      // 1:2 blocks
      MAKE_SMS_ARR_SWITCH_CASE(128, 256, 0);
      MAKE_SMS_ARR_SWITCH_CASE(64, 128, 0);
      MAKE_SMS_ARR_SWITCH_CASE(32, 64, 0);
      MAKE_SMS_ARR_SWITCH_CASE(16, 32, 0);
      MAKE_SMS_ARR_SWITCH_CASE(8, 16, 0);
      MAKE_SMS_ARR_SWITCH_CASE(4, 8, 0);

      // 2:1 blocks
      MAKE_SMS_ARR_SWITCH_CASE(256, 128, 0);
      MAKE_SMS_ARR_SWITCH_CASE(128, 64, 0);
      MAKE_SMS_ARR_SWITCH_CASE(64, 32, 0);
      MAKE_SMS_ARR_SWITCH_CASE(32, 16, 0);
      MAKE_SMS_ARR_SWITCH_CASE(16, 8, 0);
      MAKE_SMS_ARR_SWITCH_CASE(8, 4, 0);

      // 1:4 blocks
      MAKE_SMS_ARR_SWITCH_CASE(16, 64, 0);
      MAKE_SMS_ARR_SWITCH_CASE(8, 32, 0);
      MAKE_SMS_ARR_SWITCH_CASE(4, 16, 0);

      // 4:1 blocks
      MAKE_SMS_ARR_SWITCH_CASE(64, 16, 0);
      MAKE_SMS_ARR_SWITCH_CASE(32, 8, 0);
      MAKE_SMS_ARR_SWITCH_CASE(16, 4, 0);

      // 1:8 blocks
      MAKE_SMS_ARR_SWITCH_CASE(8, 64, 0);
      MAKE_SMS_ARR_SWITCH_CASE(4, 32, 0);

      // 8:1 blocks
      MAKE_SMS_ARR_SWITCH_CASE(64, 8, 0);
      MAKE_SMS_ARR_SWITCH_CASE(32, 4, 0);

      default: assert(0 && "Invalid bsize"); return NULL;
    }
  }
}
#undef MAKE_SMS_ARR_SWITCH_CASE

// Retrieves the SimpleMotionData from SimpleMotionDataBufs
SimpleMotionData *av2_get_sms_data_entry(SimpleMotionDataBufs *sms_bufs,
                                         int mi_row, int mi_col,
                                         BLOCK_SIZE bsize, BLOCK_SIZE sb_size,
                                         int8_t region_type) {
  assert(mi_size_high[sb_size] == mi_size_wide[sb_size]);
  assert(bsize < BLOCK_SIZES_ALL);
  const int mi_in_sb = mi_size_high[sb_size];
  const int mi_row_in_sb = mi_row % mi_in_sb;
  const int mi_col_in_sb = mi_col % mi_in_sb;
  const int mi_high = mi_size_high[bsize];
  const int mi_wide = mi_size_wide[bsize];
  const int idx_row_in_sb = get_sms_arr_1d_idx(mi_high, mi_row_in_sb);
  if (idx_row_in_sb == -1) return NULL;
  const int idx_col_in_sb = get_sms_arr_1d_idx(mi_wide, mi_col_in_sb);
  if (idx_col_in_sb == -1) return NULL;
  const int arr_stride = get_sms_count_from_length(mi_wide);
  SimpleMotionData *sms_arr = get_sms_arr(sms_bufs, bsize, region_type);
  return &sms_arr[idx_row_in_sb * arr_stride + idx_col_in_sb];
}

void av2_cache_best_partition(SimpleMotionDataBufs *sms_bufs, int mi_row,
                              int mi_col, BLOCK_SIZE bsize, BLOCK_SIZE sb_size,
                              PARTITION_TYPE partition, int8_t region_type) {
  SimpleMotionData *cur_block = av2_get_sms_data_entry(
      sms_bufs, mi_row, mi_col, bsize, sb_size, region_type);
  cur_block->has_prev_partition = 1;
  cur_block->prev_partition = partition;
}

// Performs a simple motion search and store the result in sms_data.
static void compute_sms_data(AV2_COMP *const cpi, const TileInfo *const tile,
                             MACROBLOCK *x, SimpleMotionData *sms_data,
                             int mi_row, int mi_col, BLOCK_SIZE bsize
#if CONFIG_ML_PART_SPLIT
                             ,
                             ThreadData *td, bool need_residual_stats
#endif  // CONFIG_ML_PART_SPLIT
) {
  const AV2_COMMON *const cm = &cpi->common;
  const int ref_frame = get_closest_pastcur_ref_or_ref0(cm);
  assert(ref_frame >= 0);
  if (mi_col >= cm->mi_params.mi_cols || mi_row >= cm->mi_params.mi_rows) {
    // If the whole block is outside of the image, set the var and sse to 0.
    sms_data->sse = 0;
    sms_data->var = 0;
    sms_data->dist = 0;
    sms_data->rate = 0;
    sms_data->rdcost = 0;
    sms_data->ref_frame = -1;
    sms_data->rdmult = 0;
    sms_data->valid = 1;
#if CONFIG_ML_PART_SPLIT
    if (need_residual_stats) {
      sms_data->residual_stats_valid = true;
      memset(&sms_data->residual_stats, 0, sizeof(sms_data->residual_stats));
    }
#endif  // CONFIG_ML_PART_SPLIT
    return;
  }
  set_offsets_for_motion_search(cpi, x, mi_row, mi_col, bsize);
  //  We need to update the rd-mult here to in case we are doing simple motion
  //  search on a subblock of the current coding block.
  const int orig_rdmult = x->rdmult;
  const AQ_MODE aq_mode = cpi->oxcf.q_cfg.aq_mode;
  MB_MODE_INFO *mbmi = x->e_mbd.mi[0];
  // These values need to be initialized in the context in order for the
  // motion search to work correctly, otherwise the values will be picked up
  // from what they were set in the previous coding blocks and which is not
  // the intended behaviour.
  mbmi->mode = NEWMV;
  mbmi->refinemv_flag = 0;
  mbmi->pb_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
  mbmi->warpmv_with_mvd_flag = 0;
  mbmi->sb_type[0] = mbmi->sb_type[1] = bsize;
  mbmi->chroma_ref_info.bsize_base = bsize;
  mbmi->chroma_ref_info.is_chroma_ref = 1;
#if CONFIG_ML_PART_SPLIT
  // only if NONE part pruning is enabled
  if (cpi->sf.part_sf.prune_none_with_ml) {
    mbmi->interinter_comp.type = COMPOUND_AVERAGE;
    mbmi->motion_mode = SIMPLE_TRANSLATION;
    mbmi->comp_group_idx = 0;
    mbmi->use_intrabc[0] = 0;
    mbmi->use_intrabc[1] = 0;
    mbmi->interp_fltr = MULTITAP_SHARP;
    mbmi->bawp_flag[0] = 0;
    mbmi->cwp_idx = CWP_EQUAL;
  }
#endif  // CONFIG_ML_PART_SPLIT
  mbmi->use_amvd = 0;
  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, aq_mode, mbmi);
  // Set error per bit for current rdmult
  av2_set_error_per_bit(&x->mv_costs, x->rdmult);
  if (cm->ref_frame_flags & (1 << ref_frame)) {
    const MACROBLOCKD *xd = &x->e_mbd;
    const uint16_t *src_buf = x->plane[0].src.buf;
    const uint16_t *dst_buf = xd->plane[0].dst.buf;
    const int src_stride = x->plane[0].src.stride;
    const int dst_stride = xd->plane[0].dst.stride;
    if (sms_data->num_start_mvs == 0) {
      sms_data->start_mv_list[sms_data->num_start_mvs++] = kZeroMv;
    }
    sms_data->rdcost = INT64_MAX;
    SimpleMotionData best_data = *sms_data;
    for (int idx = 0; idx < sms_data->num_start_mvs; idx++) {
      const MV start_mv = sms_data->start_mv_list[idx];
      const FULLPEL_MV start_mv_full = get_fullmv_from_mv(&start_mv);
      av2_simple_motion_search_ext(cpi, tile, x, mi_row, mi_col, bsize,
                                   ref_frame, start_mv_full, 1, 1, sms_data);
      sms_data->var = cpi->fn_ptr[bsize].vf(src_buf, src_stride, dst_buf,
                                            dst_stride, &sms_data->sse);
#if CONFIG_ML_PART_SPLIT
      if (need_residual_stats) {
        compute_residual_stats(cpi, td, x, bsize, &sms_data->residual_stats);
        sms_data->residual_stats_valid = true;

        assert(sms_data->var == sms_data->residual_stats.var);
        assert(sms_data->sse == sms_data->residual_stats.sse);
      }
#endif  // CONFIG_ML_PART_SPLIT
      sms_data->dist = 16 * sms_data->sse;
      sms_data->rate = 0;
      sms_data->rdcost = RDCOST(x->rdmult, sms_data->rate, sms_data->dist);
      if (sms_data->rdcost <= best_data.rdcost) {
        best_data = *sms_data;
      }
    }
    *sms_data = best_data;
  }
#if CONFIG_ML_PART_SPLIT
  // If the above code didn't actually execute the residual stats computation
  // but all of the downstream code assumes the residual stats have been
  // computed.
  if (need_residual_stats && !sms_data->residual_stats_valid) {
    sms_data->residual_stats_valid = true;
    memset(&sms_data->residual_stats, 0, sizeof(sms_data->residual_stats));
  }
#endif  // CONFIG_ML_PART_SPLIT
  sms_data->valid = 1;
  sms_data->bsize = bsize;
  sms_data->mi_row = mi_row;
  sms_data->mi_col = mi_col;
  sms_data->ref_frame = ref_frame;
  sms_data->rdmult = x->rdmult;
  x->rdmult = orig_rdmult;
  return;
}

static INLINE void add_start_mv_to_block(SimpleMotionData *block, MV start_mv) {
  if (block->num_start_mvs == kSMSMaxStartMVs) {
    return;
  }
  for (int idx = 0; idx < block->num_start_mvs; idx++) {
    const int_mv *cur_mv = (int_mv *)&block->start_mv_list[idx];
    if (((int_mv *)&start_mv)->as_int == cur_mv->as_int) {
      return;
    }
  }
  block->start_mv_list[block->num_start_mvs++] = start_mv;
}

static INLINE void add_start_mv_to_partition(
    SimpleMotionDataBufs *sms_bufs, int mi_row, int mi_col, BLOCK_SIZE bsize,
    BLOCK_SIZE sb_size, PARTITION_TYPE partition, MV start_mv) {
  assert(bsize < BLOCK_SIZES_ALL);
  const BLOCK_SIZE part_subsize = get_partition_subsize(bsize, partition);
  if (part_subsize == BLOCK_INVALID) return;

  int subblock_cnt = get_subblock_count(partition);
  int mi_rows[4], mi_cols[4];
  BLOCK_SIZE subsizes[4];
  get_partition_subblock_layout(partition, bsize, mi_row, mi_col, subsizes,
                                mi_rows, mi_cols);
  for (int sub_idx = 0; sub_idx < subblock_cnt; sub_idx++) {
    assert(subsizes[sub_idx] != BLOCK_INVALID);
    SimpleMotionData *subblock =
        av2_get_sms_data_entry(sms_bufs, mi_rows[sub_idx], mi_cols[sub_idx],
                               subsizes[sub_idx], sb_size, 1);
    add_start_mv_to_block(subblock, start_mv);
  }
}

// Computes and stores the simple motion search data for the block at mi_row,
// mi_col with block size bsize.
SimpleMotionData *av2_get_sms_data(AV2_COMP *const cpi,
                                   const TileInfo *const tile, MACROBLOCK *x,
                                   int mi_row, int mi_col, BLOCK_SIZE bsize
#if CONFIG_ML_PART_SPLIT
                                   ,
                                   ThreadData *td, bool need_residual_stats
#endif  // CONFIG_ML_PART_SPLIT
                                   ,
                                   int8_t region_type) {
  assert(region_type == 1);
  const AV2_COMMON *const cm = &cpi->common;
  const BLOCK_SIZE sb_size = cm->sb_size;
  SimpleMotionDataBufs *sms_bufs = x->sms_bufs;
  SimpleMotionData *cur_block = av2_get_sms_data_entry(
      sms_bufs, mi_row, mi_col, bsize, sb_size, region_type);
  if (!cur_block->valid
#if CONFIG_ML_PART_SPLIT
      || (need_residual_stats && !cur_block->residual_stats_valid)
#endif  // CONFIG_ML_PART_SPLIT
  ) {
    compute_sms_data(cpi, tile, x, cur_block, mi_row, mi_col, bsize
#if CONFIG_ML_PART_SPLIT
                     ,
                     td, need_residual_stats
#endif  // CONFIG_ML_PART_SPLIT
    );
    for (PARTITION_TYPE partition = PARTITION_NONE;
         partition < EXT_PARTITION_TYPES; partition++) {
      add_start_mv_to_partition(sms_bufs, mi_row, mi_col, bsize, sb_size,
                                partition, cur_block->fullmv);
    }
  }

  return cur_block;
}

PARTITION_TYPE av2_get_prev_partition(MACROBLOCK *x, int mi_row, int mi_col,
                                      BLOCK_SIZE bsize, BLOCK_SIZE sb_size,
                                      int8_t region_type) {
  SimpleMotionDataBufs *sms_bufs = x->sms_bufs;
  const SimpleMotionData *cur_block = av2_get_sms_data_entry(
      sms_bufs, mi_row, mi_col, bsize, sb_size, region_type);
  if (cur_block && cur_block->has_prev_partition) {
    return cur_block->prev_partition;
  } else {
    return PARTITION_INVALID;
  }
}

static AVM_INLINE int64_t clip_rate(const int rate) {
  if (rate == INT_MAX) {
    return av2_cost_symbol(EC_MIN_PROB);
  }
  return rate;
}

// Get the minimum partition block width and height(in log scale) under a
// SIMPLE_MOTION_DATA_TREE.
static inline void get_min_bsize(const SIMPLE_MOTION_DATA_TREE *sms_tree,
                                 int *min_bw, int *min_bh) {
  if (!sms_tree) return;

  const BLOCK_SIZE bsize = sms_tree->block_size;
  if (bsize == BLOCK_4X4) {
    *min_bw = 0;
    *min_bh = 0;
    return;
  }

  PARTITION_TYPE part_type = sms_tree->partitioning;
  if (part_type == PARTITION_INVALID) return;

  if (part_type == PARTITION_SPLIT) {
    for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
      get_min_bsize(sms_tree->split[i], min_bw, min_bh);
    }
  } else {
    if (part_type == PARTITION_HORZ_4A || part_type == PARTITION_HORZ_4B ||
        part_type == PARTITION_VERT_4A || part_type == PARTITION_VERT_4B ||
        part_type == PARTITION_HORZ_3 || part_type == PARTITION_VERT_3)
      part_type = PARTITION_SPLIT;
    const BLOCK_SIZE subsize = get_partition_subsize(bsize, part_type);
    if (subsize != BLOCK_INVALID) {
      *min_bw = AVMMIN(*min_bw, mi_size_wide_log2[subsize]);
      *min_bh = AVMMIN(*min_bh, mi_size_high_log2[subsize]);
    }
  }
}

static inline void add_rd_feature(int64_t rd, int64_t best_rd, float *features,
                                  int *feature_idx) {
  const int rd_valid = rd > 0 && rd < INT64_MAX;
  const float rd_ratio = rd_valid ? (float)rd / best_rd : 1.0f;
  features[(*feature_idx)++] = (float)rd_valid;
  features[(*feature_idx)++] = rd_ratio;
}

#define FEATURES 31
void av2_ml_early_term_after_split(AV2_COMP *const cpi, MACROBLOCK *const x,
                                   SIMPLE_MOTION_DATA_TREE *const sms_tree,
                                   int64_t best_rd, int64_t part_none_rd,
                                   int64_t part_split_rd,
                                   int64_t *split_block_rd,
                                   PartitionSearchState *part_state) {
  const PartitionBlkParams *blk_params = &part_state->part_blk_params;
  const int mi_row = blk_params->mi_row, mi_col = blk_params->mi_col;
  const BLOCK_SIZE bsize = blk_params->bsize;

  if (best_rd <= 0 || best_rd == INT64_MAX ||
      part_state->terminate_partition_search || bsize == BLOCK_256X256)
    return;

  const AV2_COMMON *const cm = &cpi->common;
  const NN_CONFIG *nn_config = NULL;
  const float thresholds[5] = { 0.0035648215612127795f, 0.0014665436382494844f,
                                5.110332251005399e-5f, 3.749879965048595e-5f,
                                9.213190802080392e-4f };
  float thresh = -1e6;
  switch (bsize) {
    case BLOCK_128X128:
      nn_config = &av2_early_term_after_split_nnconfig_64;
      thresh = thresholds[0];
      break;
    case BLOCK_64X64:
      nn_config = &av2_early_term_after_split_nnconfig_64;
      thresh = thresholds[1];
      break;
    case BLOCK_32X32:
      nn_config = &av2_early_term_after_split_nnconfig_32;
      thresh = thresholds[2];
      break;
    case BLOCK_16X16:
      nn_config = &av2_early_term_after_split_nnconfig_16;
      thresh = thresholds[3];
      break;
    case BLOCK_8X8:
      nn_config = &av2_early_term_after_split_nnconfig_8;
      thresh = thresholds[4];
      break;
    case BLOCK_4X4: break;
    default:
      assert(0 && "Invalid block size in av2_ml_early_term_after_split().");
      break;
  }
  if (!nn_config) return;

  const MACROBLOCKD *const xd = &x->e_mbd;
  const int dc_q = av2_dc_quant_QTX(x->qindex, 0,
                                    cm->seq_params.base_y_dc_delta_q, xd->bd) >>
                   (xd->bd - 8);
  const int bs = block_size_wide[bsize];
  int f_idx = 0;
  float features[FEATURES] = { 0.0f };

  features[f_idx++] = log1pf((float)dc_q / 4.0f);
  features[f_idx++] = log1pf((float)best_rd / bs / bs / 1024.0f);

  add_rd_feature(part_none_rd, best_rd, features, &f_idx);
  add_rd_feature(part_split_rd, best_rd, features, &f_idx);

  for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
    add_rd_feature(split_block_rd[i], best_rd, features, &f_idx);
    int min_bw = MAX_SB_SIZE_LOG2;
    int min_bh = MAX_SB_SIZE_LOG2;
    get_min_bsize(sms_tree->split[i], &min_bw, &min_bh);
    features[f_idx++] = (float)min_bw;
    features[f_idx++] = (float)min_bh;
  }

  simple_motion_search_prune_part_features(cpi, x, sms_tree, mi_row, mi_col,
                                           bsize, NULL,
                                           FEATURE_SMS_PRUNE_PART_FLAG);

  features[f_idx++] = log1pf((float)sms_tree->sms_none_feat[1]);

  features[f_idx++] = log1pf((float)sms_tree->split[0]->sms_none_feat[1]);
  features[f_idx++] = log1pf((float)sms_tree->split[1]->sms_none_feat[1]);
  features[f_idx++] = log1pf((float)sms_tree->split[2]->sms_none_feat[1]);
  features[f_idx++] = log1pf((float)sms_tree->split[3]->sms_none_feat[1]);

  features[f_idx++] = log1pf((float)sms_tree->sms_rect_feat[1]);
  features[f_idx++] = log1pf((float)sms_tree->sms_rect_feat[3]);
  features[f_idx++] = log1pf((float)sms_tree->sms_rect_feat[5]);
  features[f_idx++] = log1pf((float)sms_tree->sms_rect_feat[7]);

  assert(f_idx == FEATURES);

  float score = 0.0f;
  av2_nn_predict(features, nn_config, 1, &score);

  float thresh_score = (float)log(thresh / (1 - thresh));

  // Score is indicator of confidence that we should NOT terminate.
  if (score < thresh_score) {
    part_state->terminate_partition_search = 1;
  }
}
#undef FEATURES

void av2_gather_erp_rect_features(
    float *ml_features, AV2_COMP *cpi, MACROBLOCK *x, const TileInfo *tile_info,
    const PC_TREE *pc_tree, const PartitionSearchState *part_search_state,
    int64_t part_none_rd, const int (*mi_pos_rect)[SUB_PARTITIONS_RECT][2]) {
  const PartitionBlkParams *blk_params = &part_search_state->part_blk_params;
  const BLOCK_SIZE bsize = blk_params->bsize;
  int num_features = 0;
  // Partition costs
  ml_features[num_features++] = x->rdmult;
  ml_features[num_features++] = part_none_rd;
  ml_features[num_features++] =
      clip_rate(part_search_state->partition_cost[PARTITION_NONE]);
  ml_features[num_features++] =
      clip_rate(part_search_state->partition_cost[PARTITION_HORZ]);
  ml_features[num_features++] =
      clip_rate(part_search_state->partition_cost[PARTITION_VERT]);

  const SimpleMotionData *blk_none = av2_get_sms_data(
      cpi, tile_info, x, blk_params->mi_row, blk_params->mi_col, bsize
#if CONFIG_ML_PART_SPLIT
      ,
      NULL, false
#endif  // CONFIG_ML_PART_SPLIT
      ,
      1);

  const BLOCK_SIZE h_size = get_partition_subsize(bsize, PARTITION_HORZ);
  const SimpleMotionData *blk_h1 =
      h_size != BLOCK_INVALID
          ? av2_get_sms_data(cpi, tile_info, x, mi_pos_rect[HORZ][0][0],
                             mi_pos_rect[HORZ][0][1], h_size
#if CONFIG_ML_PART_SPLIT
                             ,
                             NULL, false
#endif  // CONFIG_ML_PART_SPLIT
                             ,
                             1)
          : NULL;
  const SimpleMotionData *blk_h2 =
      h_size != BLOCK_INVALID
          ? av2_get_sms_data(cpi, tile_info, x, mi_pos_rect[HORZ][1][0],
                             mi_pos_rect[HORZ][1][1], h_size
#if CONFIG_ML_PART_SPLIT
                             ,
                             NULL, false
#endif  // CONFIG_ML_PART_SPLIT
                             ,
                             1)
          : NULL;

  const BLOCK_SIZE v_size = get_partition_subsize(bsize, PARTITION_VERT);
  const SimpleMotionData *blk_v1 =
      v_size != BLOCK_INVALID
          ? av2_get_sms_data(cpi, tile_info, x, mi_pos_rect[VERT][0][0],
                             mi_pos_rect[VERT][0][1], v_size
#if CONFIG_ML_PART_SPLIT
                             ,
                             NULL, false
#endif  // CONFIG_ML_PART_SPLIT
                             ,
                             1)
          : NULL;
  const SimpleMotionData *blk_v2 =
      v_size != BLOCK_INVALID
          ? av2_get_sms_data(cpi, tile_info, x, mi_pos_rect[VERT][1][0],
                             mi_pos_rect[VERT][1][1], v_size
#if CONFIG_ML_PART_SPLIT
                             ,
                             NULL, false
#endif  // CONFIG_ML_PART_SPLIT
                             ,
                             1)
          : NULL;

  // Results of SMS on the subblocks
  ml_features[num_features++] = blk_none->sse;
  ml_features[num_features++] = blk_none->var;
  if (h_size != BLOCK_INVALID) {
    ml_features[num_features++] = 1;
    ml_features[num_features++] = blk_h1->sse;
    ml_features[num_features++] = blk_h1->var;
    ml_features[num_features++] = blk_h2->sse;
    ml_features[num_features++] = blk_h2->var;
  } else {
    ml_features[num_features++] = 0;
    ml_features[num_features++] = 0;
    ml_features[num_features++] = 0;
    ml_features[num_features++] = 0;
    ml_features[num_features++] = 0;
  }
  if (v_size != BLOCK_INVALID) {
    ml_features[num_features++] = 1;
    ml_features[num_features++] = blk_v1->sse;
    ml_features[num_features++] = blk_v1->var;
    ml_features[num_features++] = blk_v2->sse;
    ml_features[num_features++] = blk_v2->var;
  } else {
    ml_features[num_features++] = 0;
    ml_features[num_features++] = 0;
    ml_features[num_features++] = 0;
    ml_features[num_features++] = 0;
    ml_features[num_features++] = 0;
  }

  // Whether we are in the middle of a PARTITION_3 subblock
  const PC_TREE *parent = pc_tree->parent;
  REGION_TYPE cur_region_type = pc_tree->region_type;
  ml_features[num_features++] =
      parent && (parent->horizontal3[cur_region_type][1] == pc_tree ||
                 parent->horizontal3[cur_region_type][2] == pc_tree);
  ml_features[num_features++] =
      parent && (parent->vertical3[cur_region_type][1] == pc_tree ||
                 parent->vertical3[cur_region_type][2] == pc_tree);
  assert(num_features == 19);
}
