/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"
#include "config/av2_rtcd.h"

#include "av2/common/quant_common.h"

#include "av2/encoder/encodeframe.h"
#include "av2/encoder/encodeframe_utils.h"
#include "av2/encoder/mcomp.h"
#include "av2/encoder/var_based_part.h"
#include "av2/encoder/reconinter_enc.h"
#include "av2/encoder/rdopt_utils.h"

#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/mem.h"

// Macros for common video resolutions: width x height
// For example, 720p represents video resolution of 1280x720 pixels.
#define RESOLUTION_180P (320 * 180)
#define RESOLUTION_240P (320 * 240)
#define RESOLUTION_288P (352 * 288)
#define RESOLUTION_360P (640 * 360)
#define RESOLUTION_480P (640 * 480)
#define RESOLUTION_720P (1280 * 720)
#define RESOLUTION_1080P (1920 * 1080)
#define RESOLUTION_1440P (2560 * 1440)
#define RESOLUTION_4K (3840 * 2160)

// Possible values for the force_split variable while evaluating variance based
// partitioning.
enum {
  // Evaluate all partition types
  PART_EVAL_ALL = 0,
  // Force PARTITION_SPLIT
  PART_EVAL_ONLY_SPLIT = 1,
  // Force PARTITION_NONE
  PART_EVAL_ONLY_NONE = 2
} UENUM1BYTE(PART_EVAL_STATUS);

typedef struct {
  VPVariance *part_variances;
  VPartVar *split[4];
} variance_node;

static inline void tree_to_node(void *data, BLOCK_SIZE bsize,
                                variance_node *node) {
  node->part_variances = NULL;
  switch (bsize) {
    case BLOCK_256X256: {
      VP256x256 *vt = (VP256x256 *)data;
      node->part_variances = &vt->part_variances;
      for (int split_idx = 0; split_idx < 4; split_idx++)
        node->split[split_idx] = &vt->split[split_idx].part_variances.none;
      break;
    }
    case BLOCK_128X128: {
      VP128x128 *vt = (VP128x128 *)data;
      node->part_variances = &vt->part_variances;
      for (int split_idx = 0; split_idx < 4; split_idx++)
        node->split[split_idx] = &vt->split[split_idx].part_variances.none;
      break;
    }
    case BLOCK_64X64: {
      VP64x64 *vt = (VP64x64 *)data;
      node->part_variances = &vt->part_variances;
      for (int split_idx = 0; split_idx < 4; split_idx++)
        node->split[split_idx] = &vt->split[split_idx].part_variances.none;
      break;
    }
    case BLOCK_32X32: {
      VP32x32 *vt = (VP32x32 *)data;
      node->part_variances = &vt->part_variances;
      for (int split_idx = 0; split_idx < 4; split_idx++)
        node->split[split_idx] = &vt->split[split_idx].part_variances.none;
      break;
    }
    case BLOCK_16X16: {
      VP16x16 *vt = (VP16x16 *)data;
      node->part_variances = &vt->part_variances;
      for (int split_idx = 0; split_idx < 4; split_idx++)
        node->split[split_idx] = &vt->split[split_idx].part_variances.none;
      break;
    }
    case BLOCK_8X8: {
      VP8x8 *vt = (VP8x8 *)data;
      node->part_variances = &vt->part_variances;
      for (int split_idx = 0; split_idx < 4; split_idx++)
        node->split[split_idx] = &vt->split[split_idx].part_variances.none;
      break;
    }
    default: {
      VP4x4 *vt = (VP4x4 *)data;
      assert(bsize == BLOCK_4X4);
      node->part_variances = &vt->part_variances;
      for (int split_idx = 0; split_idx < 4; split_idx++)
        node->split[split_idx] = &vt->split[split_idx];
      break;
    }
  }
}

// Set variance values given sum square error, sum error, count.
static inline void fill_variance(uint64_t s2, int32_t s, int c, VPartVar *v) {
  v->sum_square_error = s2;
  v->sum_error = s;
  v->log2_count = c;
}

static inline void get_variance(VPartVar *v) {
  v->variance =
      (int)(256 * (v->sum_square_error -
                   (uint64_t)(((int64_t)v->sum_error * v->sum_error) >>
                              v->log2_count)) >>
            v->log2_count);
}

static inline void sum_2_variances(const VPartVar *a, const VPartVar *b,
                                   VPartVar *r) {
  assert(a->log2_count == b->log2_count);
  fill_variance(a->sum_square_error + b->sum_square_error,
                a->sum_error + b->sum_error, a->log2_count + 1, r);
}

static inline void fill_variance_tree(void *data, BLOCK_SIZE bsize) {
  variance_node node;
  memset(&node, 0, sizeof(node));
  tree_to_node(data, bsize, &node);
  sum_2_variances(node.split[0], node.split[1], &node.part_variances->horz[0]);
  sum_2_variances(node.split[2], node.split[3], &node.part_variances->horz[1]);
  sum_2_variances(node.split[0], node.split[2], &node.part_variances->vert[0]);
  sum_2_variances(node.split[1], node.split[3], &node.part_variances->vert[1]);
  sum_2_variances(&node.part_variances->vert[0], &node.part_variances->vert[1],
                  &node.part_variances->none);
}

static inline void set_block_size(AV2_COMP *const cpi, int mi_row, int mi_col,
                                  BLOCK_SIZE bsize) {
  if (cpi->common.mi_params.mi_cols > mi_col &&
      cpi->common.mi_params.mi_rows > mi_row) {
    CommonModeInfoParams *mi_params = &cpi->common.mi_params;
    const int mi_grid_idx = get_mi_grid_idx(mi_params, mi_row, mi_col);
    const int mi_alloc_idx = get_alloc_mi_idx(mi_params, mi_row, mi_col);
    MB_MODE_INFO *mi = mi_params->mi_grid_base[mi_grid_idx] =
        &mi_params->mi_alloc[mi_alloc_idx];
    mi->sb_type[PLANE_TYPE_Y] = bsize;
    mi->sb_type[PLANE_TYPE_UV] = bsize;
  }
}

static int set_vt_partitioning(AV2_COMP *cpi, MACROBLOCKD *const xd,
                               const TileInfo *const tile, void *data,
                               BLOCK_SIZE bsize, int mi_row, int mi_col,
                               int64_t threshold, BLOCK_SIZE bsize_min,
                               PART_EVAL_STATUS force_split) {
  AV2_COMMON *const cm = &cpi->common;
  // TODO(jianj-g): Use only square for now.
  const int check_horiz_split = false;
  const int check_vert_split = false;
  variance_node vt;
  const int block_width = mi_size_wide[bsize];
  const int block_height = mi_size_high[bsize];
  int bs_width_check = block_width;
  int bs_height_check = block_height;
  // On the right and bottom boundary we only need to check
  // if half the bsize fits, because boundary is extended
  // up to 64. So do this check only for sb_size = 64X64.
  if (cm->seq_params.sb_size == BLOCK_64X64) {
    if (tile->mi_col_end == cm->mi_params.mi_cols) {
      bs_width_check = (block_width >> 1) + 1;
    }
    if (tile->mi_row_end == cm->mi_params.mi_rows) {
      bs_height_check = (block_height >> 1) + 1;
    }
  }

  assert(block_height == block_width);
  tree_to_node(data, bsize, &vt);

  if (mi_col + bs_width_check <= tile->mi_col_end &&
      mi_row + bs_height_check <= tile->mi_row_end &&
      force_split == PART_EVAL_ONLY_NONE) {
    set_block_size(cpi, mi_row, mi_col, bsize);
    return 1;
  }
  if (force_split == PART_EVAL_ONLY_SPLIT) return 0;

  // For bsize=bsize_min (16x16/8x8 for 8x8/4x4 downsampling), select if
  // variance is below threshold, otherwise split will be selected.
  // No check for vert/horiz split as too few samples for variance.
  if (bsize == bsize_min) {
    // Variance already computed to set the force_split.
    if (frame_is_intra_only(cm)) get_variance(&vt.part_variances->none);
    if (mi_col + bs_width_check <= tile->mi_col_end &&
        mi_row + bs_height_check <= tile->mi_row_end &&
        vt.part_variances->none.variance < threshold) {
      set_block_size(cpi, mi_row, mi_col, bsize);
      return 1;
    }
    return 0;
  } else if (bsize > bsize_min) {
    // Variance already computed to set the force_split.
    if (frame_is_intra_only(cm)) get_variance(&vt.part_variances->none);
    // For key frame: take split for bsize above 32X32 or very high variance.
    if (frame_is_intra_only(cm) &&
        (bsize > BLOCK_32X32 ||
         vt.part_variances->none.variance > (threshold << 4))) {
      return 0;
    }
    // If variance is low, take the bsize (no split).
    if (mi_col + bs_width_check <= tile->mi_col_end &&
        mi_row + bs_height_check <= tile->mi_row_end &&
        vt.part_variances->none.variance < threshold) {
      set_block_size(cpi, mi_row, mi_col, bsize);
      return 1;
    }
    // Check vertical split.
    if (check_vert_split) {
      int bs_width_vert_check = (block_width >> 1);
      if (cm->seq_params.sb_size == BLOCK_64X64 &&
          tile->mi_col_end == cm->mi_params.mi_cols) {
        bs_width_vert_check = (block_width >> 2) + 1;
      }
      if (mi_row + bs_height_check <= tile->mi_row_end &&
          mi_col + bs_width_vert_check <= tile->mi_col_end) {
        BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_VERT);
        BLOCK_SIZE plane_bsize =
            get_plane_block_size(subsize, xd->plane[AVM_PLANE_U].subsampling_x,
                                 xd->plane[AVM_PLANE_U].subsampling_y);
        get_variance(&vt.part_variances->vert[0]);
        get_variance(&vt.part_variances->vert[1]);
        if (vt.part_variances->vert[0].variance < threshold &&
            vt.part_variances->vert[1].variance < threshold &&
            plane_bsize < BLOCK_INVALID) {
          set_block_size(cpi, mi_row, mi_col, subsize);
          set_block_size(cpi, mi_row, mi_col + block_width / 2, subsize);
          return 1;
        }
      }
    }
    // Check horizontal split.
    if (check_horiz_split) {
      int bs_height_horiz_check = (block_height >> 1);
      if (cm->seq_params.sb_size == BLOCK_64X64 &&
          tile->mi_row_end == cm->mi_params.mi_rows) {
        bs_height_horiz_check = (block_height >> 2) + 1;
      }
      if (mi_col + bs_width_check <= tile->mi_col_end &&
          mi_row + bs_height_horiz_check <= tile->mi_row_end) {
        BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_HORZ);
        BLOCK_SIZE plane_bsize =
            get_plane_block_size(subsize, xd->plane[AVM_PLANE_U].subsampling_x,
                                 xd->plane[AVM_PLANE_U].subsampling_y);
        get_variance(&vt.part_variances->horz[0]);
        get_variance(&vt.part_variances->horz[1]);
        if (vt.part_variances->horz[0].variance < threshold &&
            vt.part_variances->horz[1].variance < threshold &&
            plane_bsize < BLOCK_INVALID) {
          set_block_size(cpi, mi_row, mi_col, subsize);
          set_block_size(cpi, mi_row + block_height / 2, mi_col, subsize);
          return 1;
        }
      }
    }
    return 0;
  }
  return 0;
}

