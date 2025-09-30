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

#include "config/aom_config.h"
#include "aom_dsp/bitreader_buffer.h"
#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/common/timing.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/obu.h"
#include "av1/common/enums.h"

static uint32_t read_ats_region_info(struct AtlasRegionInfo *atlas_reg_params,
                                     int xlayerId, int xAId,
                                     struct aom_read_bit_buffer *rb) {
  atlas_reg_params->ats_num_region_columns_minus_1[xlayerId][xAId] =
      aom_rb_read_uvlc(rb);
  atlas_reg_params->ats_num_region_rows_minus_1[xlayerId][xAId] =
      aom_rb_read_uvlc(rb);
  atlas_reg_params->ats_uniform_spacing_flag[xlayerId][xAId] =
      aom_rb_read_bit(rb);

  if (!atlas_reg_params->ats_uniform_spacing_flag[xlayerId][xAId]) {
    for (int i = 0;
         i <
         atlas_reg_params->ats_num_region_columns_minus_1[xlayerId][xAId] + 1;
         i++) {
      atlas_reg_params->ats_column_width_minus_1[xlayerId][xAId][i] =
          aom_rb_read_uvlc(rb);
      atlas_reg_params->AtlasWidth[xlayerId][xAId] +=
          (atlas_reg_params->ats_column_width_minus_1[xlayerId][xAId][i] + 1);
    }
    for (int i = 0;
         i < atlas_reg_params->ats_num_region_rows_minus_1[xlayerId][xAId] + 1;
         i++) {
      atlas_reg_params->ats_row_height_minus_1[xlayerId][xAId][i] =
          aom_rb_read_uvlc(rb);
      atlas_reg_params->AtlasHeight[xlayerId][xAId] +=
          (atlas_reg_params->ats_row_height_minus_1[xlayerId][xAId][i] + 1);
    }
  } else {
    atlas_reg_params->ats_region_width_minus_1[xlayerId][xAId] =
        aom_rb_read_uvlc(rb);
    atlas_reg_params->ats_region_height_minus_1[xlayerId][xAId] =
        aom_rb_read_uvlc(rb);

    atlas_reg_params->AtlasWidth[xlayerId][xAId] =
        (atlas_reg_params->ats_region_width_minus_1[xlayerId][xAId] + 1) *
        (atlas_reg_params->ats_num_region_columns_minus_1[xlayerId][xAId] + 1);

    atlas_reg_params->AtlasHeight[xlayerId][xAId] =
        (atlas_reg_params->ats_region_height_minus_1[xlayerId][xAId] + 1) *
        (atlas_reg_params->ats_num_region_rows_minus_1[xlayerId][xAId] + 1);
  }
  atlas_reg_params->NumRegionsInAtlas[xlayerId][xAId] =
      (atlas_reg_params->ats_num_region_columns_minus_1[xlayerId][xAId] + 1) *
      (atlas_reg_params->ats_num_region_rows_minus_1[xlayerId][xAId] + 1);

  return 0;
}

static uint32_t read_ats_basic_atlas_info(
    struct AtlasBasicInfo *ats_basic_atlas_info, int obu_xLayer_id, int xAId,
    struct aom_read_bit_buffer *rb) {
  ats_basic_atlas_info->ats_stream_id_present[obu_xLayer_id][xAId] =
      aom_rb_read_bit(rb);
  ats_basic_atlas_info->ats_atlas_width[obu_xLayer_id][xAId] =
      aom_rb_read_uvlc(rb);
  ats_basic_atlas_info->ats_atlas_height[obu_xLayer_id][xAId] =
      aom_rb_read_uvlc(rb);
  ats_basic_atlas_info->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
      aom_rb_read_uvlc(rb);

  ats_basic_atlas_info->AtlasWidth[obu_xLayer_id][xAId] =
      ats_basic_atlas_info->ats_atlas_width[obu_xLayer_id][xAId];
  ats_basic_atlas_info->AtlasHeight[obu_xLayer_id][xAId] =
      ats_basic_atlas_info->ats_atlas_height[obu_xLayer_id][xAId];

  int NumSegments = ats_basic_atlas_info
                        ->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
                    1;
  for (int i = 0; i < NumSegments; i++) {
    if (ats_basic_atlas_info->ats_stream_id_present[obu_xLayer_id][xAId]) {
      ats_basic_atlas_info->ats_input_stream_id[obu_xLayer_id][xAId][i] =
          aom_rb_read_literal(rb, 5);
    }
    ats_basic_atlas_info->ats_segment_top_left_pos_x[obu_xLayer_id][xAId][i] =
        aom_rb_read_uvlc(rb);
    ats_basic_atlas_info->ats_segment_top_left_pos_y[obu_xLayer_id][xAId][i] =
        aom_rb_read_uvlc(rb);
    ats_basic_atlas_info->ats_segment_width[obu_xLayer_id][xAId][i] =
        aom_rb_read_uvlc(rb);
    ats_basic_atlas_info->ats_segment_height[obu_xLayer_id][xAId][i] =
        aom_rb_read_uvlc(rb);
  }
  return 0;
}

