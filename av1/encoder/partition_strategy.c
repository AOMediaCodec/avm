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

#include "av1/encoder/context_tree.h"
#include "av1/encoder/encodeframe_utils.h"
#include "config/aom_dsp_rtcd.h"

#include "aom_ports/system_state.h"

#include "av1/common/enums.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"

#include "av1/encoder/cnn.h"
#include "av1/encoder/partition_model_weights.h"
#include "av1/encoder/partition_cnn_weights.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/reconinter_enc.h"

#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/partition_search.h"
#include "av1/encoder/rdopt.h"
#if CONFIG_EXT_RECUR_PARTITIONS
#include "av1/common/idct.h"
#include "av1/encoder/hybrid_fwd_txfm.h"
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_ML_PART_SPLIT
#include "av1/encoder/part_split_prune_tflite.h"
#endif  // CONFIG_ML_PART_SPLIT

static AOM_INLINE void simple_motion_search_prune_part_features(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
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
void av1_intra_mode_cnn_partition(const AV1_COMMON *const cm, MACROBLOCK *x,
                                  BLOCK_SIZE bsize, int quad_tree_idx,
                                  int *partition_none_allowed,
                                  int *partition_horz_allowed,
                                  int *partition_vert_allowed,
                                  int *do_rectangular_split,
                                  int *do_square_split) {
  assert(cm->sb_size >= BLOCK_64X64 && "Invalid sb_size for intra_cnn!");
  const int bsize_idx = convert_bsize_to_idx(bsize);

  if (bsize == BLOCK_128X128) {
    return;
  }

  PartitionSearchInfo *part_info = &x->part_search_info;

  // Precompute the CNN part and cache the result in MACROBLOCK
  if (bsize == BLOCK_64X64 && !part_info->cnn_output_valid) {
    aom_clear_system_state();
    const CNN_CONFIG *cnn_config = &av1_intra_mode_cnn_partition_cnn_config;

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
        av1_dc_quant_QTX(x->qindex, 0, cm->seq_params.base_y_dc_delta_q,
                         bit_depth) >>
        (bit_depth - 8);
    part_info->log_q = logf(1.0f + (float)((int64_t)dc_q * (int64_t)dc_q) /
                                       (256 << (2 * QUANT_TABLE_BITS)));
    part_info->log_q =
        (part_info->log_q - av1_intra_mode_cnn_partition_mean[0]) /
        av1_intra_mode_cnn_partition_std[0];

    const int width = 65, height = 65,
              stride = x->plane[AOM_PLANE_Y].src.stride;

    uint16_t *image[1] = { x->plane[AOM_PLANE_Y].src.buf - stride - 1 };

    av1_cnn_predict_img_multi_out_highbd(image, width, height, stride,
                                         cnn_config, &thread_data, bit_depth,
                                         &output);

    part_info->cnn_output_valid = 1;
  }

  if (!part_info->cnn_output_valid) {
    return;
  }

  const NN_CONFIG *dnn_configs[5] = {
    NULL,
    &av1_intra_mode_cnn_partition_branch_0_dnn_config,
    &av1_intra_mode_cnn_partition_branch_1_dnn_config,
    &av1_intra_mode_cnn_partition_branch_2_dnn_config,
    &av1_intra_mode_cnn_partition_branch_3_dnn_config,
  };

  const NN_CONFIG *dnn_config = dnn_configs[bsize_idx];

  aom_clear_system_state();
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
  av1_nn_predict(dnn_features, dnn_config, 1, logits);
  aom_clear_system_state();

  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  float split_only_thresh = 100.0f, no_split_thresh = -100.0f;
  if (is_720p_or_larger) {
    split_only_thresh =
        av1_intra_mode_cnn_partition_split_thresh_hdres[bsize_idx];
    no_split_thresh =
        av1_intra_mode_cnn_partition_no_split_thresh_hdres[bsize_idx];
  } else if (is_480p_or_larger) {
    split_only_thresh =
        av1_intra_mode_cnn_partition_split_thresh_midres[bsize_idx];
    no_split_thresh =
        av1_intra_mode_cnn_partition_no_split_thresh_midres[bsize_idx];
  } else {
    split_only_thresh =
        av1_intra_mode_cnn_partition_split_thresh_lowres[bsize_idx];
    no_split_thresh =
        av1_intra_mode_cnn_partition_no_split_thresh_lowres[bsize_idx];
  }

  if (logits[0] > split_only_thresh) {
    *partition_none_allowed = 0;
    *partition_horz_allowed = 0;
    *partition_vert_allowed = 0;
    *do_rectangular_split = 0;
  }

  if (logits[0] < no_split_thresh) {
    *do_square_split = 0;
  }
}

void av1_simple_motion_search_based_split(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, int *partition_none_allowed,
    int *partition_horz_allowed, int *partition_vert_allowed,
    int *do_rectangular_split, int *do_square_split) {
  aom_clear_system_state();
  (void)partition_horz_allowed;
  (void)partition_vert_allowed;
  (void)do_rectangular_split;

  const AV1_COMMON *const cm = &cpi->common;
  const int bsize_idx = convert_bsize_to_idx(bsize);
  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  // res_idx is 0 for res < 480p, 1 for 480p, 2 for 720p+
  const int res_idx = is_480p_or_larger + is_720p_or_larger;

  assert(bsize_idx >= 0 && bsize_idx <= 4 &&
         "Invalid bsize in simple_motion_search_based_split");

  const float *ml_mean = av1_simple_motion_search_split_mean[bsize_idx];
  const float *ml_std = av1_simple_motion_search_split_std[bsize_idx];
  const NN_CONFIG *nn_config =
      av1_simple_motion_search_split_nn_config[bsize_idx];
  const int agg = cpi->sf.part_sf.simple_motion_search_prune_agg;

  const float split_only_thresh =
      av1_simple_motion_search_split_thresh[agg][res_idx][bsize_idx];
  const float no_split_thresh =
      av1_simple_motion_search_no_split_thresh[agg][res_idx][bsize_idx];

  float features[FEATURE_SIZE_SMS_SPLIT] = { 0.0f };
  simple_motion_search_prune_part_features(cpi, x, sms_tree, mi_row, mi_col,
                                           bsize, features,
                                           FEATURE_SMS_SPLIT_MODEL_FLAG);
  for (int idx = 0; idx < FEATURE_SIZE_SMS_SPLIT; idx++) {
    features[idx] = (features[idx] - ml_mean[idx]) / ml_std[idx];
  }

  float score = 0.0f;

  av1_nn_predict(features, nn_config, 1, &score);
  aom_clear_system_state();

  if (score > split_only_thresh) {
    *partition_none_allowed = 0;
#if CONFIG_EXT_RECUR_PARTITIONS
    (void)partition_horz_allowed;
    (void)partition_vert_allowed;
    (void)do_rectangular_split;
#else
    *partition_horz_allowed = 0;
    *partition_vert_allowed = 0;
    *do_rectangular_split = 0;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  }

  if (cpi->sf.part_sf.simple_motion_search_split >= 2 &&
      score < no_split_thresh) {
    *do_square_split = 0;
  }
}

// Given a list of ref frames in refs, performs simple_motion_search on each of
// the refs and returns the ref with the smallest sse. Returns -1 if none of the
// ref in the list is available. Also stores the best sse and var in best_sse,
// best_var, respectively. If save_mv is 0, don't update mv_ref_fulls in
// sms_tree. If save_mv is 1, update mv_ref_fulls under sms_tree and the
// subtrees.
static int simple_motion_search_get_best_ref(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, const int *const refs,
    int num_refs, int use_subpixel, int save_mv, unsigned int *best_sse,
    unsigned int *best_var) {
  const AV1_COMMON *const cm = &cpi->common;
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
          av1_simple_motion_search(cpi, x, mi_row, mi_col, bsize, ref,
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
static AOM_INLINE void simple_motion_search_prune_part_features(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, float *features,
    int features_to_get) {
  const int w_mi = mi_size_wide[bsize];
  const int h_mi = mi_size_high[bsize];
  assert(mi_size_wide[bsize] == mi_size_high[bsize]);
  // Setting up motion search
  int ref_list[1];
  ref_list[0] = get_closest_pastcur_ref_index(&cpi->common);

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

  aom_clear_system_state();
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
  aom_clear_system_state();

  const MACROBLOCKD *xd = &x->e_mbd;
  set_offsets_for_motion_search(cpi, x, mi_row, mi_col, bsize);

  // Q_INDEX
  const int dc_q =
      av1_dc_quant_QTX(x->qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
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

void av1_simple_motion_search_prune_rect(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, int partition_horz_allowed,
    int partition_vert_allowed, bool *prune_horz, bool *prune_vert) {
  // TODO(urvang): Need to change for uneven 4-way partition support.
#if CONFIG_EXT_RECUR_PARTITIONS
  assert(0 && "Not implemented");
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  aom_clear_system_state();
  const AV1_COMMON *const cm = &cpi->common;
  const int bsize_idx = convert_bsize_to_idx(bsize);
  const int is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  // res_idx is 0 for lowres, 1 for 48p, 2 for 720p+
  const int res_idx = is_480p_or_larger + is_720p_or_larger;

  // Get model parameters
  const NN_CONFIG *nn_config =
      av1_simple_motion_search_prune_rect_nn_config[bsize_idx];
  const float *ml_mean = av1_simple_motion_search_prune_rect_mean[bsize_idx],
              *ml_std = av1_simple_motion_search_prune_rect_std[bsize_idx];

  const int agg = cpi->sf.part_sf.simple_motion_search_prune_agg;
  const float prune_thresh =
      av1_simple_motion_search_prune_rect_thresh[agg][res_idx][bsize_idx];

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

  av1_nn_predict(features, nn_config, 1, scores);
  aom_clear_system_state();

  av1_nn_softmax(scores, probs, num_classes);

  // Determine if we should prune rectangular partitions.
  if (cpi->sf.part_sf.simple_motion_search_prune_rect &&
      !frame_is_intra_only(cm) &&
      (partition_horz_allowed || partition_vert_allowed) &&
      bsize >= BLOCK_8X8 && !av1_superres_scaled(cm)) {
    *prune_horz = probs[PARTITION_HORZ] <= prune_thresh;
    *prune_vert = probs[PARTITION_VERT] <= prune_thresh;
  }
}

// Early terminates PARTITION_NONE using simple_motion_search features and the
// rate, distortion, and rdcost of PARTITION_NONE. This is only called when:
//  - The frame is a show frame
//  - The frame is not intra only
//  - The current bsize is > BLOCK_8X8
//  - blk_row + blk_height/2 < total_rows and blk_col + blk_width/2 < total_cols
void av1_simple_motion_search_early_term_none(
    AV1_COMP *const cpi, MACROBLOCK *x, SIMPLE_MOTION_DATA_TREE *sms_tree,
    int mi_row, int mi_col, BLOCK_SIZE bsize, const RD_STATS *none_rdc,
    int *early_terminate) {
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
    ml_mean = av1_simple_motion_search_term_none_mean_128;
    ml_std = av1_simple_motion_search_term_none_std_128;
    ml_model = av1_simple_motion_search_term_none_model_128;
  } else if (bsize == BLOCK_64X64) {
    ml_mean = av1_simple_motion_search_term_none_mean_64;
    ml_std = av1_simple_motion_search_term_none_std_64;
    ml_model = av1_simple_motion_search_term_none_model_64;
  } else if (bsize == BLOCK_32X32) {
    ml_mean = av1_simple_motion_search_term_none_mean_32;
    ml_std = av1_simple_motion_search_term_none_std_32;
    ml_model = av1_simple_motion_search_term_none_model_32;
  } else if (bsize == BLOCK_16X16) {
    ml_mean = av1_simple_motion_search_term_none_mean_16;
    ml_std = av1_simple_motion_search_term_none_std_16;
    ml_model = av1_simple_motion_search_term_none_model_16;
#if CONFIG_EXT_RECUR_PARTITIONS
  } else if (bsize == BLOCK_256X256) {
    return;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
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
      *early_terminate = 1;
    }
  }
}

void av1_get_max_min_partition_features(AV1_COMP *const cpi, MACROBLOCK *x,
                                        int mi_row, int mi_col,
                                        float *features) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  const BLOCK_SIZE sb_size = cm->sb_size;

  assert(sb_size == BLOCK_128X128);

  int f_idx = 0;
  const int dc_q = av1_dc_quant_QTX(x->qindex, 0,
                                    cm->seq_params.base_y_dc_delta_q, xd->bd) >>
                   (xd->bd - 8);
  aom_clear_system_state();
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
      int_mv best_mv = av1_simple_motion_sse_var(
          cpi, x, this_mi_row, this_mi_col, mb_size, start_mv, 0, &sse, &var);

      aom_clear_system_state();
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
  aom_clear_system_state();
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

BLOCK_SIZE av1_predict_max_partition(const AV1_COMP *const cpi,
                                     const MACROBLOCK *const x,
                                     const float *features) {
  float scores[MAX_NUM_CLASSES_MAX_MIN_PART_PRED] = { 0.0f },
        probs[MAX_NUM_CLASSES_MAX_MIN_PART_PRED] = { 0.0f };
  const NN_CONFIG *nn_config = &av1_max_part_pred_nn_config;

  assert(cpi->sf.part_sf.auto_max_partition_based_on_simple_motion !=
         NOT_IN_USE);

  aom_clear_system_state();
  av1_nn_predict(features, nn_config, 1, scores);
  av1_nn_softmax(scores, probs, MAX_NUM_CLASSES_MAX_MIN_PART_PRED);

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
    const unsigned int source_variance = av1_high_get_sby_perpixel_variance(
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

// Get the minimum partition block width and height(in log scale) under a
// SIMPLE_MOTION_DATA_TREE.
static AOM_INLINE void get_min_bsize(const SIMPLE_MOTION_DATA_TREE *sms_tree,
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
#if !CONFIG_EXT_RECUR_PARTITIONS
    if (part_type == PARTITION_HORZ_A || part_type == PARTITION_HORZ_B ||
        part_type == PARTITION_VERT_A || part_type == PARTITION_VERT_B)
      part_type = PARTITION_SPLIT;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    const BLOCK_SIZE subsize = get_partition_subsize(bsize, part_type);
    if (subsize != BLOCK_INVALID) {
      *min_bw = AOMMIN(*min_bw, mi_size_wide_log2[subsize]);
      *min_bh = AOMMIN(*min_bh, mi_size_high_log2[subsize]);
    }
  }
}

static INLINE void add_rd_feature(int64_t rd, int64_t best_rd, float *features,
                                  int *feature_idx) {
  const int rd_valid = rd > 0 && rd < INT64_MAX;
  const float rd_ratio = rd_valid ? (float)rd / best_rd : 1.0f;
  features[(*feature_idx)++] = (float)rd_valid;
  features[(*feature_idx)++] = rd_ratio;
}

#define FEATURES 31
void av1_ml_early_term_after_split(AV1_COMP *const cpi, MACROBLOCK *const x,
                                   SIMPLE_MOTION_DATA_TREE *const sms_tree,
                                   BLOCK_SIZE bsize, int64_t best_rd,
                                   int64_t part_none_rd, int64_t part_split_rd,
                                   int64_t *split_block_rd, int mi_row,
                                   int mi_col,
                                   int *const terminate_partition_search) {
  if (best_rd <= 0 || best_rd == INT64_MAX || *terminate_partition_search)
    return;

  const AV1_COMMON *const cm = &cpi->common;
  const int is_480p_or_larger = AOMMIN(cm->width, cm->height) >= 480;
  const NN_CONFIG *nn_config = NULL;
  float thresh = -1e6;
  switch (bsize) {
    case BLOCK_128X128: break;
    case BLOCK_64X64:
      nn_config = &av1_early_term_after_split_nnconfig_64;
      thresh = is_480p_or_larger ? -2.0f : -1.2f;
      break;
    case BLOCK_32X32:
      nn_config = &av1_early_term_after_split_nnconfig_32;
      thresh = is_480p_or_larger ? -2.6f : -2.3f;
      break;
    case BLOCK_16X16:
      nn_config = &av1_early_term_after_split_nnconfig_16;
      thresh = is_480p_or_larger ? -2.0f : -2.4f;
      break;
    case BLOCK_8X8:
      nn_config = &av1_early_term_after_split_nnconfig_8;
      thresh = is_480p_or_larger ? -1.0f : -1.4f;
      break;
    case BLOCK_4X4: break;
    default:
      assert(0 && "Invalid block size in av1_ml_early_term_after_split().");
      break;
  }
  if (!nn_config) return;

  // Use more conservative threshold for level 1.
  if (cpi->sf.part_sf.ml_early_term_after_part_split_level < 2) thresh -= 0.3f;

  const MACROBLOCKD *const xd = &x->e_mbd;
  const int dc_q = av1_dc_quant_QTX(x->qindex, 0,
                                    cm->seq_params.base_y_dc_delta_q, xd->bd) >>
                   (xd->bd - 8);
  const int bs = block_size_wide[bsize];
  int f_idx = 0;
  float features[FEATURES] = { 0.0f };

  aom_clear_system_state();
  features[f_idx++] = logf(1.0f + (float)dc_q / (4 << QUANT_TABLE_BITS));
  features[f_idx++] = logf(1.0f + (float)best_rd / bs / bs / 1024.0f);

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

  features[f_idx++] = logf(1.0f + (float)sms_tree->sms_none_feat[1]);

  features[f_idx++] = logf(1.0f + (float)sms_tree->split[0]->sms_none_feat[1]);
  features[f_idx++] = logf(1.0f + (float)sms_tree->split[1]->sms_none_feat[1]);
  features[f_idx++] = logf(1.0f + (float)sms_tree->split[2]->sms_none_feat[1]);
  features[f_idx++] = logf(1.0f + (float)sms_tree->split[3]->sms_none_feat[1]);

  features[f_idx++] = logf(1.0f + (float)sms_tree->sms_rect_feat[1]);
  features[f_idx++] = logf(1.0f + (float)sms_tree->sms_rect_feat[3]);
  features[f_idx++] = logf(1.0f + (float)sms_tree->sms_rect_feat[5]);
  features[f_idx++] = logf(1.0f + (float)sms_tree->sms_rect_feat[7]);

  assert(f_idx == FEATURES);

  float score = 0.0f;
  av1_nn_predict(features, nn_config, 1, &score);
  // Score is indicator of confidence that we should NOT terminate.
  if (score < thresh) *terminate_partition_search = 1;
}
#undef FEATURES

void av1_ml_prune_rect_partition(const AV1_COMP *const cpi,
                                 const MACROBLOCK *const x, BLOCK_SIZE bsize,
                                 int64_t best_rd, int64_t none_rd,
                                 int64_t *split_rd, bool *const dst_prune_horz,
                                 bool *const dst_prune_vert) {
  if (bsize < BLOCK_8X8 || best_rd >= 1000000000) return;
  best_rd = AOMMAX(best_rd, 1);
  const NN_CONFIG *nn_config = NULL;
  const float prob_thresholds[5] = { 0.01f, 0.01f, 0.004f, 0.002f, 0.002f };
  float cur_thresh = 0.0f;
  switch (bsize) {
    case BLOCK_8X8:
      nn_config = &av1_rect_partition_nnconfig_8;
      cur_thresh = prob_thresholds[0];
      break;
    case BLOCK_16X16:
      nn_config = &av1_rect_partition_nnconfig_16;
      cur_thresh = prob_thresholds[1];
      break;
    case BLOCK_32X32:
      nn_config = &av1_rect_partition_nnconfig_32;
      cur_thresh = prob_thresholds[2];
      break;
    case BLOCK_64X64:
      nn_config = &av1_rect_partition_nnconfig_64;
      cur_thresh = prob_thresholds[3];
      break;
    case BLOCK_128X128:
      nn_config = &av1_rect_partition_nnconfig_128;
      cur_thresh = prob_thresholds[4];
      break;
    default: assert(0 && "Unexpected bsize.");
  }
  if (!nn_config) return;
  aom_clear_system_state();

  // 1. Compute input features
  float features[9];

  // RD cost ratios
  for (int i = 0; i < 5; i++) features[i] = 1.0f;
  if (none_rd > 0 && none_rd < 1000000000)
    features[0] = (float)none_rd / (float)best_rd;
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++) {
    if (split_rd[i] > 0 && split_rd[i] < 1000000000)
      features[1 + i] = (float)split_rd[i] / (float)best_rd;
  }

  // Variance ratios
  const MACROBLOCKD *const xd = &x->e_mbd;
  int whole_block_variance;
  whole_block_variance =
      av1_high_get_sby_perpixel_variance(cpi, &x->plane[0].src, bsize, xd->bd);
  whole_block_variance = AOMMAX(whole_block_variance, 1);

  int split_variance[SUB_PARTITIONS_SPLIT];
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
  struct buf_2d buf;
  buf.stride = x->plane[0].src.stride;
  const int bw = block_size_wide[bsize];
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
    const int x_idx = (i & 1) * bw / 2;
    const int y_idx = (i >> 1) * bw / 2;
    buf.buf = x->plane[0].src.buf + x_idx + y_idx * buf.stride;
    split_variance[i] =
        av1_high_get_sby_perpixel_variance(cpi, &buf, subsize, xd->bd);
  }

  for (int i = 0; i < SUB_PARTITIONS_SPLIT; i++)
    features[5 + i] = (float)split_variance[i] / (float)whole_block_variance;

  // 2. Do the prediction and prune 0-2 partitions based on their probabilities
  float raw_scores[3] = { 0.0f };
  av1_nn_predict(features, nn_config, 1, raw_scores);
  aom_clear_system_state();
  float probs[3] = { 0.0f };
  av1_nn_softmax(raw_scores, probs, 3);

  // probs[0] is the probability of the fact that both rectangular partitions
  // are worse than current best_rd
  if (probs[1] <= cur_thresh) (*dst_prune_horz) = 1;
  if (probs[2] <= cur_thresh) (*dst_prune_vert) = 1;
}

// Use a ML model to predict if horz_a, horz_b, vert_a, and vert_b should be
// considered.
void av1_ml_prune_ab_partition(
    BLOCK_SIZE bsize, int part_ctx, int var_ctx, int64_t best_rd,
    int64_t horz_rd[SUB_PARTITIONS_RECT], int64_t vert_rd[SUB_PARTITIONS_RECT],
    int64_t split_rd[SUB_PARTITIONS_SPLIT], int *const horza_partition_allowed,
    int *const horzb_partition_allowed, int *const verta_partition_allowed,
    int *const vertb_partition_allowed) {
  if (bsize < BLOCK_8X8 || best_rd >= 1000000000) return;
  const NN_CONFIG *nn_config = NULL;
  switch (bsize) {
    case BLOCK_8X8: nn_config = NULL; break;
    case BLOCK_16X16: nn_config = &av1_ab_partition_nnconfig_16; break;
    case BLOCK_32X32: nn_config = &av1_ab_partition_nnconfig_32; break;
    case BLOCK_64X64: nn_config = &av1_ab_partition_nnconfig_64; break;
    case BLOCK_128X128: nn_config = &av1_ab_partition_nnconfig_128; break;
    default: assert(0 && "Unexpected bsize.");
  }
  if (!nn_config) return;

  aom_clear_system_state();

  // Generate features.
  float features[10];
  int feature_index = 0;
  features[feature_index++] = (float)part_ctx;
  features[feature_index++] = (float)var_ctx;
  const int rdcost = (int)AOMMIN(INT_MAX, best_rd);
  int sub_block_rdcost[8] = { 0 };
  int rd_index = 0;
  for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
    if (horz_rd[i] > 0 && horz_rd[i] < 1000000000)
      sub_block_rdcost[rd_index] = (int)horz_rd[i];
    ++rd_index;
  }
  for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
    if (vert_rd[i] > 0 && vert_rd[i] < 1000000000)
      sub_block_rdcost[rd_index] = (int)vert_rd[i];
    ++rd_index;
  }
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
    if (split_rd[i] > 0 && split_rd[i] < 1000000000)
      sub_block_rdcost[rd_index] = (int)split_rd[i];
    ++rd_index;
  }
  for (int i = 0; i < 8; ++i) {
    // Ratio between the sub-block RD and the whole-block RD.
    float rd_ratio = 1.0f;
    if (sub_block_rdcost[i] > 0 && sub_block_rdcost[i] < rdcost)
      rd_ratio = (float)sub_block_rdcost[i] / (float)rdcost;
    features[feature_index++] = rd_ratio;
  }
  assert(feature_index == 10);

  // Calculate scores using the NN model.
  float score[16] = { 0.0f };
  av1_nn_predict(features, nn_config, 1, score);
  aom_clear_system_state();
  int int_score[16];
  int max_score = -1000;
  for (int i = 0; i < 16; ++i) {
    int_score[i] = (int)(100 * score[i]);
    max_score = AOMMAX(int_score[i], max_score);
  }

  // Make decisions based on the model scores.
  int thresh = max_score;
  switch (bsize) {
    case BLOCK_16X16: thresh -= 150; break;
    case BLOCK_32X32: thresh -= 100; break;
    default: break;
  }
  *horza_partition_allowed = 0;
  *horzb_partition_allowed = 0;
  *verta_partition_allowed = 0;
  *vertb_partition_allowed = 0;
  for (int i = 0; i < 16; ++i) {
    if (int_score[i] >= thresh) {
      if ((i >> 0) & 1) *horza_partition_allowed = 1;
      if ((i >> 1) & 1) *horzb_partition_allowed = 1;
      if ((i >> 2) & 1) *verta_partition_allowed = 1;
      if ((i >> 3) & 1) *vertb_partition_allowed = 1;
    }
  }
}