// Obtain parameters required to calculate variance (such as sum, sse, etc,.)
// at 8x8 sub-block level for a given 16x16 block.
// The function can be called only when is_key_frame is false since sum is
// computed between source and reference frames.
static inline void fill_variance_8x8avg(const uint16_t *src_buf, int src_stride,
                                        const uint16_t *dst_buf, int dst_stride,
                                        int x16_idx, int y16_idx, VP16x16 *vst,
                                        int pixels_wide, int pixels_high) {
  for (int idx = 0; idx < 4; idx++) {
    const int x8_idx = x16_idx + GET_BLK_IDX_X(idx, 3);
    const int y8_idx = y16_idx + GET_BLK_IDX_Y(idx, 3);
    unsigned int sse = 0;
    int sum = 0;
    if (x8_idx < pixels_wide && y8_idx < pixels_high) {
      int src_avg = avm_highbd_avg_8x8(src_buf + y8_idx * src_stride + x8_idx,
                                       src_stride);
      int dst_avg = avm_highbd_avg_8x8(dst_buf + y8_idx * dst_stride + x8_idx,
                                       dst_stride);

      sum = src_avg - dst_avg;
      sse = sum * sum;
    }
    fill_variance(sse, sum, 0, &vst->split[idx].part_variances.none);
  }
}

// Function to compute average and variance of 4x4 sub-block.
// The function can be called only when is_key_frame is true since sum is
// computed using source frame only.
static inline void fill_variance_4x4avg(const uint16_t *src_buf, int src_stride,
                                        int x8_idx, int y8_idx, VP8x8 *vst,
                                        int pixels_wide, int pixels_high,
                                        int bit_depth) {
  for (int idx = 0; idx < 4; idx++) {
    const int x4_idx = x8_idx + GET_BLK_IDX_X(idx, 2);
    const int y4_idx = y8_idx + GET_BLK_IDX_Y(idx, 2);
    unsigned int sse = 0;
    int sum = 0;
    if (x4_idx < pixels_wide && y4_idx < pixels_high) {
      int src_avg;
      int dst_avg = 1 << (bit_depth - 1);
      src_avg = avm_highbd_avg_4x4(src_buf + y4_idx * src_stride + x4_idx,
                                   src_stride);
      sum = src_avg - dst_avg;
      sse = sum * sum;
    }
    fill_variance(sse, sum, 0, &vst->split[idx].part_variances.none);
  }
}

static inline PART_EVAL_STATUS get_part_eval_based_on_sub_blk_var(
    VP16x16 *var_16x16_info, int64_t threshold_base, int current_qindex,
    bool is_lowres, bool is_screen) {
  int max_8x8_var = 0, min_8x8_var = INT_MAX;
  for (int split_idx = 0; split_idx < 4; split_idx++) {
    get_variance(&var_16x16_info->split[split_idx].part_variances.none);
    int this_8x8_var =
        var_16x16_info->split[split_idx].part_variances.none.variance;
    max_8x8_var = AVMMAX(this_8x8_var, max_8x8_var);
    min_8x8_var = AVMMIN(this_8x8_var, min_8x8_var);
  }
  const int diff = max_8x8_var - min_8x8_var;
  if (is_screen) {
    if (current_qindex > 115) {
      return (diff > (threshold_base << 1)) ? PART_EVAL_ONLY_SPLIT
                                            : PART_EVAL_ALL;
    }
    return (diff > (threshold_base >> 1)) ? PART_EVAL_ONLY_SPLIT
                                          : PART_EVAL_ALL;
  }
  if (is_lowres) {
    if (current_qindex > 130) {
      return (diff > (threshold_base << 1)) ? PART_EVAL_ONLY_SPLIT
                                            : PART_EVAL_ONLY_NONE;
    }
    return (diff > (threshold_base >> 1)) ? PART_EVAL_ONLY_SPLIT
                                          : PART_EVAL_ALL;
  }
  if (current_qindex <= 110) {
    if (diff > (threshold_base << 1)) return PART_EVAL_ONLY_SPLIT;
    if (diff > (threshold_base >> 1)) return PART_EVAL_ALL;
    return PART_EVAL_ONLY_NONE;
  }
  return (diff > (threshold_base << 1)) ? PART_EVAL_ONLY_SPLIT
                                        : PART_EVAL_ONLY_NONE;
}

static inline int64_t interpolate_threshold(int64_t low_val, int64_t high_val,
                                            int q, int q_low, int q_high) {
  if (q <= q_low) return low_val;
  if (q >= q_high) return high_val;
  return (int64_t)((q - q_low) * high_val + (q_high - q) * low_val) /
         (q_high - q_low);
}

static inline void tune_thresh_based_on_resolution(AV2_COMP *cpi,
                                                   int64_t thresholds[],
                                                   int64_t threshold_base,
                                                   int current_qindex,
                                                   int num_pixels) {
  const bool is_screen = (cpi->oxcf.tune_cfg.content == AVM_CONTENT_SCREEN);
  if (num_pixels >= RESOLUTION_720P) thresholds[4] = thresholds[4] << 1;
  if (num_pixels <= RESOLUTION_288P) {
    const int q_low = 110;
    const int q_high = 135;
    thresholds[2] = threshold_base >> 3;
    if (num_pixels <= RESOLUTION_180P) {
      thresholds[3] = interpolate_threshold((3 * threshold_base) >> 3,
                                            (7 * threshold_base) >> 3,
                                            current_qindex, q_low, q_high);
      thresholds[4] =
          interpolate_threshold(threshold_base << 2, threshold_base << 4,
                                current_qindex, q_low, q_high);
    } else if (num_pixels <= RESOLUTION_240P) {
      thresholds[3] = interpolate_threshold((7 * threshold_base) >> 4,
                                            (7 * threshold_base) >> 3,
                                            current_qindex, q_low, q_high);
      thresholds[4] =
          interpolate_threshold(threshold_base << 2, threshold_base << 4,
                                current_qindex, q_low, q_high);
    } else {
      thresholds[3] =
          interpolate_threshold(threshold_base, (9 * threshold_base) >> 2,
                                current_qindex, q_low, q_high);
      thresholds[4] =
          interpolate_threshold(threshold_base << 3, threshold_base << 4,
                                current_qindex, q_low, q_high);
    }
    if (!is_screen && !cpi->is_screen_content_type) {
      thresholds[2] = threshold_base;
      if (current_qindex > 135) {
        const int q_diff = current_qindex - 135;
        thresholds[2] = thresholds[2] + ((q_diff * thresholds[2]) >> 5);
        if (num_pixels <= RESOLUTION_240P) {
          thresholds[3] = thresholds[3] + ((q_diff * thresholds[3]) >> 4);
        }
      }
      if (current_qindex > 175) {
        thresholds[4] = INT64_MAX;
      }
    }
  } else if (num_pixels <= RESOLUTION_360P) {
    thresholds[2] = threshold_base;
    thresholds[3] = (3 * threshold_base) >> 1;
    thresholds[4] = threshold_base << 5;
    if (current_qindex > 115) {
      const int q_diff = current_qindex - 115;
      thresholds[2] = thresholds[2] + ((q_diff * thresholds[2]) >> 5);
      thresholds[3] = thresholds[3] + ((q_diff * thresholds[3]) >> 4);
    }
    if (current_qindex > 135) {
      thresholds[4] = INT64_MAX;
    }
  } else if (num_pixels < RESOLUTION_720P) {
    thresholds[3] = (3 * threshold_base) >> 1;
    if (current_qindex > 115) {
      const int q_diff = current_qindex - 115;
      thresholds[2] = thresholds[2] + ((q_diff * thresholds[2]) >> 5);
      thresholds[3] = thresholds[3] + ((q_diff * thresholds[3]) >> 4);
    }
    if (current_qindex > 135) {
      thresholds[4] = INT64_MAX;
    }
  } else if (num_pixels < RESOLUTION_1080P) {
    thresholds[3] = threshold_base << 1;
    if (current_qindex > 115) {
      const int q_diff = current_qindex - 115;
      thresholds[1] = thresholds[1] + ((q_diff * thresholds[1]) >> 5);
      thresholds[2] = thresholds[2] + ((q_diff * thresholds[2]) >> 5);
      if (is_screen) {
        thresholds[3] = thresholds[3] + ((q_diff * thresholds[3]) >> 5);
      } else {
        thresholds[3] = thresholds[3] + ((q_diff * 3 * thresholds[3]) >> 6);
      }
    }
    if (current_qindex > 135) {
      thresholds[4] = INT64_MAX;
    }
  } else {
    // num_pixels >= RESOLUTION_1080P
    if (is_screen) {
      if (num_pixels < RESOLUTION_1440P) {
        thresholds[3] = (5 * threshold_base) >> 1;
      } else {
        thresholds[3] = (7 * threshold_base) >> 1;
      }
    } else {
      if (cpi->oxcf.speed > 7) {
        thresholds[3] = threshold_base << 2;
      }
    }
    if (current_qindex > 115) {
      const int q_diff = current_qindex - 115;
      thresholds[1] = thresholds[1] + ((q_diff * thresholds[1]) >> 5);
      thresholds[2] = thresholds[2] + ((q_diff * thresholds[2]) >> 5);
      thresholds[3] = thresholds[3] + ((q_diff * thresholds[3]) >> 5);
    }
    if (current_qindex > 135) {
      thresholds[4] = INT64_MAX;
    }
  }
}

static void set_vbp_thresholds_key_frame(int64_t thresholds[],
                                         int64_t threshold_base,
                                         int num_pixels) {
  thresholds[0] = threshold_base >> 1;
  thresholds[1] = threshold_base;
  thresholds[2] = threshold_base;
  if (num_pixels < RESOLUTION_720P) {
    thresholds[3] = threshold_base / 3;
    thresholds[4] = threshold_base >> 1;
  } else {
    int shift_val = 2;
    thresholds[3] = threshold_base >> shift_val;
    thresholds[4] = threshold_base >> shift_val;
  }
  thresholds[5] = threshold_base << 2;
}

/*!\brief Computes block variance split thresholds based on frame type, Q index,
 * and resolution.
 *
 * Calculates a hierarchy of variance thresholds stored in `thresholds[]` for
 * each block level (ranging from root block size down to 16x16 blocks).
 *
 * - The base threshold (`threshold_base`) is derived from the AC quantizer step
 * size at `qindex`.
 * - Keyframes use a higher threshold multiplier and delegate to
 * `set_vbp_thresholds_key_frame()`.
 * - Inter-frames scale base thresholds across block levels and apply
 * resolution/QP-based tuning via `tune_thresh_based_on_resolution()`.
 *
 * \param[in]  cpi         Top level encoder structure
 * \param[out] thresholds  Array of length 6 holding computed variance split
 * thresholds per block level
 * \param[in]  qindex      Quantization index used to derive the base variance
 * threshold
 */
