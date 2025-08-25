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
#include <limits.h>
#include <stdio.h>

#include "aom/aom_encoder.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/binary_codes_writer.h"
#include "aom_dsp/bitwriter_buffer.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/bitops.h"
#include "aom_ports/mem_ops.h"
#include "aom_ports/system_state.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"
#include "av1/encoder/bitstream.h"
#include "av1/encoder/tokenize.h"

static uint32_t write_ats_region_info(struct AtlasRegionInfo *atlas_reg_params,
                                      int xlayerId, int xAId,
                                      struct aom_write_bit_buffer *wb) {
  aom_wb_write_uvlc(
      wb, atlas_reg_params->ats_num_region_columns_minus_1[xlayerId][xAId]);
  aom_wb_write_uvlc(
      wb, atlas_reg_params->ats_num_region_rows_minus_1[xlayerId][xAId]);

  aom_wb_write_bit(wb,
                   atlas_reg_params->ats_uniform_spacing_flag[xlayerId][xAId]);
  if (!atlas_reg_params->ats_uniform_spacing_flag[xlayerId][xAId]) {
    for (int i = 0;
         i <
         atlas_reg_params->ats_num_region_columns_minus_1[xlayerId][xAId] + 1;
         i++) {
      aom_wb_write_uvlc(
          wb, atlas_reg_params->ats_column_width_minus_1[xlayerId][xAId][i]);
    }
    for (int i = 0;
         i < atlas_reg_params->ats_num_region_rows_minus_1[xlayerId][xAId] + 1;
         i++) {
      aom_wb_write_uvlc(
          wb, atlas_reg_params->ats_row_height_minus_1[xlayerId][xAId][i]);
    }
  } else {
    aom_wb_write_uvlc(
        wb, atlas_reg_params->ats_region_width_minus_1[xlayerId][xAId]);
    aom_wb_write_uvlc(
        wb, atlas_reg_params->ats_region_height_minus_1[xlayerId][xAId]);
  }
  return 0;
}

static uint32_t write_ats_multistream_atlas_info(
    struct AtlasBasicInfo *ats_basic_atlas_info, int obu_xLayer_id, int xAId,
    struct aom_write_bit_buffer *wb) {
  aom_wb_write_uvlc(wb,
                    ats_basic_atlas_info->ats_atlas_width[obu_xLayer_id][xAId]);
  aom_wb_write_uvlc(
      wb, ats_basic_atlas_info->ats_atlas_height[obu_xLayer_id][xAId]);
  aom_wb_write_uvlc(wb,
                    ats_basic_atlas_info
                        ->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId]);

  const int NumSegments =
      ats_basic_atlas_info
          ->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
      1;
  for (int i = 0; i < NumSegments; i++) {
    aom_wb_write_literal(
        wb, ats_basic_atlas_info->ats_input_stream_id[obu_xLayer_id][xAId][i],
        5);
    aom_wb_write_uvlc(wb,
                      ats_basic_atlas_info
                          ->ats_segment_top_left_pos_x[obu_xLayer_id][xAId][i]);
    aom_wb_write_uvlc(wb,
                      ats_basic_atlas_info
                          ->ats_segment_top_left_pos_y[obu_xLayer_id][xAId][i]);
    aom_wb_write_uvlc(
        wb, ats_basic_atlas_info->ats_segment_width[obu_xLayer_id][xAId][i]);
    aom_wb_write_uvlc(
        wb, ats_basic_atlas_info->ats_segment_height[obu_xLayer_id][xAId][i]);
  }
  return 0;
}

static uint32_t write_ats_basic_atlas_info(
    struct AtlasBasicInfo *ats_basic_atlas_info, int obu_xLayer_id, int xAId,
    struct aom_write_bit_buffer *wb) {
  aom_wb_write_bit(
      wb, ats_basic_atlas_info->ats_stream_id_present[obu_xLayer_id][xAId]);

  aom_wb_write_uvlc(wb,
                    ats_basic_atlas_info->ats_atlas_width[obu_xLayer_id][xAId]);
  aom_wb_write_uvlc(
      wb, ats_basic_atlas_info->ats_atlas_height[obu_xLayer_id][xAId]);
  aom_wb_write_uvlc(wb,
                    ats_basic_atlas_info
                        ->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId]);

  const int NumSegments =
      ats_basic_atlas_info
          ->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
      1;
  for (int i = 0; i < NumSegments; i++) {
    if (ats_basic_atlas_info->ats_stream_id_present[obu_xLayer_id][xAId]) {
      aom_wb_write_literal(
          wb, ats_basic_atlas_info->ats_input_stream_id[obu_xLayer_id][xAId][i],
          5);
    }
    aom_wb_write_uvlc(wb,
                      ats_basic_atlas_info
                          ->ats_segment_top_left_pos_x[obu_xLayer_id][xAId][i]);
    aom_wb_write_uvlc(wb,
                      ats_basic_atlas_info
                          ->ats_segment_top_left_pos_y[obu_xLayer_id][xAId][i]);
    aom_wb_write_uvlc(
        wb, ats_basic_atlas_info->ats_segment_width[obu_xLayer_id][xAId][i]);
    aom_wb_write_uvlc(
        wb, ats_basic_atlas_info->ats_segment_height[obu_xLayer_id][xAId][i]);
  }
  return 0;
}