#if !CONFIG_EXT_RECUR_PARTITIONS
#define FEATURES 18
#define LABELS 4
// Use a ML model to predict if horz4 and vert4 should be considered.
void av1_ml_prune_4_partition(
    const AV1_COMP *const cpi, MACROBLOCK *const x, BLOCK_SIZE bsize,
    int part_ctx, int64_t best_rd,
    int64_t rect_part_rd[NUM_RECT_PARTS][SUB_PARTITIONS_RECT],
    int64_t split_rd[SUB_PARTITIONS_SPLIT], int *const partition_horz4_allowed,
    int *const partition_vert4_allowed, unsigned int pb_source_variance,
    int mi_row, int mi_col) {
  if (best_rd >= 1000000000) return;
  int64_t *horz_rd = rect_part_rd[HORZ];
  int64_t *vert_rd = rect_part_rd[VERT];
  const NN_CONFIG *nn_config = NULL;
  switch (bsize) {
    case BLOCK_16X16: nn_config = &av1_4_partition_nnconfig_16; break;
    case BLOCK_32X32: nn_config = &av1_4_partition_nnconfig_32; break;
    case BLOCK_64X64: nn_config = &av1_4_partition_nnconfig_64; break;
    default: assert(0 && "Unexpected bsize.");
  }
  if (!nn_config) return;

  aom_clear_system_state();

  // Generate features.
  float features[FEATURES];
  int feature_index = 0;
  features[feature_index++] = (float)part_ctx;
  features[feature_index++] = (float)get_unsigned_bits(pb_source_variance);

  const int rdcost = (int)AOMMIN(INT_MAX, best_rd);
  int sub_block_rdcost[8] = { 0 };
  int rd_index = 0;
  for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
    if (horz_rd[i] > 0 && horz_rd[i] < 1000000000)
      sub_block_rdcost[rd_index] = (int)horz_rd[i];
    ++rd_index;
  }
  for (int i = 0; i < SUB_PARTITIONS_RECT; ++i) {
    if (vert_rd[i] > 0 && vert_rd[i] < 1000000000)
      sub_block_rdcost[rd_index] = (int)vert_rd[i];
    ++rd_index;
  }
  for (int i = 0; i < SUB_PARTITIONS_SPLIT; ++i) {
    if (split_rd[i] > 0 && split_rd[i] < 1000000000)
      sub_block_rdcost[rd_index] = (int)split_rd[i];
    ++rd_index;
  }
  for (int i = 0; i < 8; ++i) {
    // Ratio between the sub-block RD and the whole-block RD.
    float rd_ratio = 1.0f;
    if (sub_block_rdcost[i] > 0 && sub_block_rdcost[i] < rdcost)
      rd_ratio = (float)sub_block_rdcost[i] / (float)rdcost;
    features[feature_index++] = rd_ratio;
  }

  // Get variance of the 1:4 and 4:1 sub-blocks.
  unsigned int horz_4_source_var[SUB_PARTITIONS_PART4] = { 0 };
  unsigned int vert_4_source_var[SUB_PARTITIONS_PART4] = { 0 };
  {
#if CONFIG_EXT_RECUR_PARTITIONS
    BLOCK_SIZE horz_4_bs = get_partition_subsize(bsize, PARTITION_HORZ_3);
    BLOCK_SIZE vert_4_bs = get_partition_subsize(bsize, PARTITION_VERT_3);
#else   // CONFIG_EXT_RECUR_PARTITIONS
    BLOCK_SIZE horz_4_bs = get_partition_subsize(bsize, PARTITION_HORZ_4);
    BLOCK_SIZE vert_4_bs = get_partition_subsize(bsize, PARTITION_VERT_4);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    av1_setup_src_planes(x, cpi->source, mi_row, mi_col,
                         av1_num_planes(&cpi->common), NULL);
    const int src_stride = x->plane[0].src.stride;
    uint16_t *src = x->plane[0].src.buf;
    const MACROBLOCKD *const xd = &x->e_mbd;

    struct buf_2d horz_4_src, vert_4_src;
    horz_4_src.stride = src_stride;
    vert_4_src.stride = src_stride;

    for (int i = 0; i < SUB_PARTITIONS_PART4; ++i) {
      horz_4_src.buf = src + i * block_size_high[horz_4_bs] * src_stride;
      vert_4_src.buf = src + i * block_size_wide[vert_4_bs];

      horz_4_source_var[i] = av1_high_get_sby_perpixel_variance(
          cpi, &horz_4_src, horz_4_bs, xd->bd);
      vert_4_source_var[i] = av1_high_get_sby_perpixel_variance(
          cpi, &vert_4_src, vert_4_bs, xd->bd);
    }
  }

  const float denom = (float)(pb_source_variance + 1);
  const float low_b = 0.1f;
  const float high_b = 10.0f;
  for (int i = 0; i < SUB_PARTITIONS_PART4; ++i) {
    // Ratio between the 4:1 sub-block variance and the whole-block variance.
    float var_ratio = (float)(horz_4_source_var[i] + 1) / denom;
    if (var_ratio < low_b) var_ratio = low_b;
    if (var_ratio > high_b) var_ratio = high_b;
    features[feature_index++] = var_ratio;
  }
  for (int i = 0; i < SUB_PARTITIONS_PART4; ++i) {
    // Ratio between the 1:4 sub-block RD and the whole-block RD.
    float var_ratio = (float)(vert_4_source_var[i] + 1) / denom;
    if (var_ratio < low_b) var_ratio = low_b;
    if (var_ratio > high_b) var_ratio = high_b;
    features[feature_index++] = var_ratio;
  }
  assert(feature_index == FEATURES);

  // Calculate scores using the NN model.
  float score[LABELS] = { 0.0f };
  av1_nn_predict(features, nn_config, 1, score);
  aom_clear_system_state();
  int int_score[LABELS];
  int max_score = -1000;
  for (int i = 0; i < LABELS; ++i) {
    int_score[i] = (int)(100 * score[i]);
    max_score = AOMMAX(int_score[i], max_score);
  }

  // Make decisions based on the model scores.
  int thresh = max_score;
  switch (bsize) {
    case BLOCK_16X16: thresh -= 500; break;
    case BLOCK_32X32: thresh -= 500; break;
    case BLOCK_64X64: thresh -= 200; break;
    default: break;
  }
  *partition_horz4_allowed = 0;
  *partition_vert4_allowed = 0;
  for (int i = 0; i < LABELS; ++i) {
    if (int_score[i] >= thresh) {
      if ((i >> 0) & 1) *partition_horz4_allowed = 1;
      if ((i >> 1) & 1) *partition_vert4_allowed = 1;
    }
  }
}
#undef FEATURES
#undef LABELS

