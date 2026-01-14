/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
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

#include "config/avm_config.h"
#include "avm_dsp/bitreader_buffer.h"
#include "av2/common/common.h"
#include "av2/common/obu_util.h"
#include "av2/common/timing.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
#include "av2/decoder/obu.h"
#include "av2/common/enums.h"

static uint32_t read_ats_region_info(struct AV2Decoder *pbi,
                                     struct AtlasRegionInfo *atlas_reg_params,
                                     int xlayerId, int xAId,
                                     struct avm_read_bit_buffer *rb) {
  atlas_reg_params->ats_num_region_columns_minus_1[xlayerId][xAId] =
      avm_rb_read_uvlc(rb);
  const int num_regions_column =
      atlas_reg_params->ats_num_region_columns_minus_1[xlayerId][xAId] + 1;
  if (num_regions_column > MAX_ATLAS_REGIONS) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                       "The value of ats_num_region_columns_minus_1[%d][%d] "
                       "shall be in the range of 0 to %d, inclusive.",
                       xlayerId, xAId, (MAX_ATLAS_REGIONS - 1));
  }

  atlas_reg_params->ats_num_region_rows_minus_1[xlayerId][xAId] =
      avm_rb_read_uvlc(rb);
  const int num_regions_row =
      atlas_reg_params->ats_num_region_rows_minus_1[xlayerId][xAId] + 1;
  if (num_regions_row > MAX_ATLAS_REGIONS) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                       "The value of ats_num_region_rows_minus_1[%d][%d] shall "
                       "be in the range of 0 to %d, inclusive.",
                       xlayerId, xAId, (MAX_ATLAS_REGIONS - 1));
  }
  atlas_reg_params->ats_uniform_spacing_flag[xlayerId][xAId] =
      avm_rb_read_bit(rb);

  if (!atlas_reg_params->ats_uniform_spacing_flag[xlayerId][xAId]) {
    for (int i = 0; i < num_regions_column; i++) {
      atlas_reg_params->ats_column_width_minus_1[xlayerId][xAId][i] =
          avm_rb_read_uvlc(rb);
      atlas_reg_params->AtlasWidth[xlayerId][xAId] +=
          (atlas_reg_params->ats_column_width_minus_1[xlayerId][xAId][i] + 1);
    }
    for (int i = 0; i < num_regions_row; i++) {
      atlas_reg_params->ats_row_height_minus_1[xlayerId][xAId][i] =
          avm_rb_read_uvlc(rb);
      atlas_reg_params->AtlasHeight[xlayerId][xAId] +=
          (atlas_reg_params->ats_row_height_minus_1[xlayerId][xAId][i] + 1);
    }
  } else {
    atlas_reg_params->ats_region_width_minus_1[xlayerId][xAId] =
        avm_rb_read_uvlc(rb);
    atlas_reg_params->ats_region_height_minus_1[xlayerId][xAId] =
        avm_rb_read_uvlc(rb);

    atlas_reg_params->AtlasWidth[xlayerId][xAId] =
        (atlas_reg_params->ats_region_width_minus_1[xlayerId][xAId] + 1) *
        num_regions_column;

    atlas_reg_params->AtlasHeight[xlayerId][xAId] =
        (atlas_reg_params->ats_region_height_minus_1[xlayerId][xAId] + 1) *
        num_regions_row;
  }
  atlas_reg_params->NumRegionsInAtlas[xlayerId][xAId] =
      num_regions_column * num_regions_row;

  return 0;
}

