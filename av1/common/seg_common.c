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

#include <assert.h>

#include "av1/common/av1_loopfilter.h"
#include "av1/common/blockd.h"
#include "av1/common/seg_common.h"
#include "av1/common/quant_common.h"

static const int seg_feature_data_signed[SEG_LVL_MAX] = { 1, 1, 1, 1, 1, 0, 0 };

static const int seg_feature_data_max[SEG_LVL_MAX] = {
  MAXQ, MAX_LOOP_FILTER, MAX_LOOP_FILTER, MAX_LOOP_FILTER, MAX_LOOP_FILTER, 0, 0
};

// These functions provide access to new segment level features.
// Eventually these function may be "optimized out" but for the moment,
// the coding mechanism is still subject to change so these provide a
// convenient single point of change.

void av1_clearall_segfeatures(struct segmentation *seg) {
  av1_zero(seg->feature_data);
  av1_zero(seg->feature_mask);
  seg->segid_preskip = 0;
  seg->last_active_segid = 0;
}

void av1_calculate_segdata(struct segmentation *seg) {
  seg->segid_preskip = 0;
  seg->last_active_segid = 0;

  const int max_seg_num = seg->enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
  for (int i = 0; i < max_seg_num; i++) {
    for (int j = 0; j < SEG_LVL_MAX; j++) {
      if (seg->feature_mask[i] & (1 << j)) {
        seg->segid_preskip |= (j >= SEG_LVL_SKIP);
        seg->last_active_segid = i;
      }
    }
  }
}

#if CONFIG_MULTI_LEVEL_SEGMENTATION
void av1_calculate_segdata_from_syntax(SegmentationInfoSyntax *seg_params) {
  seg_params->segid_preskip = 0;
  seg_params->last_active_segid = 0;

  const int max_seg_num =
      seg_params->enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
  for (int i = 0; i < max_seg_num; i++) {
    for (int j = 0; j < SEG_LVL_MAX; j++) {
      if (seg_params->feature_mask[i] & (1 << j)) {
        seg_params->segid_preskip |= (j >= SEG_LVL_SKIP);
        seg_params->last_active_segid = i;
      }
    }
  }
}

int av1_check_seg_equivalence(const struct SegmentationInfoSyntax *seg_params,
                              const struct segmentation *seg) {
  if (seg_params->enable_ext_seg != seg->enable_ext_seg) return 0;
  const int max_seg_num = seg->enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;

  for (int i = 0; i < max_seg_num; i++) {
    if (seg_params->feature_mask[i] != seg->feature_mask[i]) return 0;
  }

  for (int i = 0; i < max_seg_num; i++) {
    for (int j = 0; j < SEG_LVL_MAX; j++) {
      if (seg_params->feature_mask[i] & (i << j)) {
        if (seg_params->feature_data[i][j] != seg->feature_data[i][j]) return 0;
      }
    }
  }
  return 1;
}

void av1_reconstruct_seg_params(const struct SegmentationInfoSyntax *seg_params,
                                struct segmentation *seg) {
  const int max_seg_num =
      seg_params->enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
  seg->enable_ext_seg = seg_params->enable_ext_seg;
  seg->segid_preskip = seg_params->segid_preskip;
  seg->last_active_segid = seg_params->last_active_segid;

  // Copy feature masks and data
  for (int i = 0; i < max_seg_num; i++) {
    seg->feature_mask[i] = seg_params->feature_mask[i];
    for (int j = 0; j < SEG_LVL_MAX; j++) {
      seg->feature_data[i][j] = seg_params->feature_data[i][j];
    }
  }
}
#endif  // CONFIG_MULTI_LEVEL_SEGMENTATION

void av1_enable_segfeature(struct segmentation *seg, int segment_id,
                           SEG_LVL_FEATURES feature_id) {
  seg->feature_mask[segment_id] |= 1 << feature_id;
}

int av1_seg_feature_data_max(SEG_LVL_FEATURES feature_id) {
  return seg_feature_data_max[feature_id];
}

int av1_is_segfeature_signed(SEG_LVL_FEATURES feature_id) {
  return seg_feature_data_signed[feature_id];
}

// The 'seg_data' given for each segment can be either deltas (from the default
// value chosen for the frame) or absolute values.
//
// Valid range for abs values is (0-127 for MB_LVL_ALT_Q), (0-63 for
// SEGMENT_ALT_LF)
// Valid range for delta values are (+/-127 for MB_LVL_ALT_Q), (+/-63 for
// SEGMENT_ALT_LF)
//
// abs_delta = SEGMENT_DELTADATA (deltas) abs_delta = SEGMENT_ABSDATA (use
// the absolute values given).

void av1_set_segdata(struct segmentation *seg, int segment_id,
                     SEG_LVL_FEATURES feature_id, int seg_data) {
  if (seg_data < 0) {
    assert(seg_feature_data_signed[feature_id]);
    assert(-seg_data <= seg_feature_data_max[feature_id]);
  } else {
    assert(seg_data <= seg_feature_data_max[feature_id]);
  }

  seg->feature_data[segment_id][feature_id] = seg_data;
}

// TBD? Functions to read and write segment data with range / validity checking