#endif  // !CONFIG_EXT_RECUR_PARTITIONS

#define FEATURES 4
int av1_ml_predict_breakout(const AV1_COMP *const cpi, BLOCK_SIZE bsize,
                            const MACROBLOCK *const x,
                            const RD_STATS *const rd_stats,
                            unsigned int pb_source_variance) {
  const NN_CONFIG *nn_config = NULL;
  int thresh = 0;
  switch (bsize) {
    case BLOCK_8X8:
      nn_config = &av1_partition_breakout_nnconfig_8;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[0];
      break;
    case BLOCK_16X16:
      nn_config = &av1_partition_breakout_nnconfig_16;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[1];
      break;
    case BLOCK_32X32:
      nn_config = &av1_partition_breakout_nnconfig_32;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[2];
      break;
    case BLOCK_64X64:
      nn_config = &av1_partition_breakout_nnconfig_64;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[3];
      break;
    case BLOCK_128X128:
      nn_config = &av1_partition_breakout_nnconfig_128;
      thresh = cpi->sf.part_sf.ml_partition_search_breakout_thresh[4];
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
    case BLOCK_256X256: return 0; break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    default: assert(0 && "Unexpected bsize.");
  }
  if (!nn_config || thresh < 0) return 0;

  // Generate feature values.
  float features[FEATURES];
  int feature_index = 0;
  aom_clear_system_state();

  const int num_pels_log2 = num_pels_log2_lookup[bsize];
  float rate_f = (float)AOMMIN(rd_stats->rate, INT_MAX);
  rate_f = ((float)x->rdmult / 128.0f / 512.0f / (float)(1 << num_pels_log2)) *
           rate_f;
  features[feature_index++] = rate_f;

  const float dist_f =
      (float)(AOMMIN(rd_stats->dist, INT_MAX) >> num_pels_log2);
  features[feature_index++] = dist_f;

  features[feature_index++] = (float)pb_source_variance;

  const int dc_q = (int)x->plane[0].dequant_QTX[0];
  features[feature_index++] =
      ((float)dc_q * (float)dc_q) / (256 << (2 * QUANT_TABLE_BITS));

  assert(feature_index == FEATURES);

  // Calculate score using the NN model.
  float score = 0.0f;
  av1_nn_predict(features, nn_config, 1, &score);
  aom_clear_system_state();

  // Make decision.
  return (int)(score * 100) >= thresh;
}
#undef FEATURES

void av1_prune_partitions_before_search(
    AV1_COMP *const cpi, MACROBLOCK *const x, int mi_row, int mi_col,
    BLOCK_SIZE bsize, SIMPLE_MOTION_DATA_TREE *const sms_tree,
    int *partition_none_allowed, int *partition_horz_allowed,
    int *partition_vert_allowed, int *do_rectangular_split,
    int *do_square_split, bool *prune_horz, bool *prune_vert,
    const PC_TREE *pc_tree) {
#if !CONFIG_EXT_RECUR_PARTITIONS
  (void)pc_tree;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  const AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCKD *const xd = &x->e_mbd;

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
    av1_intra_mode_cnn_partition(
        &cpi->common, x, bsize, x->part_search_info.quad_tree_idx,
        partition_none_allowed, partition_horz_allowed, partition_vert_allowed,
        do_rectangular_split, do_square_split);
  }

  // Use simple motion search to prune out split or non-split partitions. This
  // must be done prior to PARTITION_SPLIT to propagate the initial mvs to a
  // smaller blocksize.
  // TODO (any) : Train the ML model for 'BLOCK_256X256' to enable optimization
  // for NONE partition.
  const int try_split_only =
      !cpi->is_screen_content_type &&
      cpi->sf.part_sf.simple_motion_search_split && *do_square_split &&
      bsize >= BLOCK_8X8 &&
      mi_row + mi_size_high[bsize] <= mi_params->mi_rows &&
#if CONFIG_EXT_RECUR_PARTITIONS
      bsize < BLOCK_256X256 &&