static uint32_t read_ats_region_to_segment_mapping(
    struct AtlasRegionToSegmentMapping *ats_reg_seg_map, int obu_xLayer_id,
    int xAId, int NumRegionsInAtlas, struct aom_read_bit_buffer *rb) {
  ats_reg_seg_map
      ->ats_single_region_per_atlas_segment_flag[obu_xLayer_id][xAId] =
      aom_rb_read_bit(rb);

  if (!ats_reg_seg_map
           ->ats_single_region_per_atlas_segment_flag[obu_xLayer_id][xAId]) {
    ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
        aom_rb_read_uvlc(rb);
    int NumSegments =
        ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
        1;
    for (int i = 0; i < NumSegments; i++) {
      ats_reg_seg_map->ats_top_left_region_column[obu_xLayer_id][xAId][i] =
          aom_rb_read_uvlc(rb);
      ats_reg_seg_map->ats_top_left_region_row[obu_xLayer_id][xAId][i] =
          aom_rb_read_uvlc(rb);
      ats_reg_seg_map
          ->ats_bottom_right_region_column_off[obu_xLayer_id][xAId][i] =
          aom_rb_read_uvlc(rb);
      ats_reg_seg_map->ats_bottom_right_region_row_off[obu_xLayer_id][xAId][i] =
          aom_rb_read_uvlc(rb);
    }
  } else
    ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
        NumRegionsInAtlas - 1;

  return 0;
}

static uint32_t read_ats_label_segment_info(struct AV1Decoder *pbi,
                                            int xLayerId, int xAId,
                                            struct aom_read_bit_buffer *rb) {
  struct AtlasLabelSegmentInfo *ats_label =
      &pbi->common.atlas_params.ats_label_seg;
  struct AtlasRegionToSegmentMapping *ats_reg =
      &pbi->common.atlas_params.ats_reg_seg_map;

  ats_label->ats_signalled_atlas_segment_ids_flag[xLayerId][xAId] =
      aom_rb_read_bit(rb);
  if (ats_label->ats_signalled_atlas_segment_ids_flag[xLayerId][xAId]) {
    int NumSegments =
        ats_reg->ats_num_atlas_segments_minus_1[xLayerId][xAId] + 1;
    for (int i = 0; i < NumSegments; i++) {
      ats_label->ats_atlas_segment_id[i] =
          aom_rb_read_literal(rb, ATLAS_LABEL_SEG_ID_BITS);
      ats_label->AtlasSegmentIDToIndex[xLayerId][xAId]
                                      [ats_label->ats_atlas_segment_id[i]] = i;
      ats_label->AtlasSegmentIndexToID[xLayerId][xAId][i] =
          ats_label->ats_atlas_segment_id[i];
    }
  } else {
    int NumAtlasSegments =
        ats_reg->ats_num_atlas_segments_minus_1[xLayerId][xAId] + 1;
    for (int i = 0; i < NumAtlasSegments; i++) {
      ats_label->ats_atlas_segment_id[i] = i;
      ats_label->AtlasSegmentIDToIndex[xLayerId][xAId][i] = i;
      ats_label->AtlasSegmentIndexToID[xLayerId][xAId][i] = i;
    }
  }
  return 0;
}

static uint32_t read_ats_multistream_atlas_info(
    struct AV1Decoder *pbi, struct aom_read_bit_buffer *rb) {
  aom_internal_error(
      &pbi->common.error, AOM_CODEC_UNSUP_FEATURE,
      "Implementation not avaialbe for read_ats_multistream_atlas_info()");
  (void)rb;
  return 0;
}

uint32_t av1_read_atlas_segment_info_obu(struct AV1Decoder *pbi,
                                         int obu_xLayer_id,
                                         struct aom_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;
  assert(rb->error_handler);

  int atlas_segment_id = aom_rb_read_literal(rb, ATLAS_SEG_ID_BITS);

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
    if (pbi->atlas_counter >= MAX_NUM_ATLAS_SEG_ID) {
      aom_internal_error(
          &pbi->common.error, AOM_CODEC_ERROR,
          "Failed to decode in av1_read_atlas_segment_info_obu()");
    }
    atlas_params = &pbi->atlas_list[pbi->atlas_counter];
    pbi->atlas_counter++;
  }

  atlas_params->atlas_segment_id[obu_xLayer_id] = atlas_segment_id;
  int xAId = atlas_params->atlas_segment_id[obu_xLayer_id];
  atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] =
      aom_rb_read_uvlc(rb);
  if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] >=
      ATLAS_TYPES) {
    aom_internal_error(&pbi->common.error, AOM_CODEC_ERROR,
                       "Unsupported atlas_segment_mode_idc, whose value should "
                       "be smaller than ATLAS_TYPES");
  }
  if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] == ENH_ATLAS) {
    read_ats_region_info(&atlas_params->ats_reg_params, obu_xLayer_id, xAId,
                         rb);
    read_ats_region_to_segment_mapping(
        &atlas_params->ats_reg_seg_map, obu_xLayer_id, xAId,
        atlas_params->ats_reg_params.NumRegionsInAtlas[obu_xLayer_id][xAId],
        rb);
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             BASIC_ATLAS) {
    read_ats_basic_atlas_info(atlas_params->ats_basic_atlas_info, obu_xLayer_id,
                              xAId, rb);
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             SINGLE_ATLAS) {
    atlas_params->ats_reg_seg_map
        .ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] = 0;
    atlas_params->ats_nominal_width_minus1[obu_xLayer_id][xAId] =
        aom_rb_read_uvlc(rb);
    atlas_params->ats_nominal_height_minus1[obu_xLayer_id][xAId] =
        aom_rb_read_uvlc(rb);
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             MULTISTREAM_ATLAS) {
    read_ats_multistream_atlas_info(pbi, rb);
  }
  // Label each atlas segment
  read_ats_label_segment_info(pbi, obu_xLayer_id, xAId, rb);

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