static inline void set_vbp_thresholds(AV2_COMP *cpi, int64_t thresholds[],
                                      int qindex, SOURCE_SAD source_sad_level,
                                      int is_segment_id_boosted) {
  AV2_COMMON *const cm = &cpi->common;
  const int is_key_frame = frame_is_intra_only(cm);
  const int threshold_multiplier = is_key_frame ? 120 : 1;
  // In AV2, qindex is linear in dB (exponential step size 2^(q/24)), whereas
  // AV1 had a linear quantizer table. We compute threshold_base as a function
  // of qindex and source_sad_level to account for the exponential range in AV2.
  const int num_pixels = cm->width * cm->height;
  const int32_t ac_q = av2_ac_quant_QTX(qindex, 0, 0, cm->seq_params.bit_depth);
  const int64_t q_offset = qindex - 65;
  const int64_t poly_base =
      (qindex <= 65) ? 12 : (12 + ((q_offset * (qindex + 15)) / 40));

  const bool is_screen = (cpi->oxcf.tune_cfg.content == AVM_CONTENT_SCREEN);
  int64_t quant_base;
  if (num_pixels <= RESOLUTION_240P &&
      (is_screen || cpi->is_screen_content_type)) {
    if (source_sad_level == kZeroSad) {
      quant_base = ac_q >> 2;
    } else {
      quant_base = poly_base;
    }
  } else if (num_pixels <= RESOLUTION_288P) {
    if (source_sad_level <= kVeryLowSad) {
      quant_base = ac_q >> 2;
    } else if (source_sad_level <= kLowSad) {
      quant_base = (3 * ac_q) >> 4;
    } else if (!is_screen && !cpi->is_screen_content_type) {
      quant_base = AVMMAX(poly_base, ac_q >> 2);
    } else {
      quant_base = poly_base;
    }
  } else {
    quant_base = ac_q >> 2;
  }

  int64_t threshold_base;
  if (qindex <= 80) {
    threshold_base = poly_base;
  } else if (qindex >= 120) {
    threshold_base = quant_base;
  } else {
    threshold_base =
        interpolate_threshold(poly_base, quant_base, qindex, 80, 120);
  }
  threshold_base *= threshold_multiplier;

  const int threshold_left_shift = 7;

  if (is_key_frame) {
    set_vbp_thresholds_key_frame(thresholds, threshold_base, num_pixels);
    thresholds[5] = INT64_MAX;
    return;
  }

  thresholds[0] = threshold_base >> 2;
  thresholds[1] = threshold_base >> 1;
  thresholds[2] = threshold_base;
  thresholds[3] = threshold_base << 2;
  thresholds[4] = threshold_base << threshold_left_shift;
  thresholds[5] = INT64_MAX;

  tune_thresh_based_on_resolution(cpi, thresholds, threshold_base, qindex,
                                  num_pixels);

  if (is_screen) {
    if (source_sad_level == kZeroSad) {
      thresholds[4] = INT64_MAX;
    }
    if (!is_segment_id_boosted) {
      if (source_sad_level == kZeroSad) {
        thresholds[1] = (2 * thresholds[1]);
        thresholds[2] = (2 * thresholds[2]);
        thresholds[3] = (2 * thresholds[3]);
      } else if (source_sad_level <= kVeryLowSad) {
        thresholds[1] = (3 * thresholds[1]) >> 1;
        thresholds[2] = (3 * thresholds[2]) >> 1;
        thresholds[3] = (3 * thresholds[3]) >> 1;
      } else if (source_sad_level <= kLowSad) {
        thresholds[1] = (5 * thresholds[1]) >> 2;
        thresholds[2] = (5 * thresholds[2]) >> 2;
        thresholds[3] = (5 * thresholds[3]) >> 2;
      }
    }
  } else {
    if (num_pixels <= RESOLUTION_288P) {
      if (source_sad_level == kZeroSad) {
        thresholds[2] = thresholds[2] << 2;
        thresholds[3] = thresholds[3] << 1;
        thresholds[4] = INT64_MAX;
      } else if (!is_segment_id_boosted) {
        if (source_sad_level <= kVeryLowSad && qindex >= 100) {
          thresholds[2] = (3 * thresholds[2]) >> 1;
          thresholds[3] = (5 * thresholds[3]) >> 2;
        } else if (source_sad_level <= kLowSad && qindex >= 110) {
          thresholds[2] = (5 * thresholds[2]) >> 2;
          thresholds[3] = (9 * thresholds[3]) >> 3;
        }
      }
    } else if (!is_segment_id_boosted) {
      if (source_sad_level == kZeroSad && qindex >= 100) {
        thresholds[1] = (7 * thresholds[1]) >> 2;
        if (num_pixels > RESOLUTION_360P) {
          thresholds[2] = (3 * thresholds[2]) >> 1;
        }
      } else if (source_sad_level <= kVeryLowSad && qindex >= 100) {
        thresholds[1] = (3 * thresholds[1]) >> 1;
        if (num_pixels > RESOLUTION_360P) {
          thresholds[2] = (5 * thresholds[2]) >> 2;
        }
      } else if (source_sad_level <= kLowSad && qindex >= 100) {
        thresholds[1] = (5 * thresholds[1]) >> 2;
        if (num_pixels > RESOLUTION_360P) {
          thresholds[2] = (9 * thresholds[2]) >> 3;
        }
      }
    }
  }
}

static inline void force_split_ancestors(PART_EVAL_STATUS *force_split,
                                         int offset_64x64, int offset_32x32,
                                         int blk64_idx, int lvl1_idx) {
  const int blk64_scale_idx = blk64_idx << 2;
  force_split[offset_32x32 + blk64_scale_idx + lvl1_idx] = PART_EVAL_ONLY_SPLIT;
  force_split[blk64_idx + offset_64x64] = PART_EVAL_ONLY_SPLIT;
  force_split[0] = PART_EVAL_ONLY_SPLIT;
}

static inline bool check_128x128_split(int var_128x128, int avg_64x64,
                                       int min_var_64x64, int max_var_64x64,
                                       int64_t threshold_128) {
  const int set_avg_64x64 = (9 * avg_64x64) >> 5;
  if (var_128x128 > set_avg_64x64) return true;
  if ((max_var_64x64 - min_var_64x64) > 3 * (threshold_128 >> 3) &&
      max_var_64x64 > (threshold_128 >> 1)) {
    return true;
  }
  return false;
}

/*!\brief Populates the leaf nodes of the variance tree and initializes
 * partition search flags.
 *
 * Traverses all 64x64 sub-blocks within the given superblock and computes
 * leaf-level block variances:
 * - For keyframes: Computes 4x4 down-sampled source variances
 * (`fill_variance_4x4avg`).
 * - For inter-frames: Computes 8x8 down-sampled residual (source vs prediction)
 *   variances (`fill_variance_8x8avg`) and aggregates them up to 16x16 blocks.
 *
 * Accumulates 16x16 block statistics (`avg_16x16`, `maxvar_16x16`,
 * `minvar_16x16`) and checks if 16x16 variance exceeds the split threshold. If
 * so, updates `force_split` flags to force splitting down to 8x8 for that 16x16
 * block and its ancestors.
 *
 * \param[in]     cpi           Top level encoder structure
 * \param[in]     x             Pointer to MACROBLOCK structure
 * \param[in,out] vt            Pointer to the top-level variance tree
 * (VP128x128 array)
 * \param[out]    force_split   Flat array storing forced partition evaluation
 * decisions
 * \param[out]    avg_16x16     Accumulator for 16x16 block average variances
 * per 64x64 block
 * \param[out]    maxvar_16x16  Array tracking maximum 16x16 block variance per
 * 64x64 block
 * \param[out]    minvar_16x16  Array tracking minimum 16x16 block variance per
 * 64x64 block
 * \param[in]     thresholds    Array of variance thresholds per block level
 * \param[in]     src_buf       Pointer to source Y plane buffer
 * \param[in]     src_stride    Stride of source Y plane buffer
 * \param[in]     dst_buf       Pointer to prediction Y plane buffer (NULL for
 * keyframes)
 * \param[in]     dst_stride    Stride of prediction Y plane buffer
 * \param[in]     is_key_frame  True if encoding a keyframe
 * \param[in]     sb_size       Superblock size (BLOCK_64X64, BLOCK_128X128, or
 * BLOCK_256X256)
 */
static void fill_variance_tree_leaves(
    AV2_COMP *cpi, MACROBLOCK *x, VP128x128 *vt, PART_EVAL_STATUS *force_split,
    int avg_16x16[][4], int maxvar_16x16[][4], int minvar_16x16[][4],
    int64_t *thresholds, const uint16_t *src_buf, int src_stride,
    const uint16_t *dst_buf, int dst_stride, bool is_key_frame,
    BLOCK_SIZE sb_size) {
  MACROBLOCKD *xd = &x->e_mbd;
  const int num_64x64_blocks = (sb_size == BLOCK_64X64)     ? 1
                               : (sb_size == BLOCK_128X128) ? 4
                                                            : 16;
  int pixels_wide = (sb_size == BLOCK_64X64)     ? 64
                    : (sb_size == BLOCK_128X128) ? 128
                                                 : 256;
  int pixels_high = pixels_wide;
  // dst_buf pointer is not used for is_key_frame, so it should be NULL.
  assert(IMPLIES(is_key_frame, dst_buf == NULL));
  if (xd->mb_to_right_edge < 0) pixels_wide += (xd->mb_to_right_edge >> 3);
  if (xd->mb_to_bottom_edge < 0) pixels_high += (xd->mb_to_bottom_edge >> 3);
  const VPartOffsets offsets = get_vpart_offsets(sb_size);
  const int offset_64x64 = offsets.offset_64x64;
  const int offset_32x32 = offsets.offset_32x32;
  const int offset_16x16 = offsets.offset_16x16;
  const bool is_lowres =
      (cpi->common.width * cpi->common.height <= RESOLUTION_288P);
  const bool is_screen = (cpi->oxcf.tune_cfg.content == AVM_CONTENT_SCREEN);
  for (int blk64_idx = 0; blk64_idx < num_64x64_blocks; blk64_idx++) {
    int x64_idx, y64_idx;
    VP64x64 *vt64;
    if (sb_size == BLOCK_256X256) {
      x64_idx = ((blk64_idx & 3) << 6);
      y64_idx = ((blk64_idx >> 2) << 6);
      vt64 = &vt[blk64_idx >> 2].split[blk64_idx & 3];
    } else {
      x64_idx = GET_BLK_IDX_X(blk64_idx, 6);
      y64_idx = GET_BLK_IDX_Y(blk64_idx, 6);
      vt64 = &vt->split[blk64_idx];
    }
    const int blk64_scale_idx = blk64_idx << 2;
    force_split[blk64_idx + offset_64x64] = PART_EVAL_ALL;

    for (int lvl1_idx = 0; lvl1_idx < 4; lvl1_idx++) {
      const int x32_idx = x64_idx + GET_BLK_IDX_X(lvl1_idx, 5);
      const int y32_idx = y64_idx + GET_BLK_IDX_Y(lvl1_idx, 5);
      const int lvl1_scale_idx = (blk64_scale_idx + lvl1_idx) << 2;
      force_split[offset_32x32 + blk64_scale_idx + lvl1_idx] = PART_EVAL_ALL;
      avg_16x16[blk64_idx][lvl1_idx] = 0;
      maxvar_16x16[blk64_idx][lvl1_idx] = 0;
      minvar_16x16[blk64_idx][lvl1_idx] = INT_MAX;
      for (int lvl2_idx = 0; lvl2_idx < 4; lvl2_idx++) {
        const int x16_idx = x32_idx + GET_BLK_IDX_X(lvl2_idx, 4);
        const int y16_idx = y32_idx + GET_BLK_IDX_Y(lvl2_idx, 4);
        const int split_index = offset_16x16 + lvl1_scale_idx + lvl2_idx;
        VP16x16 *vst = &vt64->split[lvl1_idx].split[lvl2_idx];
        force_split[split_index] = PART_EVAL_ALL;
        if (is_key_frame) {
          // Go down to 4x4 down-sampling for variance.
          for (int lvl3_idx = 0; lvl3_idx < 4; lvl3_idx++) {
            const int x8_idx = x16_idx + GET_BLK_IDX_X(lvl3_idx, 3);
            const int y8_idx = y16_idx + GET_BLK_IDX_Y(lvl3_idx, 3);
            VP8x8 *vst2 = &vst->split[lvl3_idx];
            fill_variance_4x4avg(src_buf, src_stride, x8_idx, y8_idx, vst2,
                                 pixels_wide, pixels_high, xd->bd);
          }
        } else {
          fill_variance_8x8avg(src_buf, src_stride, dst_buf, dst_stride,
                               x16_idx, y16_idx, vst, pixels_wide, pixels_high);

          fill_variance_tree(vst, BLOCK_16X16);
          VPartVar *none_var =
              &vt64->split[lvl1_idx].split[lvl2_idx].part_variances.none;
          get_variance(none_var);
          const int val_none_var = none_var->variance;
          avg_16x16[blk64_idx][lvl1_idx] += val_none_var;
          minvar_16x16[blk64_idx][lvl1_idx] =
              AVMMIN(minvar_16x16[blk64_idx][lvl1_idx], val_none_var);
          maxvar_16x16[blk64_idx][lvl1_idx] =
              AVMMAX(maxvar_16x16[blk64_idx][lvl1_idx], val_none_var);
          if (val_none_var > thresholds[4]) {
            // 16X16 variance is above threshold for split, so evaluate
            // split based on sub-block variance variation (this also forces
            // splits for upper levels).
            const int current_qindex = cpi->common.quant_params.base_qindex;
            force_split[split_index] = get_part_eval_based_on_sub_blk_var(
                vst, thresholds[2], current_qindex, is_lowres, is_screen);
            force_split_ancestors(force_split, offset_64x64, offset_32x32,
                                  blk64_idx, lvl1_idx);
          }
        }
      }
    }
  }
}