#endif
      mi_col + mi_size_wide[bsize] <= mi_params->mi_cols &&
      !frame_is_intra_only(cm) && !av1_superres_scaled(cm) &&
      is_square_block(bsize) && sms_tree && *partition_none_allowed;

  if (try_split_only) {
    av1_simple_motion_search_based_split(
        cpi, x, sms_tree, mi_row, mi_col, bsize, partition_none_allowed,
        partition_horz_allowed, partition_vert_allowed, do_rectangular_split,
        do_square_split);
#if CONFIG_EXT_RECUR_PARTITIONS
    if (!*partition_none_allowed) {
      av1_cache_best_partition(x->sms_bufs, mi_row, mi_col, bsize, cm->sb_size,
                               PARTITION_HORZ);
      const int mi_step = block_size_high[bsize] / 2;
      BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_HORZ);
      av1_cache_best_partition(x->sms_bufs, mi_row, mi_col, subsize,
                               cm->sb_size, PARTITION_VERT);
      av1_cache_best_partition(x->sms_bufs, mi_row + mi_step, mi_col, subsize,
                               cm->sb_size, PARTITION_VERT);
    }
    (void)pc_tree;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  }

  // Use simple motion search to prune out rectangular partition in some
  // direction. The results are stored in prune_horz and prune_vert in order to
  // bypass future related pruning checks if a pruning decision has been made.
  const int try_prune_rect =
      !cpi->is_screen_content_type &&
      cpi->sf.part_sf.simple_motion_search_prune_rect &&
      !frame_is_intra_only(cm) && *do_rectangular_split &&
      (*do_square_split || *partition_none_allowed ||
       (*prune_horz && *prune_vert)) &&
      (*partition_horz_allowed || *partition_vert_allowed) &&
      bsize >= BLOCK_8X8;

  if (try_prune_rect) {
    av1_simple_motion_search_prune_rect(
        cpi, x, sms_tree, mi_row, mi_col, bsize, *partition_horz_allowed,
        *partition_vert_allowed, prune_horz, prune_vert);
  }
}

void av1_prune_partitions_by_max_min_bsize(
    SuperBlockEnc *sb_enc, BLOCK_SIZE bsize, int is_not_edge_block,
    int *partition_none_allowed, int *partition_horz_allowed,
    int *partition_vert_allowed, int *do_square_split) {
  assert(is_bsize_square(sb_enc->max_partition_size));
  assert(is_bsize_square(sb_enc->min_partition_size));
  assert(sb_enc->min_partition_size <= sb_enc->max_partition_size);
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(is_bsize_square(bsize));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  const int max_partition_size_1d = block_size_wide[sb_enc->max_partition_size];

#if CONFIG_EXT_RECUR_PARTITIONS
  assert(is_bsize_geq(sb_enc->max_partition_size, sb_enc->min_partition_size));
  const int block_height = block_size_high[bsize];
  const int block_width = block_size_wide[bsize];
  const int is_le_min_sq_part = is_bsize_geq(sb_enc->min_partition_size, bsize);
  const int is_gt_max_sq_part = (block_height > max_partition_size_1d) ||
                                (block_width > max_partition_size_1d);
  (void)do_square_split;
  (void)is_not_edge_block;
#else   // CONFIG_EXT_RECUR_PARTITIONS
  const int min_partition_size_1d = block_size_wide[sb_enc->min_partition_size];
  const int bsize_1d = block_size_wide[bsize];
  const int is_le_min_sq_part = bsize_1d <= min_partition_size_1d;
  const int is_gt_max_sq_part = bsize_1d > max_partition_size_1d;
  assert(min_partition_size_1d <= max_partition_size_1d);
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  if (is_gt_max_sq_part) {
    // If current block size is larger than max, only allow split.
    *partition_none_allowed = 0;
#if CONFIG_EXT_RECUR_PARTITIONS
    *partition_horz_allowed = 1;
    *partition_vert_allowed = 1;
#else   // CONFIG_EXT_RECUR_PARTITIONS
    *partition_horz_allowed = 0;
    *partition_vert_allowed = 0;
    *do_square_split = 1;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  } else if (is_le_min_sq_part) {
    // If current block size is less or equal to min, only allow none if valid
    // block large enough; only allow split otherwise.
    *partition_horz_allowed = 0;
    *partition_vert_allowed = 0;
    // only disable square split when current block is not at the picture
    // boundary. otherwise, inherit the square split flag from previous logic
#if CONFIG_EXT_RECUR_PARTITIONS
    *partition_none_allowed = 1;
#else   // CONFIG_EXT_RECUR_PARTITIONS
    if (is_not_edge_block) *do_square_split = 0;
    *partition_none_allowed = !(*do_square_split);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  }
}

// Decide whether to evaluate the AB partition specified by part_type based on
// split and HORZ/VERT info
int evaluate_ab_partition_based_on_split(
    const PC_TREE *pc_tree, PARTITION_TYPE rect_part,
    const RD_RECT_PART_WIN_INFO *rect_part_win_info, int qindex, int split_idx1,
    int split_idx2) {
  int num_win = 0;
  // Threshold for number of winners
  // Conservative pruning for high quantizers
  const int num_win_thresh = AOMMIN(3 * (2 * (MAXQ - qindex) / MAXQ), 3);
  int sub_part_win =
      (rect_part_win_info == NULL)    ? (pc_tree->partitioning == rect_part)
      : (rect_part == PARTITION_HORZ) ? rect_part_win_info->rect_part_win[HORZ]
                                      : rect_part_win_info->rect_part_win[VERT];
  num_win += (sub_part_win) ? 1 : 0;
#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
  if (pc_tree->split[cur_region_type][split_idx1]) {
    num_win += (pc_tree->split[cur_region_type][split_idx1]->partitioning ==
                PARTITION_NONE);
  } else {
    num_win += 1;
  }
  if (pc_tree->split[cur_region_type][split_idx2]) {
    num_win += (pc_tree->split[cur_region_type][split_idx2]->partitioning ==
                PARTITION_NONE);
#else
  if (pc_tree->split[split_idx1]) {
    num_win +=
        (pc_tree->split[split_idx1]->partitioning == PARTITION_NONE) ? 1 : 0;
  } else {
    num_win += 1;
  }
  if (pc_tree->split[split_idx2]) {
    num_win +=
        (pc_tree->split[split_idx2]->partitioning == PARTITION_NONE) ? 1 : 0;
#endif  // CONFIG_EXTENDED_SDP
  } else {
    num_win += 1;
  }
  if (num_win < num_win_thresh) {
    return 0;
  }
  return 1;
}

void av1_prune_ab_partitions(
    const AV1_COMP *cpi, const MACROBLOCK *x, const PC_TREE *pc_tree,
    BLOCK_SIZE bsize, int pb_source_variance, int64_t best_rdcost,
    int64_t rect_part_rd[NUM_RECT_PARTS][SUB_PARTITIONS_RECT],
    int64_t split_rd[SUB_PARTITIONS_SPLIT],
    const RD_RECT_PART_WIN_INFO *rect_part_win_info, int ext_partition_allowed,
    int partition_horz_allowed, int partition_vert_allowed,
    int *horza_partition_allowed, int *horzb_partition_allowed,
    int *verta_partition_allowed, int *vertb_partition_allowed) {
  int64_t *horz_rd = rect_part_rd[HORZ];
  int64_t *vert_rd = rect_part_rd[VERT];
  const PartitionCfg *const part_cfg = &cpi->oxcf.part_cfg;
  // The standard AB partitions are allowed initially if ext-partition-types are
  // allowed.
  const MACROBLOCKD *xd = &x->e_mbd;
  int horzab_partition_allowed =
      ext_partition_allowed & part_cfg->enable_ab_partitions &&
      (xd->tree_type != CHROMA_PART || bsize > BLOCK_8X8);
  int vertab_partition_allowed =
      ext_partition_allowed & part_cfg->enable_ab_partitions &&
      (xd->tree_type != CHROMA_PART || bsize > BLOCK_8X8);

  // Pruning: pruning out AB partitions on one main direction based on the
  // current best partition and source variance.
  if (cpi->sf.part_sf.prune_ext_partition_types_search_level) {
    if (cpi->sf.part_sf.prune_ext_partition_types_search_level == 1) {
      // TODO(debargha,huisu@google.com): may need to tune the threshold for
      // pb_source_variance.
      horzab_partition_allowed &= (pc_tree->partitioning == PARTITION_HORZ ||
                                   (pc_tree->partitioning == PARTITION_NONE &&
                                    pb_source_variance < 32) ||
                                   pc_tree->partitioning == PARTITION_SPLIT);
      vertab_partition_allowed &= (pc_tree->partitioning == PARTITION_VERT ||
                                   (pc_tree->partitioning == PARTITION_NONE &&
                                    pb_source_variance < 32) ||
                                   pc_tree->partitioning == PARTITION_SPLIT);
    } else {
      horzab_partition_allowed &= (pc_tree->partitioning == PARTITION_HORZ ||
                                   pc_tree->partitioning == PARTITION_SPLIT);
      vertab_partition_allowed &= (pc_tree->partitioning == PARTITION_VERT ||
                                   pc_tree->partitioning == PARTITION_SPLIT);
    }
    horz_rd[0] = (horz_rd[0] < INT64_MAX ? horz_rd[0] : 0);
    horz_rd[1] = (horz_rd[1] < INT64_MAX ? horz_rd[1] : 0);
    vert_rd[0] = (vert_rd[0] < INT64_MAX ? vert_rd[0] : 0);
    vert_rd[1] = (vert_rd[1] < INT64_MAX ? vert_rd[1] : 0);
    split_rd[0] = (split_rd[0] < INT64_MAX ? split_rd[0] : 0);
    split_rd[1] = (split_rd[1] < INT64_MAX ? split_rd[1] : 0);
    split_rd[2] = (split_rd[2] < INT64_MAX ? split_rd[2] : 0);
    split_rd[3] = (split_rd[3] < INT64_MAX ? split_rd[3] : 0);
  }

  // Pruning: pruning out horz_a or horz_b if the combined rdcost of its
  // subblocks estimated from previous partitions is much higher than the best
  // rd so far.
  *horza_partition_allowed = horzab_partition_allowed;
  *horzb_partition_allowed = horzab_partition_allowed;
  if (cpi->sf.part_sf.prune_ext_partition_types_search_level) {
    const int64_t horz_a_rd = horz_rd[1] + split_rd[0] + split_rd[1];
    const int64_t horz_b_rd = horz_rd[0] + split_rd[2] + split_rd[3];
    switch (cpi->sf.part_sf.prune_ext_partition_types_search_level) {
      case 1:
        *horza_partition_allowed &= (horz_a_rd / 16 * 14 < best_rdcost);
        *horzb_partition_allowed &= (horz_b_rd / 16 * 14 < best_rdcost);
        break;
      case 2:
      default:
        *horza_partition_allowed &= (horz_a_rd / 16 * 15 < best_rdcost);
        *horzb_partition_allowed &= (horz_b_rd / 16 * 15 < best_rdcost);
        break;
    }
  }

  // Pruning: pruning out vert_a or vert_b if the combined rdcost of its
  // subblocks estimated from previous partitions is much higher than the best
  // rd so far.
  *verta_partition_allowed = vertab_partition_allowed;
  *vertb_partition_allowed = vertab_partition_allowed;
  if (cpi->sf.part_sf.prune_ext_partition_types_search_level) {
    const int64_t vert_a_rd = vert_rd[1] + split_rd[0] + split_rd[2];
    const int64_t vert_b_rd = vert_rd[0] + split_rd[1] + split_rd[3];
    switch (cpi->sf.part_sf.prune_ext_partition_types_search_level) {
      case 1:
        *verta_partition_allowed &= (vert_a_rd / 16 * 14 < best_rdcost);
        *vertb_partition_allowed &= (vert_b_rd / 16 * 14 < best_rdcost);
        break;
      case 2:
      default:
        *verta_partition_allowed &= (vert_a_rd / 16 * 15 < best_rdcost);
        *vertb_partition_allowed &= (vert_b_rd / 16 * 15 < best_rdcost);
        break;
    }
  }

  // Pruning: pruning out some ab partitions using a DNN taking rd costs of
  // sub-blocks from previous basic partition types.
  if (cpi->sf.part_sf.ml_prune_ab_partition && ext_partition_allowed &&
      partition_horz_allowed && partition_vert_allowed) {
    // TODO(huisu@google.com): x->source_variance may not be the current
    // block's variance. The correct one to use is pb_source_variance. Need to
    // re-train the model to fix it.
    av1_ml_prune_ab_partition(bsize, pc_tree->partitioning,
                              get_unsigned_bits(x->source_variance),
                              best_rdcost, horz_rd, vert_rd, split_rd,
                              horza_partition_allowed, horzb_partition_allowed,
                              verta_partition_allowed, vertb_partition_allowed);
  }

  // Disable ab partitions if they are disabled by the encoder parameter.
  *horza_partition_allowed &= part_cfg->enable_ab_partitions;
  *horzb_partition_allowed &= part_cfg->enable_ab_partitions;
  *verta_partition_allowed &= part_cfg->enable_ab_partitions;
  *vertb_partition_allowed &= part_cfg->enable_ab_partitions;

  // Pruning: pruning AB partitions based on the number of horz/vert wins
  // in the current block and sub-blocks in PARTITION_SPLIT.
  if (cpi->sf.part_sf.prune_ab_partition_using_split_info &&
      *horza_partition_allowed) {
    *horza_partition_allowed &= evaluate_ab_partition_based_on_split(
        pc_tree, PARTITION_HORZ, rect_part_win_info, x->qindex, 0, 1);
  }
  if (cpi->sf.part_sf.prune_ab_partition_using_split_info &&
      *horzb_partition_allowed) {
    *horzb_partition_allowed &= evaluate_ab_partition_based_on_split(
        pc_tree, PARTITION_HORZ, rect_part_win_info, x->qindex, 2, 3);
  }
  if (cpi->sf.part_sf.prune_ab_partition_using_split_info &&
      *verta_partition_allowed) {
    *verta_partition_allowed &= evaluate_ab_partition_based_on_split(
        pc_tree, PARTITION_VERT, rect_part_win_info, x->qindex, 0, 2);
  }
  if (cpi->sf.part_sf.prune_ab_partition_using_split_info &&
      *vertb_partition_allowed) {
    *vertb_partition_allowed &= evaluate_ab_partition_based_on_split(
        pc_tree, PARTITION_VERT, rect_part_win_info, x->qindex, 1, 3);
  }
}

#if CONFIG_EXT_RECUR_PARTITIONS
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

#define MAKE_SMS_ARR_SWITCH_CASE(width, height) \
  case BLOCK_##width##X##height: {              \
    return sms_bufs->b_##width##x##height;      \
  }