static uint32_t write_ats_region_to_segment_mapping(
    struct AtlasRegionToSegmentMapping *ats_reg_seg_map, int obu_xLayer_id,
    int xAId, int NumRegionsInAtlas, struct aom_write_bit_buffer *wb) {
  aom_wb_write_bit(
      wb, ats_reg_seg_map
              ->ats_single_region_per_atlas_segment_flag[obu_xLayer_id][xAId]);

  if (!ats_reg_seg_map
           ->ats_single_region_per_atlas_segment_flag[obu_xLayer_id][xAId]) {
    aom_wb_write_uvlc(
        wb,
        ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId]);
    const int NumSegments =
        ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] +
        1;
    for (int i = 0; i < NumSegments; i++) {
      aom_wb_write_uvlc(
          wb,
          ats_reg_seg_map->ats_top_left_region_column[obu_xLayer_id][xAId][i]);
      aom_wb_write_uvlc(
          wb, ats_reg_seg_map->ats_top_left_region_row[obu_xLayer_id][xAId][i]);
      aom_wb_write_uvlc(
          wb, ats_reg_seg_map
                  ->ats_bottom_right_region_column_off[obu_xLayer_id][xAId][i]);
      aom_wb_write_uvlc(
          wb, ats_reg_seg_map
                  ->ats_bottom_right_region_row_off[obu_xLayer_id][xAId][i]);
    }
  } else
    ats_reg_seg_map->ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] =
        NumRegionsInAtlas - 1;
  return 0;
}

static uint32_t write_ats_label_segment_info(AV1_COMP *cpi, int xLayerId,
                                             int xAId,
                                             struct aom_write_bit_buffer *wb) {
  struct AtlasLabelSegmentInfo *ats_label =
      &cpi->common.atlas_params.ats_label_seg;
  struct AtlasRegionToSegmentMapping *ats_reg =
      &cpi->common.atlas_params.ats_reg_seg_map;

  aom_wb_write_bit(
      wb, ats_label->ats_signalled_atlas_segment_ids_flag[xLayerId][xAId]);
  if (ats_label->ats_signalled_atlas_segment_ids_flag[xLayerId][xAId]) {
    const int NumSegments =
        ats_reg->ats_num_atlas_segments_minus_1[xLayerId][xAId] + 1;
    for (int i = 0; i < NumSegments; i++) {
      aom_wb_write_literal(wb, ats_label->ats_atlas_segment_id[i],
                           ATLAS_LABEL_SEG_ID_BITS);
    }
  }
  return 0;
}

uint32_t av1_write_atlas_segment_info_obu(AV1_COMP *cpi, int obu_xLayer_id,
                                          uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  struct AtlasSegmentInfo *atlas_params = &cpi->common.atlas_params;

  aom_wb_write_literal(&wb, atlas_params->atlas_segment_id[obu_xLayer_id], 3);
  int xAId = atlas_params->atlas_segment_id[obu_xLayer_id];
  aom_wb_write_uvlc(&wb,
                    atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId]);
  if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] == ENH_ATLAS) {
    write_ats_region_info(&atlas_params->ats_reg_params, obu_xLayer_id, xAId,
                          &wb);
    write_ats_region_to_segment_mapping(
        &atlas_params->ats_reg_seg_map, obu_xLayer_id, xAId,
        atlas_params->ats_reg_params.NumRegionsInAtlas[obu_xLayer_id][xAId],
        &wb);
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             BASIC_ATLAS) {
    write_ats_basic_atlas_info(atlas_params->ats_basic_atlas_info,
                               obu_xLayer_id, xAId, &wb);
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             SINGLE_ATLAS) {
    atlas_params->ats_reg_seg_map
        .ats_num_atlas_segments_minus_1[obu_xLayer_id][xAId] = 0;
    aom_wb_write_uvlc(
        &wb, atlas_params->ats_nominal_width_minus1[obu_xLayer_id][xAId]);
    aom_wb_write_uvlc(
        &wb, atlas_params->ats_nominal_height_minus1[obu_xLayer_id][xAId]);
  } else if (atlas_params->atlas_segment_mode_idc[obu_xLayer_id][xAId] ==
             MULTISTREAM_ATLAS) {
    write_ats_multistream_atlas_info(atlas_params->ats_basic_atlas_info,
                                     obu_xLayer_id, xAId, &wb);
  }
  // Label each atlas segment
  write_ats_label_segment_info(cpi, obu_xLayer_id, xAId, &wb);

  av1_add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}

int av1_set_atlas_segment_info_params(AV1_COMP *cpi,
                                      struct AtlasSegmentInfo *atlas,
                                      int layer_id) {
  (void)layer_id;
  AV1_COMMON *cm = &cpi->common;
  memcpy(atlas, cm->atlas, sizeof(struct AtlasSegmentInfo));
  atlas->atlas_segment_id[0] = 1;
  return 0;
}