static AVM_INLINE int mv_distance(const FULLPEL_MV *mv0,
                                  const FULLPEL_MV *mv1) {
  return abs(mv0->row - mv1->row) + abs(mv0->col - mv1->col);
}

static AVM_INLINE void evaluate_neighbour_mvs(AV2_COMP *cpi, MACROBLOCK *x,
                                              unsigned int *y_sad) {
  AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mi = xd->mi[0];
  const BLOCK_SIZE bsize = cm->seq_params.sb_size;
  const MV_REFERENCE_FRAME last_frame = mi->ref_frame[0];

  unsigned int above_y_sad = UINT_MAX;
  unsigned int left_y_sad = UINT_MAX;
  FULLPEL_MV above_mv = kZeroFullMv;
  FULLPEL_MV left_mv = kZeroFullMv;
  SubpelMvLimits subpel_mv_limits;
  const MV dummy_mv = { 0, 0 };
  av2_set_subpel_mv_search_range(&subpel_mv_limits, &x->mv_limits, &dummy_mv,
                                 cm->features.fr_mv_precision);

  // Current best MV
  FULLPEL_MV best_mv = get_fullmv_from_mv(&mi->mv[0].as_mv);

  // Use the MV from above SB if available.
  if (xd->up_available && xd->above_mbmi) {
    const MB_MODE_INFO *above_mbmi = xd->above_mbmi;
    if (above_mbmi->mode >= SINGLE_INTER_MODE_START &&
        above_mbmi->ref_frame[0] == last_frame &&
        above_mbmi->mv[0].as_int != INVALID_MV) {
      MV temp = above_mbmi->mv[0].as_mv;
      clamp_mv(&temp, &subpel_mv_limits);
      above_mv = get_fullmv_from_mv(&temp);

      if (!CHECK_MV_EQUAL(above_mv, kZeroFullMv) &&
          mv_distance(&best_mv, &above_mv) > 0) {
        const uint16_t *ref_buf =
            xd->plane[AVM_PLANE_Y].pre[0].buf +
            above_mv.row * xd->plane[AVM_PLANE_Y].pre[0].stride + above_mv.col;
        above_y_sad = cpi->fn_ptr[bsize].sdf(
            x->plane[AVM_PLANE_Y].src.buf, x->plane[AVM_PLANE_Y].src.stride,
            ref_buf, xd->plane[AVM_PLANE_Y].pre[0].stride);
      }
    }
  }

  // Use the MV from left SB if available.
  if (xd->left_available && xd->left_mbmi) {
    const MB_MODE_INFO *left_mbmi = xd->left_mbmi;
    if (left_mbmi->mode >= SINGLE_INTER_MODE_START &&
        left_mbmi->ref_frame[0] == last_frame &&
        left_mbmi->mv[0].as_int != INVALID_MV) {
      MV temp = left_mbmi->mv[0].as_mv;
      clamp_mv(&temp, &subpel_mv_limits);
      left_mv = get_fullmv_from_mv(&temp);

      if (!CHECK_MV_EQUAL(left_mv, kZeroFullMv) &&
          mv_distance(&best_mv, &left_mv) > 0 &&
          mv_distance(&above_mv, &left_mv) > 0) {
        const uint16_t *ref_buf =
            xd->plane[AVM_PLANE_Y].pre[0].buf +
            left_mv.row * xd->plane[AVM_PLANE_Y].pre[0].stride + left_mv.col;
        left_y_sad = cpi->fn_ptr[bsize].sdf(
            x->plane[AVM_PLANE_Y].src.buf, x->plane[AVM_PLANE_Y].src.stride,
            ref_buf, xd->plane[AVM_PLANE_Y].pre[0].stride);
      }
    }
  }

  if (above_y_sad < *y_sad && above_y_sad <= left_y_sad) {
    *y_sad = above_y_sad;
    mi->mv[0].as_mv = get_mv_from_fullmv(&above_mv);
  } else if (left_y_sad < *y_sad && left_y_sad < above_y_sad) {
    *y_sad = left_y_sad;
    mi->mv[0].as_mv = get_mv_from_fullmv(&left_mv);
  }
}

static AVM_INLINE void do_int_pro_motion_estimation(AV2_COMP *cpi,
                                                    MACROBLOCK *x,
                                                    unsigned int *y_sad,
                                                    int mi_row, int mi_col) {
  AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mi = xd->mi[0];
  const BLOCK_SIZE sb_size = cm->seq_params.sb_size;
  const bool is_screen = cpi->oxcf.tune_cfg.content == AVM_CONTENT_SCREEN;
  const SOURCE_SAD source_sad_level = x->source_sad_level;
  const bool increase_col_sw = source_sad_level > kMedSad;
  int me_search_size_col = is_screen ? (increase_col_sw ? 512 : 96)
                                     : (block_size_wide[sb_size] >> 1);
  // For screen use larger search size row motion to capture vertical scroll,
  // which can be larger motion.
  int me_search_size_row = is_screen ? (source_sad_level > kMedSad ? 512 : 192)
                                     : (block_size_high[sb_size] >> 1);
  if (cm->width * cm->height >= RESOLUTION_4K) {
    me_search_size_row <<= 1;
    me_search_size_col <<= 1;
  }
  unsigned int y_sad_zero = *y_sad;
  *y_sad = av2_int_pro_motion_estimation(
      cpi, x, sb_size, mi_row, mi_col, &kZeroMv, &y_sad_zero,
      me_search_size_col, me_search_size_row);
  // Select whether the motion estimated in av2_int_pro_motion_estimation()
  // is reliable (scroll / coherent motion with low SAD) and should be tested
  // as a candidate in NEWMV motion search. Only do this for screen for now.
  if (is_screen) {
    const unsigned int base_thresh_sad = (sb_size == BLOCK_256X256)   ? 200000
                                         : (sb_size == BLOCK_128X128) ? 50000
                                                                      : 20000;
    const unsigned int thresh_sad = base_thresh_sad << (xd->bd - 8);
    if (*y_sad < (y_sad_zero >> 1) && *y_sad < thresh_sad) {
      x->sb_me_partition = 1;
      x->sb_me_mv.as_int = mi->mv[0].as_int;
    } else {
      x->sb_me_partition = 0;
      // Fall back to using zero motion.
      *y_sad = y_sad_zero;
      mi->mv[0].as_int = 0;
    }
  }
}

static AVM_INLINE bool is_vbp_ref_enabled(const AV2_COMMON *const cm,
                                          MV_REFERENCE_FRAME ref) {
  return ref >= 0 && ref < INTER_REFS_PER_FRAME &&
         (cm->ref_frame_flags & (1 << ref)) != 0;
}

static AVM_INLINE MV_REFERENCE_FRAME
get_primary_vbp_ref(const AV2_COMP *const cpi) {
  const AV2_COMMON *const cm = &cpi->common;
  const int closest_ref = get_closest_pastcur_ref_or_ref0(cm);
  MV_REFERENCE_FRAME fallback_ref =
      (closest_ref >= 0 && closest_ref < INTER_REFS_PER_FRAME)
          ? (MV_REFERENCE_FRAME)closest_ref
          : 0;
  if (cpi->sf.rt_sf.rtc_enable_bru && cm->bru.enabled &&
      !cm->bru.frame_inactive_flag && fallback_ref == cm->bru.update_ref_idx) {
    fallback_ref =
        (fallback_ref == 0 && cm->ref_frames_info.num_total_refs > 1) ? 1 : 0;
  }
  if (cm->ref_frame_flags == 0) return fallback_ref;
  if (is_vbp_ref_enabled(cm, fallback_ref) &&
      !(cm->cur_frame && cm->cur_frame->refs_restricted_status[fallback_ref])) {
    return fallback_ref;
  }
  int best_dist = INT_MAX;
  MV_REFERENCE_FRAME best_ref = NONE_FRAME;
  for (int i = 0; i < cm->ref_frames_info.num_past_refs; ++i) {
    const MV_REFERENCE_FRAME ref = cm->ref_frames_info.past_refs[i];
    if (!is_vbp_ref_enabled(cm, ref)) continue;
    if (cm->cur_frame && cm->cur_frame->refs_restricted_status[ref]) continue;
    const int dist = cm->ref_frames_info.ref_frame_distance[ref];
    if (dist < best_dist) {
      best_dist = dist;
      best_ref = ref;
    }
  }
  if (best_ref != NONE_FRAME) return best_ref;
  for (MV_REFERENCE_FRAME ref = 0;
       ref < cm->ref_frames_info.num_total_refs && ref < INTER_REFS_PER_FRAME;
       ++ref) {
    if (!is_vbp_ref_enabled(cm, ref)) continue;
    if (cm->cur_frame && cm->cur_frame->refs_restricted_status[ref]) continue;
    return ref;
  }
  return fallback_ref;
}