// Returns the buffer in SimpleMotionDataBufs that correspond to bsize.
static INLINE SimpleMotionData *get_sms_arr(SimpleMotionDataBufs *sms_bufs,
                                            BLOCK_SIZE bsize) {
  switch (bsize) {
    // Square blocks
    MAKE_SMS_ARR_SWITCH_CASE(256, 256);
    MAKE_SMS_ARR_SWITCH_CASE(128, 128);
    MAKE_SMS_ARR_SWITCH_CASE(64, 64);
    MAKE_SMS_ARR_SWITCH_CASE(32, 32);
    MAKE_SMS_ARR_SWITCH_CASE(16, 16);
    MAKE_SMS_ARR_SWITCH_CASE(8, 8);
    MAKE_SMS_ARR_SWITCH_CASE(4, 4);

    // 1:2 blocks
    MAKE_SMS_ARR_SWITCH_CASE(128, 256);
    MAKE_SMS_ARR_SWITCH_CASE(64, 128);
    MAKE_SMS_ARR_SWITCH_CASE(32, 64);
    MAKE_SMS_ARR_SWITCH_CASE(16, 32);
    MAKE_SMS_ARR_SWITCH_CASE(8, 16);
    MAKE_SMS_ARR_SWITCH_CASE(4, 8);

    // 2:1 blocks
    MAKE_SMS_ARR_SWITCH_CASE(256, 128);
    MAKE_SMS_ARR_SWITCH_CASE(128, 64);
    MAKE_SMS_ARR_SWITCH_CASE(64, 32);
    MAKE_SMS_ARR_SWITCH_CASE(32, 16);
    MAKE_SMS_ARR_SWITCH_CASE(16, 8);
    MAKE_SMS_ARR_SWITCH_CASE(8, 4);

    // 1:4 blocks
    MAKE_SMS_ARR_SWITCH_CASE(16, 64);
    MAKE_SMS_ARR_SWITCH_CASE(8, 32);
    MAKE_SMS_ARR_SWITCH_CASE(4, 16);

    // 4:1 blocks
    MAKE_SMS_ARR_SWITCH_CASE(64, 16);
    MAKE_SMS_ARR_SWITCH_CASE(32, 8);
    MAKE_SMS_ARR_SWITCH_CASE(16, 4);

    // 1:8 blocks
    MAKE_SMS_ARR_SWITCH_CASE(8, 64);
    MAKE_SMS_ARR_SWITCH_CASE(4, 32);

    // 8:1 blocks
    MAKE_SMS_ARR_SWITCH_CASE(64, 8);
    MAKE_SMS_ARR_SWITCH_CASE(32, 4);

    // 16:1 blocks
    MAKE_SMS_ARR_SWITCH_CASE(64, 4);

    // 1:16 blocks
    MAKE_SMS_ARR_SWITCH_CASE(4, 64);

    default: assert(0 && "Invalid bsize"); return NULL;
  }
}
#undef MAKE_SMS_ARR_SWITCH_CASE

void av1_reset_prev_partition(SimpleMotionDataBufs *sms_bufs) {
  for (BLOCK_SIZE bsize = BLOCK_4X4; bsize < BLOCK_SIZES_ALL; bsize++) {
    SimpleMotionData *sms_arr = get_sms_arr(sms_bufs, bsize);
    const int mi_wide = mi_size_wide[bsize];
    const int mi_high = mi_size_high[bsize];
    const int sms_wide = get_sms_count_from_length(mi_wide);
    const int sms_high = get_sms_count_from_length(mi_high);
    const int sms_count = sms_wide * sms_high;
    for (int idx = 0; idx < sms_count; idx++) {
      sms_arr[idx].has_prev_partition = false;
    }
  }
}
// Retrieves the SimpleMotionData from SimpleMotionDataBufs
SimpleMotionData *av1_get_sms_data_entry(SimpleMotionDataBufs *sms_bufs,
                                         int mi_row, int mi_col,
                                         BLOCK_SIZE bsize, BLOCK_SIZE sb_size) {
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
  SimpleMotionData *sms_arr = get_sms_arr(sms_bufs, bsize);
  return &sms_arr[idx_row_in_sb * arr_stride + idx_col_in_sb];
}

void av1_cache_best_partition(SimpleMotionDataBufs *sms_bufs, int mi_row,
                              int mi_col, BLOCK_SIZE bsize, BLOCK_SIZE sb_size,
                              PARTITION_TYPE partition) {
  SimpleMotionData *cur_block =
      av1_get_sms_data_entry(sms_bufs, mi_row, mi_col, bsize, sb_size);
  cur_block->has_prev_partition = 1;
  cur_block->prev_partition = partition;
}

#if CONFIG_ML_PART_SPLIT
static void compute_residual_stats(AV1_COMP *const cpi, ThreadData *td,
                                   MACROBLOCK *x, BLOCK_SIZE bsize,
                                   ResidualStats *out);
#endif  // CONFIG_ML_PART_SPLIT

// Performs a simple motion search and store the result in sms_data.
static void compute_sms_data(AV1_COMP *const cpi, const TileInfo *const tile,
                             MACROBLOCK *x, SimpleMotionData *sms_data,
                             int mi_row, int mi_col, BLOCK_SIZE bsize
#if CONFIG_ML_PART_SPLIT
                             ,
                             ThreadData *td, bool need_residual_stats
#endif  // CONFIG_ML_PART_SPLIT
) {
  const AV1_COMMON *const cm = &cpi->common;
  const int ref_frame = get_closest_pastcur_ref_index(cm);
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
    return;
  }
  set_offsets_for_motion_search(cpi, x, mi_row, mi_col, bsize);
  //  We need to update the rd-mult here to in case we are doing simple motion
  //  search on a subblock of the current coding block.
  const int orig_rdmult = x->rdmult;
  const AQ_MODE aq_mode = cpi->oxcf.q_cfg.aq_mode;
  MB_MODE_INFO *mbmi = x->e_mbd.mi[0];
  mbmi->mode = NEWMV;
  mbmi->refinemv_flag = 0;
  mbmi->pb_mv_precision = MV_PRECISION_ONE_EIGHTH_PEL;
  mbmi->warpmv_with_mvd_flag = 0;
  mbmi->sb_type[0] = mbmi->sb_type[1] = bsize;
  mbmi->chroma_ref_info.bsize_base = bsize;
  mbmi->chroma_ref_info.is_chroma_ref = 1;

  setup_block_rdmult(cpi, x, mi_row, mi_col, bsize, aq_mode, mbmi);
  // Set error per bit for current rdmult
  av1_set_error_per_bit(&x->mv_costs, x->rdmult);
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
      av1_simple_motion_search_ext(cpi, tile, x, mi_row, mi_col, bsize,
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
  const int eighth_step_h = block_size_high[bsize] / 8;
  const int eighth_step_w = block_size_wide[bsize] / 8;
  static const int subblock_count[ALL_PARTITION_TYPES] = {
    1,  // PARTITION_NONE
    2,  // PARTITION_HORZ
    2,  // PARTITION_VERT
    4,  // PARTITION_HORZ_3
    4,  // PARTITION_VERT_3
    4,  // PARTITION_HORZ_4A
    4,  // PARTITION_HORZ_4B
    4,  // PARTITION_VERT_4A
    4,  // PARTITION_VERT_4B
    4,  // PARTITION_SPLIT
  };
  // PARTITION x NUM_SUBBLOCKS x (ROW and COL)
  static const int step_multiplier[ALL_PARTITION_TYPES][4][2] = {
    { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },  // PARTITION_NONE
    { { 0, 0 }, { 4, 0 }, { 0, 0 }, { 0, 0 } },  // PARTITION_HORZ
    { { 0, 0 }, { 0, 4 }, { 0, 0 }, { 0, 0 } },  // PARTITION_VERT
    { { 0, 0 }, { 2, 0 }, { 2, 4 }, { 6, 0 } },  // PARTITION_HORZ_3
    { { 0, 0 }, { 0, 2 }, { 4, 2 }, { 0, 6 } },  // PARTITION_VERT_3
    { { 0, 0 }, { 1, 0 }, { 3, 0 }, { 7, 0 } },  // PARTITION_HORZ_4A
    { { 0, 0 }, { 1, 0 }, { 5, 0 }, { 7, 0 } },  // PARTITION_HORZ_4B
    { { 0, 0 }, { 0, 1 }, { 0, 3 }, { 0, 7 } },  // PARTITION_VERT_4A
    { { 0, 0 }, { 0, 1 }, { 0, 5 }, { 0, 7 } },  // PARTITION_VERT_4B
    { { 0, 0 }, { 0, 4 }, { 4, 0 }, { 4, 4 } },  // PARTITION_SPLIT
  };

  // Sizes of subblocks.
  const BLOCK_SIZE part_subsize = get_partition_subsize(bsize, partition);
  if (part_subsize == BLOCK_INVALID) return;

  BLOCK_SIZE subsizes[4] = { part_subsize, part_subsize, part_subsize,
                             part_subsize };
  if (partition == PARTITION_HORZ_4A) {
    subsizes[2] = get_partition_subsize(bsize, PARTITION_HORZ);
    subsizes[1] = get_partition_subsize(subsizes[2], PARTITION_HORZ);
  } else if (partition == PARTITION_HORZ_4B) {
    subsizes[1] = get_partition_subsize(bsize, PARTITION_HORZ);
    subsizes[2] = get_partition_subsize(subsizes[1], PARTITION_HORZ);
  } else if (partition == PARTITION_VERT_4A) {
    subsizes[2] = get_partition_subsize(bsize, PARTITION_VERT);
    subsizes[1] = get_partition_subsize(subsizes[2], PARTITION_VERT);
  } else if (partition == PARTITION_VERT_4B) {
    subsizes[1] = get_partition_subsize(bsize, PARTITION_VERT);
    subsizes[2] = get_partition_subsize(subsizes[1], PARTITION_VERT);
  }
  if (partition == PARTITION_HORZ_3 || partition == PARTITION_VERT_3) {
    for (int idx = 0; idx < subblock_count[partition]; idx++) {
      subsizes[idx] = get_h_partition_subsize(bsize, idx, partition);
    }
  }

  for (int idx = 0; idx < subblock_count[partition]; idx++) {
    assert(subsizes[idx] != BLOCK_INVALID);
    const int sub_row =
        mi_row + step_multiplier[partition][idx][0] * eighth_step_h / 4;
    const int sub_col =
        mi_col + step_multiplier[partition][idx][1] * eighth_step_w / 4;
    SimpleMotionData *subblock = av1_get_sms_data_entry(
        sms_bufs, sub_row, sub_col, subsizes[idx], sb_size);
    add_start_mv_to_block(subblock, start_mv);
  }
}