static uint32_t read_ats_basic_info(struct AV2Decoder *pbi,
                                    struct AtlasBasicInfo *ats_basic_info,
                                    int obu_xLayer_id, int xAId,
                                    struct avm_read_bit_buffer *rb) {
  ats_basic_info->ats_stream_id_present[obu_xLayer_id][xAId] =
      avm_rb_read_bit(rb);
  ats_basic_info->ats_atlas_width[obu_xLayer_id][xAId] = avm_rb_read_uvlc(rb);
  ats_basic_info->ats_atlas_height[obu_xLayer_id][xAId] = avm_rb_read_uvlc(rb);
  ats_basic_info->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
      avm_rb_read_uvlc(rb);

  ats_basic_info->AtlasWidth[obu_xLayer_id][xAId] =
      ats_basic_info->ats_atlas_width[obu_xLayer_id][xAId];
  ats_basic_info->AtlasHeight[obu_xLayer_id][xAId] =
      ats_basic_info->ats_atlas_height[obu_xLayer_id][xAId];

  int NumSegments =
      ats_basic_info->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] + 1;
  if (NumSegments > MAX_NUM_ATLAS_SEGMENTS) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                       "The value of ats_num_atlas_segments_minus_1[%d][%d] "
                       "shall be in the range of 0 to %d, inclusive.",
                       obu_xLayer_id, xAId, (MAX_NUM_ATLAS_SEGMENTS - 1));
  }
  for (int i = 0; i < NumSegments; i++) {
    if (ats_basic_info->ats_stream_id_present[obu_xLayer_id][xAId]) {
      ats_basic_info->ats_input_stream_id[obu_xLayer_id][xAId][i] =
          avm_rb_read_literal(rb, 5);
    }
    ats_basic_info->ats_segment_top_left_pos_x[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_top_left_pos_y[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_width[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_height[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
  }
  return 0;
}

static uint32_t read_ats_region_to_segment_mapping(
    struct AV2Decoder *pbi, struct AtlasRegionToSegmentMapping *ats_reg_seg_map,
    int obu_xLayer_id, int xAId, int NumRegionsInAtlas,
    struct avm_read_bit_buffer *rb) {
  ats_reg_seg_map
      ->ats_single_region_per_atlas_segment_flag[obu_xLayer_id][xAId] =
      avm_rb_read_bit(rb);

  if (!ats_reg_seg_map
           ->ats_single_region_per_atlas_segment_flag[obu_xLayer_id][xAId]) {
    ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
        avm_rb_read_uvlc(rb);
    int NumSegments =
        ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
        1;
    if (NumSegments > MAX_NUM_ATLAS_SEGMENTS) {
      avm_internal_error(&pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                         "The value of ats_num_atlas_segments_minus_1[%d][%d] "
                         "shall be in the range of 0 to %d, inclusive.",
                         obu_xLayer_id, xAId, (MAX_NUM_ATLAS_SEGMENTS - 1));
    }
    for (int i = 0; i < NumSegments; i++) {
      // read top-left row and column
      ats_reg_seg_map->ats_top_left_region_column[obu_xLayer_id][xAId][i] =
          avm_rb_read_uvlc(rb);
      ats_reg_seg_map->ats_top_left_region_row[obu_xLayer_id][xAId][i] =
          avm_rb_read_uvlc(rb);
      // read row and column offsets
      ats_reg_seg_map
          ->ats_bottom_right_region_column_offset[obu_xLayer_id][xAId][i] =
          avm_rb_read_uvlc(rb);
      ats_reg_seg_map
          ->ats_bottom_right_region_row_offset[obu_xLayer_id][xAId][i] =
          avm_rb_read_uvlc(rb);
      // derive ats_bottom_right_region_column
      ats_reg_seg_map->ats_bottom_right_region_column[obu_xLayer_id][xAId][i] =
          ats_reg_seg_map->ats_top_left_region_column[obu_xLayer_id][xAId][i] +
          ats_reg_seg_map
              ->ats_bottom_right_region_column_offset[obu_xLayer_id][xAId][i];
      // derive ats_bottom_right_region_row
      ats_reg_seg_map->ats_bottom_right_region_row[obu_xLayer_id][xAId][i] =
          ats_reg_seg_map->ats_top_left_region_row[obu_xLayer_id][xAId][i] +
          ats_reg_seg_map
              ->ats_bottom_right_region_row_offset[obu_xLayer_id][xAId][i];
    }
  } else {
    if (NumRegionsInAtlas > MAX_NUM_ATLAS_SEGMENTS) {
      avm_internal_error(
          &pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
          "If ats_single_region_per_atlas_segment_flag[%d][%d] is enabled, "
          "NumRegionsInAtlas shall not be larger than %d.",
          obu_xLayer_id, xAId, MAX_NUM_ATLAS_SEGMENTS);
    }
    ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
        NumRegionsInAtlas - 1;
  }

  return 0;
}

static uint32_t read_ats_label_segment_info(
    struct AV2Decoder *pbi, struct AtlasSegmentInfo *atlas_params, int xLayerId,
    int xAId, int NumSegments, struct avm_read_bit_buffer *rb) {
  assert(NumSegments >= 0);
  // TODO(hegilmez/spaluri): cleanup pbi or use common structure
  // (&pbi->common.atlas_params.ats_label_seg)
  (void)pbi;
  struct AtlasLabelSegmentInfo *ats_label = &atlas_params->ats_label_seg;

  ats_label->ats_signalled_atlas_segment_ids_flag[xLayerId][xAId] =
      avm_rb_read_bit(rb);
  if (ats_label->ats_signalled_atlas_segment_ids_flag[xLayerId][xAId]) {
    for (int i = 0; i < NumSegments; i++) {
      ats_label->ats_atlas_segment_id[i] =
          avm_rb_read_literal(rb, ATLAS_LABEL_SEG_ID_BITS);
      ats_label->AtlasSegmentIDToIndex[xLayerId][xAId]
                                      [ats_label->ats_atlas_segment_id[i]] = i;
      ats_label->AtlasSegmentIndexToID[xLayerId][xAId][i] =
          ats_label->ats_atlas_segment_id[i];
    }
  } else {
    for (int i = 0; i < NumSegments; i++) {
      ats_label->ats_atlas_segment_id[i] = i;
      ats_label->AtlasSegmentIDToIndex[xLayerId][xAId][i] = i;
      ats_label->AtlasSegmentIndexToID[xLayerId][xAId][i] = i;
    }
  }
  return 0;
}

static uint32_t read_ats_multistream_alpha_atlas_info(
    struct AV2Decoder *pbi, struct AtlasBasicInfo *ats_basic_info,
    int obu_xLayer_id, int xAId, struct avm_read_bit_buffer *rb) {
  ats_basic_info->ats_atlas_width[obu_xLayer_id][xAId] = avm_rb_read_uvlc(rb);
  ats_basic_info->ats_atlas_height[obu_xLayer_id][xAId] = avm_rb_read_uvlc(rb);
  ats_basic_info->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
      avm_rb_read_uvlc(rb);
  int NumSegments =
      ats_basic_info->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] + 1;
  if (NumSegments > MAX_NUM_ATLAS_SEGMENTS) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                       "The value of ats_num_atlas_segments_minus_1[%d][%d] "
                       "shall be in the range of 0 to %d, inclusive.",
                       obu_xLayer_id, xAId, (MAX_NUM_ATLAS_SEGMENTS - 1));
  }

  ats_basic_info->AtlasWidth[obu_xLayer_id][xAId] =
      ats_basic_info->ats_atlas_width[obu_xLayer_id][xAId];
  ats_basic_info->AtlasHeight[obu_xLayer_id][xAId] =
      ats_basic_info->ats_atlas_height[obu_xLayer_id][xAId];
  ats_basic_info->ats_alpha_segments_present_flag[obu_xLayer_id][xAId] =
      avm_rb_read_bit(rb);

  ats_basic_info->ats_background_info_present_flag[obu_xLayer_id][xAId] =
      avm_rb_read_bit(rb);
  if (ats_basic_info->ats_background_info_present_flag[obu_xLayer_id][xAId] ==
      1) {
    ats_basic_info->ats_background_red_value[obu_xLayer_id][xAId] =
        avm_rb_read_literal(rb, 8);
    ats_basic_info->ats_background_green_value[obu_xLayer_id][xAId] =
        avm_rb_read_literal(rb, 8);
    ats_basic_info->ats_background_blue_value[obu_xLayer_id][xAId] =
        avm_rb_read_literal(rb, 8);
  }

  for (int i = 0; i < NumSegments; i++) {
    ats_basic_info->ats_input_stream_id[obu_xLayer_id][xAId][i] =
        avm_rb_read_literal(rb, 5);
    ats_basic_info->ats_segment_top_left_pos_x[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_top_left_pos_y[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_width[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_height[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    if (ats_basic_info->ats_alpha_segments_present_flag[obu_xLayer_id][xAId] ==
            1 &&
        i < NumSegments - 1)
      ats_basic_info->ats_alpha_segment_flag[obu_xLayer_id][xAId][i] =
          avm_rb_read_bit(rb);
  }
  return 0;
}

static uint32_t read_ats_multistream_atlas_info(
    struct AV2Decoder *pbi, struct AtlasBasicInfo *ats_basic_info,
    int obu_xLayer_id, int xAId, struct avm_read_bit_buffer *rb) {
  ats_basic_info->ats_atlas_width[obu_xLayer_id][xAId] = avm_rb_read_uvlc(rb);
  ats_basic_info->ats_atlas_height[obu_xLayer_id][xAId] = avm_rb_read_uvlc(rb);
  ats_basic_info->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
      avm_rb_read_uvlc(rb);
  int NumSegments =
      ats_basic_info->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] + 1;
  if (NumSegments > MAX_NUM_ATLAS_SEGMENTS) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                       "The value of ats_num_atlas_segments_minus_1[%d][%d] "
                       "shall be in the range of 0 to %d, inclusive.",
                       obu_xLayer_id, xAId, (MAX_NUM_ATLAS_SEGMENTS - 1));
  }

  ats_basic_info->AtlasWidth[obu_xLayer_id][xAId] =
      ats_basic_info->ats_atlas_width[obu_xLayer_id][xAId];
  ats_basic_info->AtlasHeight[obu_xLayer_id][xAId] =
      ats_basic_info->ats_atlas_height[obu_xLayer_id][xAId];

  ats_basic_info->ats_background_info_present_flag[obu_xLayer_id][xAId] =
      avm_rb_read_bit(rb);
  if (ats_basic_info->ats_background_info_present_flag[obu_xLayer_id][xAId] ==
      1) {
    ats_basic_info->ats_background_red_value[obu_xLayer_id][xAId] =
        avm_rb_read_literal(rb, 8);
    ats_basic_info->ats_background_green_value[obu_xLayer_id][xAId] =
        avm_rb_read_literal(rb, 8);
    ats_basic_info->ats_background_blue_value[obu_xLayer_id][xAId] =
        avm_rb_read_literal(rb, 8);
  }

  for (int i = 0; i < NumSegments; i++) {
    ats_basic_info->ats_input_stream_id[obu_xLayer_id][xAId][i] =
        avm_rb_read_literal(rb, 5);
    ats_basic_info->ats_segment_top_left_pos_x[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_top_left_pos_y[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_width[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
    ats_basic_info->ats_segment_height[obu_xLayer_id][xAId][i] =
        avm_rb_read_uvlc(rb);
  }
  return 0;
}

uint32_t av2_read_atlas_segment_info_obu(struct AV2Decoder *pbi,
                                         int obu_xLayer_id,
                                         struct avm_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;
  assert(rb->error_handler);

  int atlas_segment_id = avm_rb_read_literal(rb, ATLAS_SEG_ID_BITS);

  struct AtlasSegmentInfo *atlas_params = NULL;
  int atlas_pos = -1;
  for (int i = 0; i < pbi->atlas_counter; i++) {
    if (pbi->atlas_list[i].atlas_segment_id[obu_xLayer_id] ==
        atlas_segment_id) {
      atlas_pos = i;
      break;
    }
  }
  if (atlas_pos != -1) {
    atlas_params = &pbi->atlas_list[atlas_pos];
  } else {
    const int idx = AVMMIN(pbi->atlas_counter, MAX_NUM_ATLAS_SEG_ID - 1);
    atlas_params = &pbi->atlas_list[idx];
    pbi->atlas_counter = AVMMIN(pbi->atlas_counter + 1, MAX_NUM_ATLAS_SEG_ID);
    atlas_params->ats_basic_info = &atlas_params->ats_basic_info_s;
  }

  atlas_params->atlas_segment_id[obu_xLayer_id] = atlas_segment_id;
  int xAId = atlas_params->atlas_segment_id[obu_xLayer_id];
  atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] =
      avm_rb_read_uvlc(rb);
  if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] >=
      ATLAS_TYPES) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_ERROR,
                       "Unsupported atlas_segment_mode_idc, whose value should "
                       "be smaller than ATLAS_TYPES");
  }
  int num_segments = -1;  // invalid
  if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
      ENHANCED_ATLAS) {
    read_ats_region_info(pbi, &atlas_params->ats_reg_params, obu_xLayer_id,
                         xAId, rb);
    read_ats_region_to_segment_mapping(
        pbi, &atlas_params->ats_reg_seg_map, obu_xLayer_id, xAId,
        atlas_params->ats_reg_params.NumRegionsInAtlas[obu_xLayer_id][xAId],
        rb);
    num_segments = atlas_params->ats_reg_seg_map
                       .ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
                   1;
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             BASIC_ATLAS) {
    read_ats_basic_info(pbi, atlas_params->ats_basic_info, obu_xLayer_id, xAId,
                        rb);
    num_segments = atlas_params->ats_basic_info
                       ->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
                   1;
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             SINGLE_ATLAS) {
    atlas_params->ats_reg_seg_map
        .ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] = 0;
    num_segments = 1;  // equivalent to (ats_num_atlas_segments_minus_1 + 1)
    atlas_params->ats_nominal_width_minus1[obu_xLayer_id][xAId] =
        avm_rb_read_uvlc(rb);
    atlas_params->ats_nominal_height_minus1[obu_xLayer_id][xAId] =
        avm_rb_read_uvlc(rb);
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             MULTISTREAM_ATLAS) {
    read_ats_multistream_atlas_info(pbi, atlas_params->ats_basic_info,
                                    obu_xLayer_id, xAId, rb);
    num_segments = atlas_params->ats_basic_info
                       ->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
                   1;
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             MULTISTREAM_ALPHA_ATLAS) {
    read_ats_multistream_alpha_atlas_info(pbi, atlas_params->ats_basic_info,
                                          obu_xLayer_id, xAId, rb);
    num_segments = atlas_params->ats_basic_info
                       ->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
                   1;
  }
  // Label each atlas segment
  read_ats_label_segment_info(pbi, atlas_params, obu_xLayer_id, xAId,
                              num_segments, rb);
#if CONFIG_F414_OBU_EXTENSION
  size_t bits_before_ext = rb->bit_offset - saved_bit_offset;
  atlas_params->ats_extension_present_flag = avm_rb_read_bit(rb);
  if (atlas_params->ats_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag) -
    // trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &pbi->common.error);
    if (extension_bits > 0) {
      // skip over the extension bits
      rb->bit_offset += extension_bits;
    } else {
      // No extension data present
    }
  }
#endif  // CONFIG_F414_OBU_EXTENSION
  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