static void setup_planes(AV2_COMP *cpi, MACROBLOCK *x, unsigned int *y_sad,
                         int mi_row, int mi_col, bool scaled_ref_last) {
  AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  const int num_planes = av2_num_planes(cm);
  BLOCK_SIZE bsize = cm->sb_size;
  MB_MODE_INFO *mi = xd->mi[0];
  const MV_REFERENCE_FRAME last_frame = get_primary_vbp_ref(cpi);
  const YV12_BUFFER_CONFIG *yv12 =
      scaled_ref_last ? av2_get_scaled_ref_frame(cpi, last_frame)
                      : get_ref_frame_yv12_buf(cm, last_frame);
  assert(yv12 != NULL);
  const bool use_last_ref =
      is_vbp_ref_enabled(cm, last_frame) || cm->ref_frame_flags == 0;

  mi->ref_frame[0] = NONE_FRAME;
  mi->ref_frame[1] = NONE_FRAME;
  mi->sb_type[PLANE_TYPE_Y] = cm->seq_params.sb_size;
  mi->sb_type[PLANE_TYPE_UV] = cm->seq_params.sb_size;
  mi->mv[0].as_int = 0;

  if (yv12 != NULL && use_last_ref) {
    av2_setup_pre_planes(
        xd, 0, yv12, mi_row, mi_col,
        scaled_ref_last ? NULL : get_ref_scale_factors(cm, last_frame),
        num_planes, NULL);
    mi->ref_frame[0] = last_frame;

    const int est_motion =
        cpi->sf.rt_sf.estimate_motion_for_var_based_partition;
    if (!scaled_ref_last && est_motion == 1 && xd->mb_to_right_edge >= 0 &&
        xd->mb_to_bottom_edge >= 0 && x->source_sad_level > kVeryLowSad) {
      if (x->source_variance == UINT_MAX) {
        x->source_variance = av2_high_get_sby_perpixel_variance(
            cpi, &x->plane[AVM_PLANE_Y].src, bsize, xd->bd);
      }
      if (x->source_variance > 100) {
        do_int_pro_motion_estimation(cpi, x, y_sad, mi_row, mi_col);
      }
    }

    if (*y_sad == UINT_MAX) {
      *y_sad = cpi->fn_ptr[bsize].sdf(x->plane[AVM_PLANE_Y].src.buf,
                                      x->plane[AVM_PLANE_Y].src.stride,
                                      xd->plane[AVM_PLANE_Y].pre[0].buf,
                                      xd->plane[AVM_PLANE_Y].pre[0].stride);
    }

    // Evaluate if neighbours' MVs give better predictions. Zero MV is tested
    // already, so only non-zero MVs are tested here. Here the neighbour blocks
    // are the first block above or left to this superblock.
    if (!scaled_ref_last && est_motion == 1 &&
        (xd->up_available || xd->left_available)) {
      evaluate_neighbour_mvs(cpi, x, y_sad);
    }
  }

  // Check if other enabled reference frames provide a better zero-MV
  // predictor for partitioning, biasing toward last_frame by factor 0.9.
  if ((x->source_sad_level != kZeroSad || !use_last_ref) && *y_sad > 0 &&
      !x->sb_me_partition) {
    MV_REFERENCE_FRAME best_other_ref = NONE_FRAME;
    unsigned int best_other_sad = UINT_MAX;
    const YV12_BUFFER_CONFIG *best_other_yv12 = NULL;
    bool best_other_scaled = false;

    for (MV_REFERENCE_FRAME ref_frame = 0;
         ref_frame < cm->ref_frames_info.num_total_refs &&
         ref_frame < INTER_REFS_PER_FRAME;
         ++ref_frame) {
      if (ref_frame == last_frame) continue;
      if (!is_vbp_ref_enabled(cm, ref_frame)) continue;
      if (cm->cur_frame && cm->cur_frame->refs_restricted_status[ref_frame])
        continue;
      const YV12_BUFFER_CONFIG *yv12_ref =
          get_ref_frame_yv12_buf(cm, ref_frame);
      bool scaled_ref = false;
      if (yv12_ref != NULL && (yv12_ref->y_crop_height != cm->height ||
                               yv12_ref->y_crop_width != cm->width)) {
        yv12_ref = av2_get_scaled_ref_frame(cpi, ref_frame);
        scaled_ref = true;
      }
      if (yv12_ref == NULL || (use_last_ref && yv12_ref == yv12)) continue;

      struct buf_2d ref_y_pre;
      const struct macroblockd_plane *const pd_y = &xd->plane[AVM_PLANE_Y];
      setup_pred_plane(&ref_y_pre, yv12_ref->buffers[AVM_PLANE_Y],
                       yv12_ref->widths[0], yv12_ref->heights[0],
                       yv12_ref->crop_widths[0], yv12_ref->crop_heights[0],
                       yv12_ref->strides[0], mi_row, mi_col,
                       scaled_ref ? NULL : get_ref_scale_factors(cm, ref_frame),
                       pd_y->subsampling_x, pd_y->subsampling_y, NULL);
      const unsigned int ref_y_sad = cpi->fn_ptr[bsize].sdf(
          x->plane[AVM_PLANE_Y].src.buf, x->plane[AVM_PLANE_Y].src.stride,
          ref_y_pre.buf, ref_y_pre.stride);
      if (ref_y_sad < best_other_sad) {
        best_other_sad = ref_y_sad;
        best_other_ref = ref_frame;
        best_other_yv12 = yv12_ref;
        best_other_scaled = scaled_ref;
      }
    }

    if (best_other_ref != NONE_FRAME &&
        (!use_last_ref ||
         (uint64_t)best_other_sad * 10 < (uint64_t)(*y_sad) * 9)) {
      av2_setup_pre_planes(
          xd, 0, best_other_yv12, mi_row, mi_col,
          best_other_scaled ? NULL : get_ref_scale_factors(cm, best_other_ref),
          num_planes, NULL);
      mi->ref_frame[0] = best_other_ref;
      mi->mv[0].as_int = 0;
      *y_sad = best_other_sad;
      x->sb_me_partition = 0;
      x->sb_me_mv.as_int = 0;
    } else if (!use_last_ref && yv12 != NULL) {
      av2_setup_pre_planes(
          xd, 0, yv12, mi_row, mi_col,
          scaled_ref_last ? NULL : get_ref_scale_factors(cm, last_frame),
          num_planes, NULL);
      mi->ref_frame[0] = last_frame;
      *y_sad = cpi->fn_ptr[bsize].sdf(x->plane[AVM_PLANE_Y].src.buf,
                                      x->plane[AVM_PLANE_Y].src.stride,
                                      xd->plane[AVM_PLANE_Y].pre[0].buf,
                                      xd->plane[AVM_PLANE_Y].pre[0].stride);
    }
  }
  x->vbp_ref_frame = mi->ref_frame[0];
}

static void set_vt_partitioning_64x64(AV2_COMP *cpi, MACROBLOCKD *xd,
                                      const TileInfo *const tile, VP64x64 *vt64,
                                      int mi_row, int mi_col, int x64_idx,
                                      int y64_idx, int blk64_idx,
                                      const int64_t *thresholds,
                                      const PART_EVAL_STATUS *force_split,
                                      VPartOffsets offsets) {
  const int offset_64x64 = offsets.offset_64x64;
  const int offset_32x32 = offsets.offset_32x32;
  const int offset_16x16 = offsets.offset_16x16;
  const int blk64_scale_idx = blk64_idx << 2;

  if (set_vt_partitioning(cpi, xd, tile, vt64, BLOCK_64X64, mi_row + y64_idx,
                          mi_col + x64_idx, thresholds[2], BLOCK_16X16,
                          force_split[offset_64x64 + blk64_idx]))
    return;

  for (int lvl1_idx = 0; lvl1_idx < 4; ++lvl1_idx) {
    const int x32_idx = GET_BLK_IDX_X(lvl1_idx, 3);
    const int y32_idx = GET_BLK_IDX_Y(lvl1_idx, 3);
    const int lvl1_scale_idx = (blk64_scale_idx + lvl1_idx) << 2;
    if (set_vt_partitioning(
            cpi, xd, tile, &vt64->split[lvl1_idx], BLOCK_32X32,
            (mi_row + y64_idx + y32_idx), (mi_col + x64_idx + x32_idx),
            thresholds[3], BLOCK_16X16,
            force_split[offset_32x32 + blk64_scale_idx + lvl1_idx]))
      continue;
    for (int lvl2_idx = 0; lvl2_idx < 4; ++lvl2_idx) {
      const int x16_idx = GET_BLK_IDX_X(lvl2_idx, 2);
      const int y16_idx = GET_BLK_IDX_Y(lvl2_idx, 2);
      const int split_index = offset_16x16 + lvl1_scale_idx + lvl2_idx;
      VP16x16 *vtemp = &vt64->split[lvl1_idx].split[lvl2_idx];
      if (set_vt_partitioning(cpi, xd, tile, vtemp, BLOCK_16X16,
                              mi_row + y64_idx + y32_idx + y16_idx,
                              mi_col + x64_idx + x32_idx + x16_idx,
                              thresholds[4], BLOCK_8X8,
                              force_split[split_index]))
        continue;
      for (int lvl3_idx = 0; lvl3_idx < 4; ++lvl3_idx) {
        const int x8_idx = GET_BLK_IDX_X(lvl3_idx, 1);
        const int y8_idx = GET_BLK_IDX_Y(lvl3_idx, 1);
        set_block_size(cpi, (mi_row + y64_idx + y32_idx + y16_idx + y8_idx),
                       (mi_col + x64_idx + x32_idx + x16_idx + x8_idx),
                       BLOCK_8X8);
      }
    }
  }
}

static inline bool is_chroma_var_high(const AV2_COMP *cpi, const MACROBLOCK *x,
                                      const MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                      int y_row, int y_col, FULLPEL_MV full_mv,
                                      int64_t threshold) {
  if (av2_num_planes(&cpi->common) <= 1) return false;
  const int ss_x = xd->plane[AVM_PLANE_U].subsampling_x;
  const int ss_y = xd->plane[AVM_PLANE_U].subsampling_y;
  const BLOCK_SIZE uv_bsize = get_plane_block_size(bsize, ss_x, ss_y);
  if (uv_bsize >= BLOCK_SIZES_ALL) return false;
  const int uv_row = y_row >> ss_y;
  const int uv_col = y_col >> ss_x;
  const int uv_mv_row = full_mv.row >> ss_y;
  const int uv_mv_col = full_mv.col >> ss_x;
  const unsigned int uv_thresh = (unsigned int)AVMMAX(4, threshold >> 3);
  for (int plane = AVM_PLANE_U; plane <= AVM_PLANE_V; ++plane) {
    const int src_stride = x->plane[plane].src.stride;
    const int dst_stride = xd->plane[plane].pre[0].stride;
    const uint16_t *src_uv =
        x->plane[plane].src.buf + uv_row * src_stride + uv_col;
    const uint16_t *dst_uv = xd->plane[plane].pre[0].buf +
                             (uv_row + uv_mv_row) * dst_stride +
                             (uv_col + uv_mv_col);
    unsigned int sse = 0;
    const unsigned int var =
        cpi->fn_ptr[uv_bsize].vf(src_uv, src_stride, dst_uv, dst_stride, &sse);
    const unsigned int pp_var =
        ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[uv_bsize]);
    if (pp_var > uv_thresh) return true;
  }
  return false;
}

// Assigns the largest square coding blocks (>= BLOCK_8X8) that fit inside the
// active tile/frame boundary for a static superblock, avoiding full
// variance-tree construction on right/bottom border superblocks.
static void set_static_sb_partitioning(AV2_COMP *cpi,
                                       const TileInfo *const tile, int mi_row,
                                       int mi_col, BLOCK_SIZE bsize) {
  if (mi_row >= tile->mi_row_end || mi_col >= tile->mi_col_end) return;
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  if ((mi_col + bw <= tile->mi_col_end && mi_row + bh <= tile->mi_row_end) ||
      bsize <= BLOCK_8X8) {
    set_block_size(cpi, mi_row, mi_col, bsize);
    return;
  }
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
  if (subsize >= BLOCK_SIZES_ALL) return;
  const int hbs_w = bw >> 1;
  const int hbs_h = bh >> 1;
  set_static_sb_partitioning(cpi, tile, mi_row, mi_col, subsize);
  set_static_sb_partitioning(cpi, tile, mi_row, mi_col + hbs_w, subsize);
  set_static_sb_partitioning(cpi, tile, mi_row + hbs_h, mi_col, subsize);
  set_static_sb_partitioning(cpi, tile, mi_row + hbs_h, mi_col + hbs_w,
                             subsize);
}