// Computes and stores the simple motion search data for the block at mi_row,
// mi_col with block size bsize.
SimpleMotionData *av1_get_sms_data(AV1_COMP *const cpi,
                                   const TileInfo *const tile, MACROBLOCK *x,
                                   int mi_row, int mi_col, BLOCK_SIZE bsize
#if CONFIG_ML_PART_SPLIT
                                   ,
                                   ThreadData *td, bool need_residual_stats
#endif  // CONFIG_ML_PART_SPLIT
) {
  const AV1_COMMON *const cm = &cpi->common;
  const BLOCK_SIZE sb_size = cm->sb_size;
  SimpleMotionDataBufs *sms_bufs = x->sms_bufs;
  SimpleMotionData *cur_block =
      av1_get_sms_data_entry(sms_bufs, mi_row, mi_col, bsize, sb_size);
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

PARTITION_TYPE av1_get_prev_partition(MACROBLOCK *x, int mi_row, int mi_col,
                                      BLOCK_SIZE bsize, BLOCK_SIZE sb_size) {
  SimpleMotionDataBufs *sms_bufs = x->sms_bufs;
  const SimpleMotionData *cur_block =
      av1_get_sms_data_entry(sms_bufs, mi_row, mi_col, bsize, sb_size);
  if (cur_block && cur_block->has_prev_partition) {
    return cur_block->prev_partition;
  } else {
    return PARTITION_INVALID;
  }
}

static AOM_INLINE int64_t clip_rate(const int rate) {
  if (rate == INT_MAX) {
    return av1_cost_symbol(EC_MIN_PROB);
  }
  return rate;
}

void av1_gather_erp_rect_features(
    float *ml_features, AV1_COMP *cpi, MACROBLOCK *x, const TileInfo *tile_info,
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

  const SimpleMotionData *blk_none = av1_get_sms_data(
      cpi, tile_info, x, blk_params->mi_row, blk_params->mi_col, bsize
#if CONFIG_ML_PART_SPLIT
      ,
      NULL, false
#endif  // CONFIG_ML_PART_SPLIT
  );

  const BLOCK_SIZE h_size = get_partition_subsize(bsize, PARTITION_HORZ);
  const SimpleMotionData *blk_h1 =
      h_size != BLOCK_INVALID
          ? av1_get_sms_data(cpi, tile_info, x, mi_pos_rect[HORZ][0][0],
                             mi_pos_rect[HORZ][0][1], h_size
#if CONFIG_ML_PART_SPLIT
                             ,
                             NULL, false
#endif  // CONFIG_ML_PART_SPLIT
                             )
          : NULL;
  const SimpleMotionData *blk_h2 =
      h_size != BLOCK_INVALID
          ? av1_get_sms_data(cpi, tile_info, x, mi_pos_rect[HORZ][1][0],
                             mi_pos_rect[HORZ][1][1], h_size
#if CONFIG_ML_PART_SPLIT
                             ,
                             NULL, false
#endif  // CONFIG_ML_PART_SPLIT
                             )
          : NULL;

  const BLOCK_SIZE v_size = get_partition_subsize(bsize, PARTITION_VERT);
  const SimpleMotionData *blk_v1 =
      v_size != BLOCK_INVALID
          ? av1_get_sms_data(cpi, tile_info, x, mi_pos_rect[VERT][0][0],
                             mi_pos_rect[VERT][0][1], v_size
#if CONFIG_ML_PART_SPLIT
                             ,
                             NULL, false
#endif  // CONFIG_ML_PART_SPLIT
                             )
          : NULL;
  const SimpleMotionData *blk_v2 =
      v_size != BLOCK_INVALID
          ? av1_get_sms_data(cpi, tile_info, x, mi_pos_rect[VERT][1][0],
                             mi_pos_rect[VERT][1][1], v_size
#if CONFIG_ML_PART_SPLIT
                             ,
                             NULL, false
#endif  // CONFIG_ML_PART_SPLIT
                             )
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
#if CONFIG_EXTENDED_SDP
  REGION_TYPE cur_region_type = pc_tree->region_type;
  ml_features[num_features++] =
      parent && (parent->horizontal3[cur_region_type][1] == pc_tree ||
                 parent->horizontal3[cur_region_type][2] == pc_tree);
  ml_features[num_features++] =
      parent && (parent->vertical3[cur_region_type][1] == pc_tree ||
                 parent->vertical3[cur_region_type][2] == pc_tree);
#else
  ml_features[num_features++] = parent && (parent->horizontal3[1] == pc_tree ||
                                           parent->horizontal3[2] == pc_tree);
  ml_features[num_features++] = parent && (parent->vertical3[1] == pc_tree ||
                                           parent->vertical3[2] == pc_tree);
#endif  // CONFIG_EXTENDED_SDP
  assert(num_features == 19);
}

#if CONFIG_ML_PART_SPLIT

enum {
  FEATURE_INTRA_LOG_QP_SQUARED = 0,
  FEATURE_INTRA_HAS_ABOVE,
  FEATURE_INTRA_LOG_ABOVE_WIDTH,
  FEATURE_INTRA_LOG_ABOVE_HEIGHT,
  FEATURE_INTRA_HAS_LEFT,
  FEATURE_INTRA_LOG_LEFT_WIDTH,
  FEATURE_INTRA_LOG_LEFT_HEIGHT,
  FEATURE_INTRA_NORM_BEST_0_SSE,
  FEATURE_INTRA_NORM_BEST_0_VAR,
  FEATURE_INTRA_NORM_BEST_1_SSE,
  FEATURE_INTRA_NORM_BEST_1_VAR,
  FEATURE_INTRA_NORM_BEST_2_SSE,
  FEATURE_INTRA_NORM_BEST_2_VAR,
  FEATURE_INTRA_NORM_BEST_SSE_0_00,
  FEATURE_INTRA_NORM_BEST_VAR_0_00,
  FEATURE_INTRA_NORM_BEST_SSE_0_01,
  FEATURE_INTRA_NORM_BEST_VAR_0_01,
  FEATURE_INTRA_NORM_BEST_SSE_0_10,
  FEATURE_INTRA_NORM_BEST_VAR_0_10,
  FEATURE_INTRA_NORM_BEST_SSE_0_11,
  FEATURE_INTRA_NORM_BEST_VAR_0_11,
  FEATURE_INTRA_NORM_BEST_SSE_1_00,
  FEATURE_INTRA_NORM_BEST_VAR_1_00,
  FEATURE_INTRA_NORM_BEST_SSE_1_01,
  FEATURE_INTRA_NORM_BEST_VAR_1_01,
  FEATURE_INTRA_NORM_BEST_SSE_1_10,
  FEATURE_INTRA_NORM_BEST_VAR_1_10,
  FEATURE_INTRA_NORM_BEST_SSE_1_11,
  FEATURE_INTRA_NORM_BEST_VAR_1_11,
  FEATURE_INTRA_NORM_BEST_SSE_2_00,
  FEATURE_INTRA_NORM_BEST_VAR_2_00,
  FEATURE_INTRA_NORM_BEST_SSE_2_01,
  FEATURE_INTRA_NORM_BEST_VAR_2_01,
  FEATURE_INTRA_NORM_BEST_SSE_2_10,
  FEATURE_INTRA_NORM_BEST_VAR_2_10,
  FEATURE_INTRA_NORM_BEST_SSE_2_11,
  FEATURE_INTRA_NORM_BEST_VAR_2_11,
  FEATURE_INTRA_MAX
};

enum {
  // final_part_prune_inter_bs6_9_qp_110_135_160_nnz_psnr_32_16_tflite_model
  FEATURE_INTER_RD_MULT = 0,
  FEATURE_INTER_FULL_PSNR,
  FEATURE_INTER_FULL_Q_COEFF_MAX,
  FEATURE_INTER_FULL_Q_COEFF_NONZ,
  FEATURE_INTER_SQ_0_PSNR,
  FEATURE_INTER_SQ_0_Q_COEFF_MAX,
  FEATURE_INTER_SQ_0_Q_COEFF_NONZ,
  FEATURE_INTER_SQ_1_PSNR,
  FEATURE_INTER_SQ_1_Q_COEFF_MAX,
  FEATURE_INTER_SQ_1_Q_COEFF_NONZ,
  FEATURE_INTER_SQ_2_PSNR,
  FEATURE_INTER_SQ_2_Q_COEFF_MAX,
  FEATURE_INTER_SQ_2_Q_COEFF_NONZ,
  FEATURE_INTER_SQ_3_PSNR,
  FEATURE_INTER_SQ_3_Q_COEFF_MAX,
  FEATURE_INTER_SQ_3_Q_COEFF_NONZ,

  // final_part_prune_inter_bs3_6_9_12_qp_110_135_160_nnz_psnr_vect_satdq_32_16_tflite_model
  FEATURE_INTER_FULL_LOG_MAG,
  FEATURE_INTER_FULL_ANGLE_RAD,
  FEATURE_INTER_SQ_0_LOG_MAG,
  FEATURE_INTER_SQ_0_ANGLE_RAD,
  FEATURE_INTER_SQ_1_LOG_MAG,
  FEATURE_INTER_SQ_1_ANGLE_RAD,
  FEATURE_INTER_SQ_2_LOG_MAG,
  FEATURE_INTER_SQ_2_ANGLE_RAD,
  FEATURE_INTER_SQ_3_LOG_MAG,
  FEATURE_INTER_SQ_3_ANGLE_RAD,
  //
  FEATURE_INTER_FULL_LOG_SATDQ,
  FEATURE_INTER_SQ_0_LOG_SATDQ,
  FEATURE_INTER_SQ_1_LOG_SATDQ,
  FEATURE_INTER_SQ_2_LOG_SATDQ,
  FEATURE_INTER_SQ_3_LOG_SATDQ,
  FEATURE_INTER_FULL_LOG_SATD,
  FEATURE_INTER_SQ_0_LOG_SATD,
  FEATURE_INTER_SQ_1_LOG_SATD,
  FEATURE_INTER_SQ_2_LOG_SATD,
  FEATURE_INTER_SQ_3_LOG_SATD,

  FEATURE_INTER_MAX
};

#define ZERO_ARRAY(arr) memset(arr, 0, sizeof(arr))

#define MAX_BLK_SIZE (MAX_TX_SIZE << 1)
#define MAX_BLK_SQUARE (MAX_BLK_SIZE * MAX_BLK_SIZE)
#define MAX_TX_RECT (MAX_TX_SIZE * MAX_BLK_SIZE)

static AOM_INLINE void av1_ml_part_split_features_square(AV1_COMP *const cpi,
                                                         MACROBLOCK *x,
                                                         int mi_row, int mi_col,
                                                         BLOCK_SIZE bsize,
                                                         float *out_features) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int w_mi = mi_size_wide[bsize];
  const int h_mi = mi_size_high[bsize];
  DECLARE_ALIGNED(16, uint16_t, intrapred[MAX_TX_SQUARE]);

  // plus top line and left column
  BLOCK_SIZE subsize_sq = get_partition_subsize(
      get_partition_subsize(bsize, PARTITION_HORZ), PARTITION_VERT);
  if (subsize_sq == BLOCK_INVALID) {
    subsize_sq = get_partition_subsize(
        get_partition_subsize(bsize, PARTITION_VERT), PARTITION_HORZ);
  }

  if (subsize_sq != BLOCK_INVALID) {
    const int w_sub_mi = mi_size_wide[subsize_sq];
    const int h_sub_mi = mi_size_high[subsize_sq];
    TX_SIZE tx_sub_size = max_txsize_rect_lookup[subsize_sq];
    unsigned int best_sub_sse[2][2][3] = {
      { { INT_MAX, INT_MAX, INT_MAX }, { INT_MAX, INT_MAX, INT_MAX } },
      { { INT_MAX, INT_MAX, INT_MAX }, { INT_MAX, INT_MAX, INT_MAX } }
    };
    unsigned int best_sub_var[2][2][3] = {
      { { INT_MAX, INT_MAX, INT_MAX }, { INT_MAX, INT_MAX, INT_MAX } },
      { { INT_MAX, INT_MAX, INT_MAX }, { INT_MAX, INT_MAX, INT_MAX } }
    };
    PREDICTION_MODE best_sub_mode[2][2][3] = {
      { { MODE_INVALID, MODE_INVALID, MODE_INVALID },
        { MODE_INVALID, MODE_INVALID, MODE_INVALID } },
      { { MODE_INVALID, MODE_INVALID, MODE_INVALID },
        { MODE_INVALID, MODE_INVALID, MODE_INVALID } }
    };

    for (int row_off = 0, r_idx = 0; row_off < h_mi;
         row_off += h_sub_mi, ++r_idx) {
      int mi_row_left = xd->tile.mi_row_end - mi_row - row_off;
      // Don't process beyond the tile boundary
      if (mi_row_left < 0) break;
      for (int col_off = 0, c_idx = 0; col_off < w_mi;
           col_off += w_sub_mi, ++c_idx) {
        int mi_col_left = xd->tile.mi_col_end - mi_col - col_off;
        // Don't process beyond the tile boundary
        if (mi_col_left < 0) break;
        int src_off = (row_off << 2) * x->plane[0].src.stride + (col_off << 2);
        xd->mb_to_top_edge = (-mi_row - row_off) << MI_SUBPEL_SIZE_LOG2;
        xd->mb_to_left_edge = (-mi_col - col_off) << MI_SUBPEL_SIZE_LOG2;
        mbmi->sb_type[0] = subsize_sq;
        xd->up_available = (mi_row + row_off) > 0;
        xd->left_available = (mi_col + col_off) > 0;

        for (PREDICTION_MODE intra_sub_mode = INTRA_MODE_START;
             intra_sub_mode < INTRA_MODE_END; ++intra_sub_mode) {
          memset(intrapred, 0, sizeof(intrapred));
          xd->up_available = (mi_row + row_off) > 0;
          xd->left_available = (mi_col + col_off) > 0;
          av1_predict_intra_block(
              cm, xd, w_sub_mi << MI_SIZE_LOG2, h_sub_mi << MI_SIZE_LOG2,
              tx_sub_size, intra_sub_mode, 0, 0, FILTER_INTRA_MODES,
              x->plane[0].src.buf + src_off, x->plane[0].src.stride, intrapred,
              MAX_TX_SIZE, 0, 0, 0);

          unsigned int curr_sse = 0, curr_var = 0;
          curr_var = cpi->fn_ptr[txsize_to_bsize[tx_sub_size]].vf(
              x->plane[0].src.buf + src_off, x->plane[0].src.stride, intrapred,
              MAX_TX_SIZE, &curr_sse);
          for (int cand = 0; cand < 3; cand++) {
            if (curr_sse < best_sub_sse[r_idx][c_idx][cand]) {
              for (int s = 2; s > cand; s--) {
                best_sub_sse[r_idx][c_idx][s] =
                    best_sub_sse[r_idx][c_idx][s - 1];
                best_sub_var[r_idx][c_idx][s] =
                    best_sub_var[r_idx][c_idx][s - 1];
                best_sub_mode[r_idx][c_idx][s] =
                    best_sub_mode[r_idx][c_idx][s - 1];
              }
              best_sub_sse[r_idx][c_idx][cand] = curr_sse;
              best_sub_var[r_idx][c_idx][cand] = curr_var;
              best_sub_mode[r_idx][c_idx][cand] = intra_sub_mode;
              break;
            }
          }
        }
        if (out_features) {
          const int sub_area_log2 =
              mi_size_wide_log2[subsize_sq] + mi_size_high_log2[subsize_sq] + 4;
          for (int cand = 0; cand < 3; ++cand) {
            int foff = r_idx * 4 + c_idx * 2 + cand * 8;
            out_features[FEATURE_INTRA_NORM_BEST_SSE_0_00 + foff] = logf(
                1.0f + (best_sub_sse[r_idx][c_idx][cand] >> sub_area_log2));
            out_features[FEATURE_INTRA_NORM_BEST_VAR_0_00 + foff] = logf(
                1.0f + (best_sub_var[r_idx][c_idx][cand] >> sub_area_log2));
          }
        }
      }
    }
  }
}