// Accumulates zero-MV Y/U/V SAD across the visible fitting square sub-blocks
// of a right/bottom boundary superblock without reading out-of-frame padding.
static void compute_visible_sb_zero_mv_sad(
    const AV2_COMP *cpi, const MACROBLOCK *x, const TileInfo *const tile,
    int mi_row, int mi_col, int y_mi_off, int x_mi_off, BLOCK_SIZE bsize,
    unsigned int *vis_y_sad, unsigned int vis_uv_sad[2], int *vis_mi_area,
    bool *fully_inside_crop) {
  if (!*fully_inside_crop) return;
  const int cur_mi_row = mi_row + y_mi_off;
  const int cur_mi_col = mi_col + x_mi_off;
  if (cur_mi_row >= tile->mi_row_end || cur_mi_col >= tile->mi_col_end) return;

  const AV2_COMMON *const cm = &cpi->common;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  if ((cur_mi_col + bw <= tile->mi_col_end &&
       cur_mi_row + bh <= tile->mi_row_end) ||
      bsize <= BLOCK_8X8) {
    if ((cur_mi_col + bw) * MI_SIZE > cm->width ||
        (cur_mi_row + bh) * MI_SIZE > cm->height) {
      *fully_inside_crop = false;
      return;
    }
    const int y_px_off = y_mi_off << MI_SIZE_LOG2;
    const int x_px_off = x_mi_off << MI_SIZE_LOG2;
    const struct macroblock_plane *const py = &x->plane[AVM_PLANE_Y];
    const struct macroblockd_plane *const pdy = &xd->plane[AVM_PLANE_Y];
    *vis_y_sad += cpi->fn_ptr[bsize].sdf(
        py->src.buf + y_px_off * py->src.stride + x_px_off, py->src.stride,
        pdy->pre[0].buf + y_px_off * pdy->pre[0].stride + x_px_off,
        pdy->pre[0].stride);
    *vis_mi_area += bw * bh;

    const int num_planes = av2_num_planes(cm);
    for (int plane = AVM_PLANE_U; plane < num_planes; ++plane) {
      const struct macroblock_plane *const p = &x->plane[plane];
      const struct macroblockd_plane *const pd = &xd->plane[plane];
      const BLOCK_SIZE bs =
          get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
      if (bs >= BLOCK_SIZES_ALL) {
        *fully_inside_crop = false;
        return;
      }
      const int uv_y = y_px_off >> pd->subsampling_y;
      const int uv_x = x_px_off >> pd->subsampling_x;
      vis_uv_sad[plane - AVM_PLANE_U] += cpi->fn_ptr[bs].sdf(
          p->src.buf + uv_y * p->src.stride + uv_x, p->src.stride,
          pd->pre[0].buf + uv_y * pd->pre[0].stride + uv_x, pd->pre[0].stride);
    }
    return;
  }

  const BLOCK_SIZE subsize = get_partition_subsize(bsize, PARTITION_SPLIT);
  if (subsize >= BLOCK_SIZES_ALL) return;
  const int hbs_w = bw >> 1;
  const int hbs_h = bh >> 1;
  compute_visible_sb_zero_mv_sad(cpi, x, tile, mi_row, mi_col, y_mi_off,
                                 x_mi_off, subsize, vis_y_sad, vis_uv_sad,
                                 vis_mi_area, fully_inside_crop);
  compute_visible_sb_zero_mv_sad(cpi, x, tile, mi_row, mi_col, y_mi_off,
                                 x_mi_off + hbs_w, subsize, vis_y_sad,
                                 vis_uv_sad, vis_mi_area, fully_inside_crop);
  compute_visible_sb_zero_mv_sad(cpi, x, tile, mi_row, mi_col, y_mi_off + hbs_h,
                                 x_mi_off, subsize, vis_y_sad, vis_uv_sad,
                                 vis_mi_area, fully_inside_crop);
  compute_visible_sb_zero_mv_sad(cpi, x, tile, mi_row, mi_col, y_mi_off + hbs_h,
                                 x_mi_off + hbs_w, subsize, vis_y_sad,
                                 vis_uv_sad, vis_mi_area, fully_inside_crop);
}

// Computes superblock-level chroma sensitivity flags (0: off, 1: active,
// 2: borderline to refine per coding block) on U and V planes for non-RD
// inter mode search, ported from AV1's chroma_check().
static void set_color_sensitivity_sb(AV2_COMP *cpi, MACROBLOCK *x,
                                     BLOCK_SIZE bsize, int mi_row, int mi_col,
                                     unsigned int y_sad, FULLPEL_MV full_mv,
                                     unsigned int uv_sad_out[2],
                                     bool *uv_sad_valid_out) {
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int num_planes = av2_num_planes(cm);
  memset(x->color_sensitivity_sb, 0, sizeof(x->color_sensitivity_sb));
  memset(x->color_sensitivity_sb_alt, 0, sizeof(x->color_sensitivity_sb_alt));
  uv_sad_out[0] = 0;
  uv_sad_out[1] = 0;
  *uv_sad_valid_out = (num_planes <= 1);
  if (num_planes <= 1 || y_sad == UINT_MAX) return;

  const SOURCE_SAD source_sad_level = x->source_sad_level;
  const bool is_screen = (cpi->oxcf.tune_cfg.content == AVM_CONTENT_SCREEN);
  int shift_upper_limit = 1;
  int shift_lower_limit = 3;
  int fac_uv = 6;

  if (!is_screen) {
    fac_uv = (cm->width * cm->height >= RESOLUTION_1080P) ? 3 : 5;
  }
  if (is_screen && cpi->rc.high_source_sad) {
    shift_lower_limit = 7;
  } else if (is_screen && cpi->rc.frame_source_sad > 10000 &&
             source_sad_level > kLowSad) {
    shift_lower_limit = 8;
    shift_upper_limit = 3;
  } else if (!is_screen && cpi->sf.rt_sf.use_color_sensitivity >= 2) {
    shift_upper_limit = 2;
    shift_lower_limit = (source_sad_level > kMedSad) ? 5 : 4;
  } else if (source_sad_level >= kMedSad && x->source_variance != UINT_MAX &&
             x->source_variance > 500 && cm->width * cm->height >= 640 * 360) {
    shift_upper_limit = 2;
    shift_lower_limit = (source_sad_level > kMedSad) ? 5 : 4;
  } else if (!is_screen) {
    shift_lower_limit = 4;
  }

  bool all_uv_valid = true;
  for (int plane = AVM_PLANE_U; plane < num_planes; ++plane) {
    const struct macroblock_plane *const p = &x->plane[plane];
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE bs =
        get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
    if (bs >= BLOCK_SIZES_ALL) {
      all_uv_valid = false;
      continue;
    }

    const int uv_mv_row = full_mv.row >> pd->subsampling_y;
    const int uv_mv_col = full_mv.col >> pd->subsampling_x;
    const uint16_t *const ref_uv =
        pd->pre[0].buf + uv_mv_row * pd->pre[0].stride + uv_mv_col;
    const unsigned int uv_sad = cpi->fn_ptr[bs].sdf(p->src.buf, p->src.stride,
                                                    ref_uv, pd->pre[0].stride);
    uv_sad_out[plane - 1] = uv_sad;

    if (uv_sad == 0) {
      x->color_sensitivity_sb[plane - 1] = 0;
    } else if (uv_sad > (y_sad >> shift_upper_limit)) {
      x->color_sensitivity_sb[plane - 1] = 1;
    } else if (uv_sad < (y_sad >> shift_lower_limit)) {
      x->color_sensitivity_sb[plane - 1] = 0;
    } else {
      x->color_sensitivity_sb[plane - 1] = 2;
    }
  }
  *uv_sad_valid_out = all_uv_valid;

  // Skip secondary-reference chroma SAD checks when the VBP reference already
  // has an exact zero-MV match across all Y, U, and V planes; mark secondary
  // references as borderline (2) so block-level refinement runs lazily if
  // mode search ever falls through to a secondary reference.
  if (y_sad == 0 && all_uv_valid && uv_sad_out[0] == 0 && uv_sad_out[1] == 0 &&
      full_mv.row == 0 && full_mv.col == 0) {
    memset(x->color_sensitivity_sb_alt, 2, sizeof(x->color_sensitivity_sb_alt));
    return;
  }

  // Evaluate zero-MV chroma sensitivity on all usable secondary DPB references
  // (including pure-chroma changes where luma source_sad_level == kZeroSad).
  for (MV_REFERENCE_FRAME rf = 0;
       rf < cm->ref_frames_info.num_total_refs && rf < INTER_REFS_PER_FRAME;
       ++rf) {
    if (rf == x->vbp_ref_frame || !is_vbp_ref_enabled(cm, rf)) continue;
    if (cm->bru.enabled && rf == cm->bru.update_ref_idx) continue;
    if (cm->cur_frame && cm->cur_frame->refs_restricted_status[rf]) continue;

    const YV12_BUFFER_CONFIG *yv12_ref = get_ref_frame_yv12_buf(cm, rf);
    bool scaled_ref = false;
    if (yv12_ref != NULL && (yv12_ref->y_crop_height != cm->height ||
                             yv12_ref->y_crop_width != cm->width)) {
      yv12_ref = av2_get_scaled_ref_frame(cpi, rf);
      scaled_ref = true;
    }
    if (yv12_ref == NULL) continue;

    const struct scale_factors *const ref_sf =
        scaled_ref ? NULL : get_ref_scale_factors_const(cm, rf);
    struct buf_2d ref_y_pre;
    const struct macroblockd_plane *const pd_y = &xd->plane[AVM_PLANE_Y];
    setup_pred_plane(&ref_y_pre, yv12_ref->buffers[AVM_PLANE_Y],
                     yv12_ref->widths[0], yv12_ref->heights[0],
                     yv12_ref->crop_widths[0], yv12_ref->crop_heights[0],
                     yv12_ref->strides[0], mi_row, mi_col, ref_sf,
                     pd_y->subsampling_x, pd_y->subsampling_y, NULL);
    const unsigned int ref_y_sad = cpi->fn_ptr[bsize].sdf(
        x->plane[AVM_PLANE_Y].src.buf, x->plane[AVM_PLANE_Y].src.stride,
        ref_y_pre.buf, ref_y_pre.stride);

    for (int plane = AVM_PLANE_U; plane < num_planes; ++plane) {
      const struct macroblock_plane *const p = &x->plane[plane];
      const struct macroblockd_plane *const pd = &xd->plane[plane];
      const BLOCK_SIZE bs =
          get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
      if (bs >= BLOCK_SIZES_ALL) continue;

      struct buf_2d ref_uv_pre;
      setup_pred_plane(&ref_uv_pre, yv12_ref->buffers[plane],
                       yv12_ref->widths[1], yv12_ref->heights[1],
                       yv12_ref->crop_widths[1], yv12_ref->crop_heights[1],
                       yv12_ref->strides[1], mi_row, mi_col, ref_sf,
                       pd->subsampling_x, pd->subsampling_y, NULL);
      const unsigned int ref_uv_sad = cpi->fn_ptr[bs].sdf(
          p->src.buf, p->src.stride, ref_uv_pre.buf, ref_uv_pre.stride);
      x->color_sensitivity_sb_alt[rf][plane - 1] =
          (uint64_t)ref_uv_sad * fac_uv > ref_y_sad;
    }
  }
}

void av2_choose_var_based_partitioning(AV2_COMP *cpi,
                                       const TileInfo *const tile,
                                       ThreadData *td, MACROBLOCK *x,
                                       int mi_row, int mi_col,
                                       unsigned int source_sad) {
  AV2_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  // Flat array representing quadtree nodes up to 16x16 level for 256x256 SB
  PART_EVAL_STATUS force_split[VBP_FORCE_SPLIT_NODES] = { PART_EVAL_ALL };
  int avg_64x64;
  int max_var_32x32[16];
  int min_var_32x32[16];
  int var_32x32;
  int var_64x64;
  int min_var_64x64 = INT_MAX;
  int max_var_64x64 = 0;
  int avg_16x16[16][4];
  int maxvar_16x16[16][4];
  int minvar_16x16[16][4];
  const uint16_t *src_buf;
  const uint16_t *dst_buf;
  int dst_stride;
  bool is_key_frame = frame_is_intra_only(cm);
  bool scaled_ref_last = false;
  const int is_360p_or_smaller = cm->width * cm->height <= RESOLUTION_360P;
  const SOURCE_SAD source_sad_level = x->source_sad_level;
  const bool is_screen = (cpi->oxcf.tune_cfg.content == AVM_CONTENT_SCREEN);

  assert(cm->seq_params.sb_size == BLOCK_64X64 ||
         cm->seq_params.sb_size == BLOCK_128X128 ||
         cm->seq_params.sb_size == BLOCK_256X256);
  const bool is_small_sb = (cm->seq_params.sb_size == BLOCK_64X64);
  const int num_64x64_blocks = (cm->seq_params.sb_size == BLOCK_64X64)     ? 1
                               : (cm->seq_params.sb_size == BLOCK_128X128) ? 4
                                                                           : 16;

  unsigned int y_sad = UINT_MAX;
  x->sb_me_partition = 0;
  x->sb_me_mv.as_int = 0;
  x->vbp_ref_frame = NONE_FRAME;
  x->force_zeromv_skip_for_sb = 0;
  if (cpi->sf.rt_sf.use_nonrd_pickmode && cpi->sf.rt_sf.use_color_sensitivity) {
    memset(x->color_sensitivity_sb, 0, sizeof(x->color_sensitivity_sb));
    memset(x->color_sensitivity_sb_alt, 0, sizeof(x->color_sensitivity_sb_alt));
  }

  int64_t thresholds[6];
  const uint8_t *const map =
      cm->seg.enabled ? cpi->enc_seg.map : cm->last_frame_seg_map;
  const int segment_id =
      map ? get_segment_id(&cm->mi_params, map, cm->sb_size, mi_row, mi_col)
          : 0;
  const int is_segment_id_boosted =
      cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ && cm->seg.enabled &&
      cyclic_refresh_segment_id_boosted(segment_id);
  const int qindex =
      (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ && cm->seg.enabled)
          ? av2_get_qindex(&cm->seg, segment_id, cm->quant_params.base_qindex,
                           cm->seq_params.bit_depth)
          : cm->quant_params.base_qindex;

  int64_t thresholds_base[6];
  int64_t *leaves_thresholds = thresholds;
  set_vbp_thresholds(cpi, thresholds, qindex, source_sad_level,
                     is_segment_id_boosted);
  if (qindex != cm->quant_params.base_qindex || is_segment_id_boosted) {
    set_vbp_thresholds(cpi, thresholds_base, cm->quant_params.base_qindex,
                       source_sad_level, 0);
    leaves_thresholds = thresholds_base;
  }

  src_buf = x->plane[AVM_PLANE_Y].src.buf;
  int src_stride = x->plane[AVM_PLANE_Y].src.stride;
  FULLPEL_MV full_mv = kZeroFullMv;

  if (!frame_is_intra_only(cm)) {
    const MV_REFERENCE_FRAME last_frame = get_primary_vbp_ref(cpi);
    const YV12_BUFFER_CONFIG *ref = get_ref_frame_yv12_buf(cm, last_frame);
    if (ref == NULL) {
      is_key_frame = true;
    } else if (ref->y_crop_height != cm->height ||
               ref->y_crop_width != cm->width) {
      scaled_ref_last = true;
      const YV12_BUFFER_CONFIG *ref_scaled =
          av2_get_scaled_ref_frame(cpi, last_frame);
      if (ref_scaled == NULL) is_key_frame = true;
    }
  }

  if (!is_key_frame) {
    setup_planes(cpi, x, &y_sad, mi_row, mi_col, scaled_ref_last);

    MB_MODE_INFO *mi = xd->mi[0];
    // Use reference SB directly (offset by full-pel MV when non-zero).
    full_mv = get_fullmv_from_mv(&mi->mv[0].as_mv);
    dst_stride = xd->plane[AVM_PLANE_Y].pre[0].stride;
    dst_buf = xd->plane[AVM_PLANE_Y].pre[0].buf + full_mv.row * dst_stride +
              full_mv.col;
    unsigned int uv_sad[2] = { 0, 0 };
    bool uv_sad_valid = false;
    if (cpi->sf.rt_sf.use_nonrd_pickmode &&
        cpi->sf.rt_sf.use_color_sensitivity) {
      set_color_sensitivity_sb(cpi, x, cm->seq_params.sb_size, mi_row, mi_col,
                               y_sad, full_mv, uv_sad, &uv_sad_valid);
    }

    const int block_width = mi_size_wide[cm->seq_params.sb_size];
    const int block_height = mi_size_high[cm->seq_params.sb_size];
    // When a slide reverts or uncovers static background (source_sad > 0
    // relative to the previous frame), setup_planes() may pick a distant LTR
    // (ref_frame_distance > 2) with an exact zero-MV luma match (y_sad == 0).
    // Relax VBP thresholds to kZeroSad and allow the whole-SB early exit so
    // matching LTR superblocks are not over-partitioned.
    const bool used_lt_ref =
        cpi->sf.rt_sf.rtc_use_lt_ref && is_inter_ref_frame(x->vbp_ref_frame) &&
        cm->ref_frames_info.ref_frame_distance[x->vbp_ref_frame] > 2 &&
        y_sad == 0;
    if (used_lt_ref && source_sad_level > kZeroSad) {
      set_vbp_thresholds(cpi, thresholds, qindex, kZeroSad,
                         is_segment_id_boosted);
      if (qindex != cm->quant_params.base_qindex || is_segment_id_boosted) {
        set_vbp_thresholds(cpi, thresholds_base, cm->quant_params.base_qindex,
                           kZeroSad, 0);
      }
    }

    const bool can_force_zmv_skip =
        cpi->sf.rt_sf.use_nonrd_pickmode &&
        cpi->sf.rt_sf.part_early_exit_zeromv && !xd->lossless[segment_id] &&
        !scaled_ref_last && full_mv.row == 0 && full_mv.col == 0 &&
        is_inter_ref_frame(x->vbp_ref_frame);

    if (source_sad == 0 || used_lt_ref) {
      // Without BRU or cyclic refresh, non-RD mode search lacks recursive
      // sub-block transform partitioning inside a whole 128x128 superblock, so
      // require y_sad == 0 so variance-based partitioning splits and cleans up
      // post-transition text residuals once before converging to exact zero-MV
      // skip on subsequent static frames.
      const bool allow_scr_nonzero_ysad =
          !(cpi->sf.rt_sf.use_nonrd_pickmode && !cm->bru.enabled &&
            cpi->oxcf.q_cfg.aq_mode == NO_AQ);
      if (mi_col + block_width <= tile->mi_col_end &&
          mi_row + block_height <= tile->mi_row_end) {
        if (y_sad == 0 ||
            (is_screen && allow_scr_nonzero_ysad && !is_segment_id_boosted &&
             y_sad < (unsigned int)(block_width * block_height * 2))) {
          const bool exact_zero_yuv =
              y_sad == 0 && uv_sad_valid && uv_sad[0] == 0 && uv_sad[1] == 0;
          if (exact_zero_yuv ||
              !is_chroma_var_high(cpi, x, xd, cm->seq_params.sb_size, 0, 0,
                                  full_mv, thresholds[1])) {
            set_block_size(cpi, mi_row, mi_col, cm->seq_params.sb_size);
            if (can_force_zmv_skip) {
              if (exact_zero_yuv) {
                x->force_zeromv_skip_for_sb = 2;
              } else if (source_sad == 0 && y_sad == 0 &&
                         !is_segment_id_boosted &&
                         (is_screen || cpi->rc.frames_since_key > 30)) {
                x->force_zeromv_skip_for_sb = 1;
              }
            }
            return;
          }
        }
      } else if (can_force_zmv_skip) {
        unsigned int vis_y_sad = 0;
        unsigned int vis_uv_sad[2] = { 0, 0 };
        int vis_mi_area = 0;
        bool fully_inside_crop = true;
        compute_visible_sb_zero_mv_sad(
            cpi, x, tile, mi_row, mi_col, 0, 0, cm->seq_params.sb_size,
            &vis_y_sad, vis_uv_sad, &vis_mi_area, &fully_inside_crop);
        if (fully_inside_crop && vis_mi_area > 0 && vis_y_sad == 0 &&
            vis_uv_sad[0] == 0 && vis_uv_sad[1] == 0) {
          x->color_sensitivity_sb[0] = 0;
          x->color_sensitivity_sb[1] = 0;
          set_static_sb_partitioning(cpi, tile, mi_row, mi_col,
                                     cm->seq_params.sb_size);
          x->force_zeromv_skip_for_sb = 2;
          return;
        }
      }
    }
  } else {
    dst_buf = NULL;
    dst_stride = 0;
  }

  // TODO(jianj-g, marpan-max): check for early exits.
  VP128x128 *vt;
  VP128x128 vt_local;
  if (cm->seq_params.sb_size == BLOCK_256X256) {
    const int num_128x128_blocks = 4;
    if (td->vt128x128 == NULL) {
      AVM_CHECK_MEM_ERROR(
          xd->error_info, td->vt128x128,
          avm_malloc(sizeof(*td->vt128x128) * num_128x128_blocks));
    }
    vt = td->vt128x128;
  } else {
    vt = &vt_local;
  }
  if (td->vt64x64 == NULL) {
    AVM_CHECK_MEM_ERROR(xd->error_info, td->vt64x64,
                        avm_malloc(sizeof(*td->vt64x64) * num_64x64_blocks));
  }
  if (cm->seq_params.sb_size == BLOCK_256X256) {
    for (int i = 0; i < 4; i++) {
      vt[i].split = &td->vt64x64[i * 4];
    }
  } else {
    vt->split = td->vt64x64;
  }

  // Fill in the entire tree of 8x8 (for inter frames) or 4x4 (for key frames)
  // variances for splits.
  fill_variance_tree_leaves(cpi, x, vt, force_split, avg_16x16, maxvar_16x16,
                            minvar_16x16, leaves_thresholds, src_buf,
                            src_stride, dst_buf, dst_stride, is_key_frame,
                            cm->seq_params.sb_size);

  const VPartOffsets offsets = get_vpart_offsets(cm->seq_params.sb_size);
  const int offset_64x64 = offsets.offset_64x64;
  const int offset_32x32 = offsets.offset_32x32;
  const int offset_16x16 = offsets.offset_16x16;
  const bool is_lowres = (cm->width * cm->height <= RESOLUTION_288P);
  const int block_width = mi_size_wide[cm->seq_params.sb_size];
  const int block_height = mi_size_high[cm->seq_params.sb_size];
  const bool check_chroma =
      !is_key_frame &&
      (source_sad == 0 || y_sad == 0 ||
       (is_screen && y_sad < (unsigned int)(block_width * block_height * 4)));

  avg_64x64 = 0;
  for (int blk64_idx = 0; blk64_idx < num_64x64_blocks; ++blk64_idx) {
    VP64x64 *vt64 = (cm->seq_params.sb_size == BLOCK_256X256)
                        ? &vt[blk64_idx >> 2].split[blk64_idx & 3]
                        : &vt->split[blk64_idx];
    max_var_32x32[blk64_idx] = 0;
    min_var_32x32[blk64_idx] = INT_MAX;
    const int blk64_scale_idx = blk64_idx << 2;
    for (int lvl1_idx = 0; lvl1_idx < 4; lvl1_idx++) {
      const int lvl1_scale_idx = (blk64_scale_idx + lvl1_idx) << 2;
      for (int lvl2_idx = 0; lvl2_idx < 4; lvl2_idx++) {
        if (!is_key_frame) continue;
        VP16x16 *vtemp = &vt64->split[lvl1_idx].split[lvl2_idx];
        for (int lvl3_idx = 0; lvl3_idx < 4; lvl3_idx++)
          fill_variance_tree(&vtemp->split[lvl3_idx], BLOCK_8X8);
        fill_variance_tree(vtemp, BLOCK_16X16);
        // If variance of this 16x16 block is above the threshold, force block
        // to split. This also forces a split on the upper levels.
        get_variance(&vtemp->part_variances.none);
        if (vtemp->part_variances.none.variance > thresholds[4]) {
          const int split_index = offset_16x16 + lvl1_scale_idx + lvl2_idx;
          const int current_qindex = cm->quant_params.base_qindex;
          force_split[split_index] = get_part_eval_based_on_sub_blk_var(
              vtemp, thresholds[2], current_qindex, is_lowres, is_screen);
          force_split_ancestors(force_split, offset_64x64, offset_32x32,
                                blk64_idx, lvl1_idx);
        }
      }
      fill_variance_tree(&vt64->split[lvl1_idx], BLOCK_32X32);
      // If variance of this 32x32 block is above the threshold, or if its above
      // (some threshold of) the average variance over the sub-16x16 blocks,
      // then force this block to split. This also forces a split on the upper
      // (64x64) level.
      if (force_split[offset_32x32 + blk64_scale_idx + lvl1_idx] ==
          PART_EVAL_ALL) {
        get_variance(&vt64->split[lvl1_idx].part_variances.none);
        var_32x32 = vt64->split[lvl1_idx].part_variances.none.variance;
        max_var_32x32[blk64_idx] = AVMMAX(var_32x32, max_var_32x32[blk64_idx]);
        min_var_32x32[blk64_idx] = AVMMIN(var_32x32, min_var_32x32[blk64_idx]);
        const int max_min_var_16X16_diff = (maxvar_16x16[blk64_idx][lvl1_idx] -
                                            minvar_16x16[blk64_idx][lvl1_idx]);

        if (var_32x32 > thresholds[3] ||
            (!is_key_frame && var_32x32 > (thresholds[3] >> 1) &&
             var_32x32 > (avg_16x16[blk64_idx][lvl1_idx] >> 1))) {
          force_split_ancestors(force_split, offset_64x64, offset_32x32,
                                blk64_idx, lvl1_idx);
        } else if (!is_key_frame && !is_screen) {
          const bool lowres_nonscreen =
              is_lowres && !cpi->is_screen_content_type;
          const int maxvar_thresh_shift = lowres_nonscreen ? 2 : 4;
          const int minvar_scale_shift = lowres_nonscreen ? 3 : 2;
          if ((max_min_var_16X16_diff > (thresholds[3] >> 1) &&
               maxvar_16x16[blk64_idx][lvl1_idx] > thresholds[3]) ||
              (is_360p_or_smaller &&
               maxvar_16x16[blk64_idx][lvl1_idx] >
                   (thresholds[3] >> maxvar_thresh_shift) &&
               maxvar_16x16[blk64_idx][lvl1_idx] >
                   ((int64_t)minvar_16x16[blk64_idx][lvl1_idx]
                    << minvar_scale_shift))) {
            force_split_ancestors(force_split, offset_64x64, offset_32x32,
                                  blk64_idx, lvl1_idx);
          }
        }
      }
    }
    if (force_split[offset_64x64 + blk64_idx] == PART_EVAL_ALL) {
      fill_variance_tree(vt64, BLOCK_64X64);
      get_variance(&vt64->part_variances.none);
      var_64x64 = vt64->part_variances.none.variance;
      max_var_64x64 = AVMMAX(var_64x64, max_var_64x64);
      min_var_64x64 = AVMMIN(var_64x64, min_var_64x64);
      // If the difference of the max-min variances of sub-blocks or max
      // variance of a sub-block is above some threshold of then force this
      // block to split.
      const int max_min_var_32x32_diff =
          max_var_32x32[blk64_idx] - min_var_32x32[blk64_idx];
      const int check_max_var = max_var_32x32[blk64_idx] > (thresholds[2] >> 1);
      const int64_t set_threshold = 3 * (thresholds[2] >> 3);

      if (!is_key_frame && max_min_var_32x32_diff > set_threshold &&
          check_max_var) {
        force_split[offset_64x64 + blk64_idx] = PART_EVAL_ONLY_SPLIT;
        force_split[0] = PART_EVAL_ONLY_SPLIT;
      } else if (check_chroma) {
        const int x64_idx = (cm->seq_params.sb_size == BLOCK_256X256)
                                ? ((blk64_idx & 3) << 6)
                                : GET_BLK_IDX_X(blk64_idx, 6);
        const int y64_idx = (cm->seq_params.sb_size == BLOCK_256X256)
                                ? ((blk64_idx >> 2) << 6)
                                : GET_BLK_IDX_Y(blk64_idx, 6);
        if (mi_col + (x64_idx >> 2) + 16 <= tile->mi_col_end &&
            mi_row + (y64_idx >> 2) + 16 <= tile->mi_row_end &&
            is_chroma_var_high(cpi, x, xd, BLOCK_64X64, y64_idx, x64_idx,
                               full_mv, thresholds[2])) {
          force_split[offset_64x64 + blk64_idx] = PART_EVAL_ONLY_SPLIT;
          force_split[0] = PART_EVAL_ONLY_SPLIT;
        }
      }
      avg_64x64 += var_64x64;
    }
    if (is_small_sb) force_split[0] = PART_EVAL_ONLY_SPLIT;
  }

  if (force_split[0] == PART_EVAL_ALL &&
      cm->seq_params.sb_size != BLOCK_256X256) {
    fill_variance_tree(vt, BLOCK_128X128);
    get_variance(&vt->part_variances.none);
    if (!is_key_frame &&
        check_128x128_split(vt->part_variances.none.variance, avg_64x64,
                            min_var_64x64, max_var_64x64, thresholds[1])) {
      force_split[0] = PART_EVAL_ONLY_SPLIT;
    }
  }

  if (cm->seq_params.sb_size == BLOCK_256X256) {
    PART_EVAL_STATUS force_split_128[4] = { PART_EVAL_ALL, PART_EVAL_ALL,
                                            PART_EVAL_ALL, PART_EVAL_ALL };
    for (int blk128_idx = 0; blk128_idx < 4; ++blk128_idx) {
      bool all_64_eval_all = true;
      for (int s64 = 0; s64 < 4; ++s64) {
        int b64 = (blk128_idx << 2) + s64;
        if (force_split[offset_64x64 + b64] != PART_EVAL_ALL) {
          all_64_eval_all = false;
          force_split_128[blk128_idx] = PART_EVAL_ONLY_SPLIT;
          break;
        }
      }

      if (all_64_eval_all) {
        fill_variance_tree(&vt[blk128_idx], BLOCK_128X128);
        get_variance(&vt[blk128_idx].part_variances.none);

        int avg_64x64_128 = 0;
        int max_var_64x64_128 = 0;
        int min_var_64x64_128 = INT_MAX;
        for (int s64 = 0; s64 < 4; ++s64) {
          VP64x64 *vt64 = &vt[blk128_idx].split[s64];
          int v64 = vt64->part_variances.none.variance;
          avg_64x64_128 += v64;
          max_var_64x64_128 = AVMMAX(v64, max_var_64x64_128);
          min_var_64x64_128 = AVMMIN(v64, min_var_64x64_128);
        }
        if (!is_key_frame &&
            check_128x128_split(vt[blk128_idx].part_variances.none.variance,
                                avg_64x64_128, min_var_64x64_128,
                                max_var_64x64_128, thresholds[1])) {
          force_split_128[blk128_idx] = PART_EVAL_ONLY_SPLIT;
        }
      }
    }

    for (int blk128_idx = 0; blk128_idx < 4; ++blk128_idx) {
      const int x128_idx = GET_BLK_IDX_X(blk128_idx, 5);
      const int y128_idx = GET_BLK_IDX_Y(blk128_idx, 5);
      if (mi_col + x128_idx >= tile->mi_col_end ||
          mi_row + y128_idx >= tile->mi_row_end)
        continue;
      if (mi_col + x128_idx + 32 <= tile->mi_col_end &&
          mi_row + y128_idx + 32 <= tile->mi_row_end &&
          set_vt_partitioning(cpi, xd, tile, &vt[blk128_idx], BLOCK_128X128,
                              mi_row + y128_idx, mi_col + x128_idx,
                              thresholds[1], BLOCK_16X16,
                              force_split_128[blk128_idx])) {
        continue;
      }
      for (int sub64_idx = 0; sub64_idx < 4; ++sub64_idx) {
        const int blk64_idx = (blk128_idx << 2) + sub64_idx;
        const int x64_idx = x128_idx + GET_BLK_IDX_X(sub64_idx, 4);
        const int y64_idx = y128_idx + GET_BLK_IDX_Y(sub64_idx, 4);
        VP64x64 *vt64 = &vt[blk128_idx].split[sub64_idx];
        set_vt_partitioning_64x64(cpi, xd, tile, vt64, mi_row, mi_col, x64_idx,
                                  y64_idx, blk64_idx, thresholds, force_split,
                                  offsets);
      }
    }
  } else {
    if (is_small_sb || mi_col + 32 > tile->mi_col_end ||
        mi_row + 32 > tile->mi_row_end ||
        !set_vt_partitioning(cpi, xd, tile, vt, BLOCK_128X128, mi_row, mi_col,
                             thresholds[1], BLOCK_16X16, force_split[0])) {
      for (int blk64_idx = 0; blk64_idx < num_64x64_blocks; ++blk64_idx) {
        const int x64_idx = GET_BLK_IDX_X(blk64_idx, 4);
        const int y64_idx = GET_BLK_IDX_Y(blk64_idx, 4);
        VP64x64 *vt64 = &vt->split[blk64_idx];
        set_vt_partitioning_64x64(cpi, xd, tile, vt64, mi_row, mi_col, x64_idx,
                                  y64_idx, blk64_idx, thresholds, force_split,
                                  offsets);
      }
    }
  }
}