static AOM_INLINE void av1_ml_part_split_features_none(AV1_COMP *const cpi,
                                                       MACROBLOCK *x,
                                                       int mi_row, int mi_col,
                                                       BLOCK_SIZE bsize,
                                                       float *out_features) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int w_mi = mi_size_wide[bsize];
  const int h_mi = mi_size_high[bsize];

  TX_SIZE tx_size = max_txsize_rect_lookup[bsize];

  unsigned int tx_w = tx_size_wide_unit[tx_size];
  unsigned int tx_h = tx_size_high_unit[tx_size];
  DECLARE_ALIGNED(16, uint16_t, intrapred[MAX_BLK_SQUARE]);

  xd->mb_to_top_edge = -mi_row << MI_SUBPEL_SIZE_LOG2;
  xd->mb_to_left_edge = -mi_col << MI_SUBPEL_SIZE_LOG2;
  mbmi->sb_type[0] = bsize;
  unsigned int best_sse[3] = { INT_MAX, INT_MAX, INT_MAX };
  unsigned int best_var[3] = { 0, 0, 0 };
  PREDICTION_MODE best_mode[3] = { MODE_INVALID, MODE_INVALID, MODE_INVALID };
  for (PREDICTION_MODE intra_mode = INTRA_MODE_START;
       intra_mode < INTRA_MODE_END; ++intra_mode) {
    unsigned int curr_sse = 0, curr_var = 0;
    memset(intrapred, 0, sizeof(intrapred));
    for (int row_off = 0; row_off < h_mi; row_off += tx_h) {
      for (int col_off = 0; col_off < w_mi; col_off += tx_w) {
        int src_off = (row_off << 2) * x->plane[0].src.stride + (col_off << 2);
        int intr_off = (row_off << 2) * MAX_BLK_SIZE + (col_off << 2);
        xd->up_available = (mi_row + row_off) > 0;
        xd->left_available = (mi_col + col_off) > 0;
        av1_predict_intra_block(
            cm, xd, w_mi << MI_SIZE_LOG2, h_mi << MI_SIZE_LOG2, tx_size,
            intra_mode, 0, 0, FILTER_INTRA_MODES, x->plane[0].src.buf + src_off,
            x->plane[0].src.stride, intrapred + intr_off, MAX_BLK_SIZE, 0, 0,
            0);
        unsigned int tmp = 0;
        curr_var += cpi->fn_ptr[txsize_to_bsize[tx_size]].vf(
            x->plane[0].src.buf + src_off, x->plane[0].src.stride,
            intrapred + intr_off, MAX_BLK_SIZE, &tmp);
        curr_sse += tmp;
      }
    }
    for (int cand = 0; cand < 3; cand++) {
      if (curr_sse < best_sse[cand]) {
        for (int s = 2; s > cand; s--) {
          best_sse[s] = best_sse[s - 1];
          best_var[s] = best_var[s - 1];
          best_mode[s] = best_mode[s - 1];
        }
        best_sse[cand] = curr_sse;
        best_var[cand] = curr_var;
        best_mode[cand] = intra_mode;
        break;
      }
    }
  }
  if (out_features) {
    const int blk_area_log2 =
        mi_size_wide_log2[bsize] + mi_size_high_log2[bsize] + 4;
    out_features[FEATURE_INTRA_NORM_BEST_0_SSE] =
        logf(1.0f + (best_sse[0] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_0_VAR] =
        logf(1.0f + (best_var[0] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_1_SSE] =
        logf(1.0f + (best_sse[1] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_1_VAR] =
        logf(1.0f + (best_var[1] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_2_SSE] =
        logf(1.0f + (best_sse[2] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_2_VAR] =
        logf(1.0f + (best_var[2] >> blk_area_log2));
  }
}

static AOM_INLINE void av1_ml_part_split_features(AV1_COMP *const cpi,
                                                  MACROBLOCK *x, int mi_row,
                                                  int mi_col, BLOCK_SIZE bsize,
                                                  float *out_features) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];

  av1_setup_src_planes(x, cpi->source, mi_row, mi_col, 1, NULL);

  if (out_features) {
    // Q_INDEX
    const int dc_q =
        av1_dc_quant_QTX(x->qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
                         xd->bd) >>
        (xd->bd - 8);
    out_features[FEATURE_INTRA_LOG_QP_SQUARED] =
        logf(1.0f + (float)((int64_t)dc_q * (int64_t)dc_q) /
                        (256 << (2 * QUANT_TABLE_BITS)));

    // Neighbor stuff
    const int has_above = !!xd->above_mbmi;
    const int has_left = !!xd->left_mbmi;
    const BLOCK_SIZE above_bsize =
        has_above ? xd->above_mbmi->sb_type[xd->tree_type == CHROMA_PART]
                  : bsize;
    const BLOCK_SIZE left_bsize =
        has_left ? xd->left_mbmi->sb_type[xd->tree_type == CHROMA_PART] : bsize;

    out_features[FEATURE_INTRA_HAS_ABOVE] = (float)has_above;
    out_features[FEATURE_INTRA_LOG_ABOVE_WIDTH] =
        (float)mi_size_wide_log2[above_bsize];
    out_features[FEATURE_INTRA_LOG_ABOVE_HEIGHT] =
        (float)mi_size_high_log2[above_bsize];
    out_features[FEATURE_INTRA_HAS_LEFT] = (float)has_left;
    out_features[FEATURE_INTRA_LOG_LEFT_WIDTH] =
        (float)mi_size_wide_log2[left_bsize];
    out_features[FEATURE_INTRA_LOG_LEFT_HEIGHT] =
        (float)mi_size_high_log2[left_bsize];
  }

  int old1 = xd->mb_to_top_edge;
  int old2 = xd->mb_to_left_edge;
  int old3 = mbmi->sb_type[0];
  int old4 = mbmi->mrl_index;
  mbmi->mrl_index = 0;

  av1_ml_part_split_features_square(cpi, x, mi_row, mi_col, bsize,
                                    out_features);
  av1_ml_part_split_features_none(cpi, x, mi_row, mi_col, bsize, out_features);

  xd->mb_to_top_edge = old1;
  xd->mb_to_left_edge = old2;
  mbmi->sb_type[0] = old3;
  mbmi->mrl_index = old4;

  aom_clear_system_state();
}

static MODEL_TYPE get_model_type(BLOCK_SIZE bsize, bool intra) {
  if (!intra) {
    switch (bsize) {
      case BLOCK_64X64: return MODEL_INTER_64X64;
      case BLOCK_32X32: return MODEL_INTER_32X32;
      case BLOCK_16X16: return MODEL_INTER_16X16;
      case BLOCK_8X8: return MODEL_INTER_8X8;
      default: return MODEL_OTHER;
    }
  } else {
    switch (bsize) {
      case BLOCK_128X128: return MODEL_128X128;
      case BLOCK_64X64: return MODEL_64X64;
      case BLOCK_32X32: return MODEL_32X32;
      case BLOCK_16X16: return MODEL_16X16;
      default: return MODEL_OTHER;
    }
  }
}

static float log_mag(MV mv) {
  double mag = sqrt(mv.col * mv.col + mv.row * mv.row);
  return (float)logl(1.0f + mag);
}

static float angle_rad(MV mv) {
  double mag = sqrt(mv.col * mv.col + mv.row * mv.row);
  return (float)(mag == 0 ? 0 : asin(mv.row / mag));
}

// Computes residual stats on a transformed and quantized residual of the
// block. This is used as ML features for prediction. The information computed
// is NNZ (Number of Non-Zero coefficients of the transformed and quantized
// residual), MAX_COEFF, PSNR.
static void compute_residual_stats(AV1_COMP *const cpi, ThreadData *td,
                                   MACROBLOCK *x, BLOCK_SIZE bsize,
                                   ResidualStats *out) {
  AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  TX_SIZE tx_size = max_txsize_rect_lookup[bsize];
  const int plane = 0;
  const int block = 0;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];

  memset(out, 0, sizeof(ResidualStats));

  av1_subtract_plane(x, bsize, plane);

  const uint16_t *src = x->plane[0].src.buf;
  const uint16_t *dst = xd->plane[0].dst.buf;
  const int src_stride = x->plane[0].src.stride;
  const int dst_stride = xd->plane[0].dst.stride;
  out->var = cpi->fn_ptr[bsize].vf(src, src_stride, dst, dst_stride, &out->sse);

  const int num_blk = mi_size_wide[bsize] * mi_size_high[bsize];
  struct aom_internal_error_info error;
  AOM_CHECK_MEM_ERROR(&error, p->eobs,
                      aom_memalign(32, num_blk * sizeof(p->eobs[0])));
  p->coeff = td->shared_coeff_buf.coeff_buf[plane];
  p->qcoeff = td->shared_coeff_buf.qcoeff_buf[plane];
  p->dqcoeff = td->shared_coeff_buf.dqcoeff_buf[plane];
  tran_low_t *const dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);
  tran_low_t *const qcoeff = p->qcoeff + BLOCK_OFFSET(block);
  tran_low_t *const coeff = p->coeff + BLOCK_OFFSET(block);
  AOM_CHECK_MEM_ERROR(&error, p->bobs,
                      aom_memalign(32, num_blk * sizeof(p->bobs[0])));
  AOM_CHECK_MEM_ERROR(
      &error, p->txb_entropy_ctx,
      aom_memalign(32, num_blk * sizeof(p->txb_entropy_ctx[0])));

  TxfmParam txfm_param;
  QUANT_PARAM quant_param;

  av1_setup_xform(cm, x, plane, tx_size, DCT_DCT, CCTX_NONE, &txfm_param);
  av1_setup_quant(tx_size, 0, AV1_XFORM_QUANT_B, cpi->oxcf.q_cfg.quant_b_adapt,
                  &quant_param);
  av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, DCT_DCT,
                    &quant_param);
  av1_xform_quant(cm, x, plane, block, 0, 0, bsize, &txfm_param, &quant_param);
  const int n_coeffs = av1_get_max_eob(txfm_param.tx_size);
  for (int i = 0; i < n_coeffs; i++) {
    int abs_qcoeff = abs(qcoeff[i]);
    out->satd += abs(coeff[i]);
    out->satdq += abs_qcoeff;
    out->q_coeff_max = AOMMAX(out->q_coeff_max, abs_qcoeff);
    out->q_coeff_nonz += qcoeff[i] != 0;
  }

  if (p->eobs[block]) {
    txfm_param.eob = p->eobs[block];

    av1_highbd_inv_txfm_add(dqcoeff, pd->dst.buf, pd->dst.stride, &txfm_param);
  }
  int sse = 0;
  for (int i = 0; i < block_size_high[bsize]; i++) {
    for (int j = 0; j < block_size_wide[bsize]; j++) {
      int d = pd->dst.buf[i * pd->dst.stride + j] -
              x->plane[plane].src.buf[i * x->plane[plane].src.stride + j];
      sse += d * d;
    }
  }
  double mse =
      ((double)sse) / (block_size_high[bsize] * block_size_wide[bsize]);
  out->psnr = (float)(sse == 0 ? 70 : AOMMIN(70, 20 * log10(255 / sqrt(mse))));

  // TODO: figure out the way to do it w/o allocations
  p->coeff = NULL;
  p->qcoeff = NULL;
  p->dqcoeff = NULL;
  aom_free(p->eobs);
  p->eobs = NULL;
  aom_free(p->bobs);
  p->bobs = NULL;
  aom_free(p->txb_entropy_ctx);
  p->txb_entropy_ctx = NULL;
}

static void blk_features(float *out_features, int o_psnr, int o_log_mag,
                         int o_satdq, int o_satd, SimpleMotionData *sms,
                         int blk_area) {
  assert(sms->residual_stats_valid);
  out_features[o_psnr + 0] = sms->residual_stats.psnr - 35;
  out_features[o_psnr + 1] = ((float)sms->residual_stats.q_coeff_max) / 1024;
  out_features[o_psnr + 2] =
      ((float)sms->residual_stats.q_coeff_nonz) / blk_area;
  out_features[o_log_mag + 0] = log_mag(sms->submv);
  out_features[o_log_mag + 1] = angle_rad(sms->submv);
  out_features[o_satdq] = logf(1.0f + sms->residual_stats.satdq);
  out_features[o_satd] = logf(1.0f + sms->residual_stats.satd);
}

static void av1_ml_part_split_features_inter(AV1_COMP *const cpi, MACROBLOCK *x,
                                             int mi_row, int mi_col,
                                             BLOCK_SIZE bsize,
                                             const TileInfo *tile_info,
                                             ThreadData *td,
                                             float *out_features) {
  if (cpi->common.current_frame.frame_type != INTER_FRAME) return;

  SimpleMotionData *blk_none =
      av1_get_sms_data(cpi, tile_info, x, mi_row, mi_col, bsize, td, true);

  BLOCK_SIZE subsize_sq = get_partition_subsize(
      get_partition_subsize(bsize, PARTITION_HORZ), PARTITION_VERT);
  if (subsize_sq == BLOCK_INVALID) {
    subsize_sq = get_partition_subsize(
        get_partition_subsize(bsize, PARTITION_VERT), PARTITION_HORZ);
  }

  if (subsize_sq != BLOCK_INVALID) {
    int w_sub_mi = mi_size_wide[subsize_sq];
    int h_sub_mi = mi_size_high[subsize_sq];
    SimpleMotionData *blk_sq_0 = av1_get_sms_data(cpi, tile_info, x, mi_row,
                                                  mi_col, subsize_sq, td, true);
    SimpleMotionData *blk_sq_1 = av1_get_sms_data(
        cpi, tile_info, x, mi_row, mi_col + w_sub_mi, subsize_sq, td, true);
    SimpleMotionData *blk_sq_2 = av1_get_sms_data(
        cpi, tile_info, x, mi_row + h_sub_mi, mi_col, subsize_sq, td, true);
    SimpleMotionData *blk_sq_3 =
        av1_get_sms_data(cpi, tile_info, x, mi_row + h_sub_mi,
                         mi_col + w_sub_mi, subsize_sq, td, true);

    if (out_features) {
      int blk_area = block_size_wide[bsize] * block_size_high[bsize];
      out_features[FEATURE_INTER_RD_MULT] = logf(1.0f + blk_none->rdmult);

      blk_features(out_features, FEATURE_INTER_FULL_PSNR,
                   FEATURE_INTER_FULL_LOG_MAG, FEATURE_INTER_FULL_LOG_SATDQ,
                   FEATURE_INTER_FULL_LOG_SATD, blk_none, blk_area);
      blk_features(out_features, FEATURE_INTER_SQ_0_PSNR,
                   FEATURE_INTER_SQ_0_LOG_MAG, FEATURE_INTER_SQ_0_LOG_SATDQ,
                   FEATURE_INTER_SQ_0_LOG_SATD, blk_sq_0, blk_area);
      blk_features(out_features, FEATURE_INTER_SQ_1_PSNR,
                   FEATURE_INTER_SQ_1_LOG_MAG, FEATURE_INTER_SQ_1_LOG_SATDQ,
                   FEATURE_INTER_SQ_1_LOG_SATD, blk_sq_1, blk_area);
      blk_features(out_features, FEATURE_INTER_SQ_2_PSNR,
                   FEATURE_INTER_SQ_2_LOG_MAG, FEATURE_INTER_SQ_2_LOG_SATDQ,
                   FEATURE_INTER_SQ_2_LOG_SATD, blk_sq_2, blk_area);
      blk_features(out_features, FEATURE_INTER_SQ_3_PSNR,
                   FEATURE_INTER_SQ_3_LOG_MAG, FEATURE_INTER_SQ_3_LOG_SATDQ,
                   FEATURE_INTER_SQ_3_LOG_SATD, blk_sq_3, blk_area);
    }
  }

  aom_clear_system_state();
}

int av1_ml_part_split_infer(AV1_COMP *const cpi, MACROBLOCK *x, int mi_row,
                            int mi_col, BLOCK_SIZE bsize,
                            const TileInfo *tile_info, ThreadData *td) {
  const MACROBLOCKD *xd = &x->e_mbd;
  int qp = cpi->common.quant_params.base_qindex;
  bool key_frame = cpi->common.current_frame.frame_type == KEY_FRAME;
  MODEL_TYPE model_type[] = { get_model_type(bsize, true),
                              get_model_type(bsize, false) };

  struct ModelParams params[2];
  for (int i = 0; i < 2; i++) {
    int had_error = model_type[i] != MODEL_OTHER &&
                    av2_part_split_prune_tflite_params(
                        model_type[i],
                        key_frame ? cpi->sf.part_sf.prune_split_ml_level
                                  : cpi->sf.part_sf.prune_split_ml_level_inter,
                        &params[i]);
    assert(!had_error);
    if (had_error) return ML_PART_NOT_SURE;
  }

  const AV1_COMMON *const cm = &cpi->common;
  int qp_offset;
  switch (cm->seq_params.bit_depth) {
    case AOM_BITS_10: qp_offset = qindex_10b_offset[1]; break;
    case AOM_BITS_12: qp_offset = qindex_12b_offset[1]; break;
    default: qp_offset = 0; break;
  }

  bool model_disabled[2] = { false, false };
  for (int i = 0; i < 2; i++) {
    model_disabled[i] = model_type[i] == MODEL_OTHER ||
                        qp > (params[i].qp_high + qp_offset) ||
                        qp < (params[i].qp_low + qp_offset);
  }
  // use intra model only for key frames for now
  model_disabled[0] |= !key_frame;
  model_disabled[1] |= key_frame;
  if (xd->tree_type == CHROMA_PART || (model_disabled[0] && model_disabled[1]))
    return ML_PART_NOT_SURE;

  int vote[2] = { ML_PART_NOT_SURE, ML_PART_NOT_SURE };
  if (!model_disabled[0]) {
    float ml_input[FEATURE_INTRA_MAX] = { 0.0f };
    av1_ml_part_split_features(cpi, x, mi_row, mi_col, bsize, ml_input);

    float ml_output[1] = { 0.0f };

    bool has_error = av2_part_split_prune_tflite_exec(
        &td->partition_model, ml_input, FEATURE_INTRA_MAX, ml_output, 1,
        model_type[0]);
    assert(!has_error);
    if (has_error)
      vote[0] = ML_PART_NOT_SURE;
    else {
      bool high_test = ml_output[0] > params[0].thresh_high;
      bool low_test = ml_output[0] < params[0].thresh_low;
      vote[0] = high_test ? ML_PART_FORCE_SPLIT
                          : (low_test ? ML_PART_PRUNE_SPLIT : ML_PART_NOT_SURE);
    }
  }
  if (!model_disabled[1]) {
    float ml_output[1] = { 0.0f };
    float ml_input[FEATURE_INTER_MAX] = { 0.0f };
    av1_ml_part_split_features_inter(cpi, x, mi_row, mi_col, bsize, tile_info,
                                     td, ml_input);
    bool has_error = av2_part_split_prune_tflite_exec(
        &td->partition_model, ml_input, FEATURE_INTER_MAX, ml_output, 1,
        model_type[1]);
    assert(!has_error);

    if (has_error)
      vote[1] = ML_PART_NOT_SURE;
    else {
      bool high_test = ml_output[0] > params[1].thresh_high;
      bool low_test = ml_output[0] < params[1].thresh_low;
      vote[1] = high_test ? ML_PART_FORCE_SPLIT
                          : (low_test ? ML_PART_PRUNE_SPLIT : ML_PART_NOT_SURE);
    }
  }
  int final_vote = 255;
  if (vote[0] == ML_PART_NOT_SURE)
    final_vote = vote[1];
  else if (vote[1] == ML_PART_NOT_SURE)
    final_vote = vote[0];
  else if (vote[0] == vote[1])
    final_vote = vote[0];
  else
    final_vote = ML_PART_FORCE_SPLIT;

  assert(final_vote != 255);
  return final_vote;
}
#endif  // CONFIG_ML_PART_SPLIT

#endif  // CONFIG_EXT_RECUR_PARTITIONS
