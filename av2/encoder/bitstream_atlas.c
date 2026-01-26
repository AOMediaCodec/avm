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

#include "avm/avm_encoder.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/binary_codes_writer.h"
#include "avm_dsp/bitwriter_buffer.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/bitops.h"
#include "avm_ports/mem_ops.h"
#include "avm_ports/system_state.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "avm_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"
#include "av2/encoder/bitstream.h"
#include "av2/encoder/tokenize.h"

static uint32_t write_ats_region_info(struct AtlasRegionInfo *atlas_reg_params,
                                      struct avm_write_bit_buffer *wb) {
  avm_wb_write_uvlc(wb, atlas_reg_params->ats_num_region_columns_minus_1);
  avm_wb_write_uvlc(wb, atlas_reg_params->ats_num_region_rows_minus_1);

  avm_wb_write_bit(wb, atlas_reg_params->ats_uniform_spacing_flag);
  if (!atlas_reg_params->ats_uniform_spacing_flag) {
    for (int i = 0; i < atlas_reg_params->ats_num_region_columns_minus_1 + 1;
         i++) {
      avm_wb_write_uvlc(wb, atlas_reg_params->ats_column_width_minus_1[i]);
    }
    for (int i = 0; i < atlas_reg_params->ats_num_region_rows_minus_1 + 1;
         i++) {
      avm_wb_write_uvlc(wb, atlas_reg_params->ats_row_height_minus_1[i]);
    }
  } else {
    avm_wb_write_uvlc(wb, atlas_reg_params->ats_region_width_minus_1);
    avm_wb_write_uvlc(wb, atlas_reg_params->ats_region_height_minus_1);
  }
  return 0;
}

static uint32_t write_ats_multistream_alpha_atlas_info(
    struct AtlasBasicInfo *ats_basic_info, struct avm_write_bit_buffer *wb) {
  avm_wb_write_uvlc(wb, ats_basic_info->ats_atlas_width);
  avm_wb_write_uvlc(wb, ats_basic_info->ats_atlas_height);
  avm_wb_write_uvlc(wb, ats_basic_info->ats_num_atlas_segments_minus_1);

  avm_wb_write_bit(wb, ats_basic_info->ats_alpha_segments_present_flag);
  avm_wb_write_bit(wb, ats_basic_info->ats_background_info_present_flag);
  if (ats_basic_info->ats_background_info_present_flag == 1) {
    avm_wb_write_literal(wb, ats_basic_info->ats_background_red_value, 8);
    avm_wb_write_literal(wb, ats_basic_info->ats_background_green_value, 8);
    avm_wb_write_literal(wb, ats_basic_info->ats_background_blue_value, 8);
  }

  const int NumSegments = ats_basic_info->ats_num_atlas_segments_minus_1 + 1;
  for (int i = 0; i < NumSegments; i++) {
    avm_wb_write_literal(wb, ats_basic_info->ats_input_stream_id[i], 5);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_top_left_pos_x[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_top_left_pos_y[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_width[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_height[i]);
    if (ats_basic_info->ats_alpha_segments_present_flag == 1 &&
        i < NumSegments - 1)
      avm_wb_write_uvlc(wb, ats_basic_info->ats_alpha_segment_flag[i]);
  }
  return 0;
}

static uint32_t write_ats_multistream_atlas_info(
    struct AtlasBasicInfo *ats_basic_info, struct avm_write_bit_buffer *wb) {
  avm_wb_write_uvlc(wb, ats_basic_info->ats_atlas_width);
  avm_wb_write_uvlc(wb, ats_basic_info->ats_atlas_height);
  avm_wb_write_uvlc(wb, ats_basic_info->ats_num_atlas_segments_minus_1);

  avm_wb_write_bit(wb, ats_basic_info->ats_background_info_present_flag);
  if (ats_basic_info->ats_background_info_present_flag == 1) {
    avm_wb_write_literal(wb, ats_basic_info->ats_background_red_value, 8);
    avm_wb_write_literal(wb, ats_basic_info->ats_background_green_value, 8);
    avm_wb_write_literal(wb, ats_basic_info->ats_background_blue_value, 8);
  }

  const int NumSegments = ats_basic_info->ats_num_atlas_segments_minus_1 + 1;
  for (int i = 0; i < NumSegments; i++) {
    avm_wb_write_literal(wb, ats_basic_info->ats_input_stream_id[i], 5);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_top_left_pos_x[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_top_left_pos_y[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_width[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_height[i]);
  }
  return 0;
}

static uint32_t write_ats_basic_info(struct AtlasBasicInfo *ats_basic_info,
                                     struct avm_write_bit_buffer *wb) {
  avm_wb_write_bit(wb, ats_basic_info->ats_stream_id_present);

  avm_wb_write_uvlc(wb, ats_basic_info->ats_atlas_width);
  avm_wb_write_uvlc(wb, ats_basic_info->ats_atlas_height);
  avm_wb_write_uvlc(wb, ats_basic_info->ats_num_atlas_segments_minus_1);

  const int NumSegments = ats_basic_info->ats_num_atlas_segments_minus_1 + 1;
  for (int i = 0; i < NumSegments; i++) {
    if (ats_basic_info->ats_stream_id_present) {
      avm_wb_write_literal(wb, ats_basic_info->ats_input_stream_id[i], 5);
    }
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_top_left_pos_x[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_top_left_pos_y[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_width[i]);
    avm_wb_write_uvlc(wb, ats_basic_info->ats_segment_height[i]);
  }
  return 0;
}

static uint32_t write_ats_region_to_segment_mapping(
    struct AtlasRegionToSegmentMapping *ats_reg_seg_map, int NumRegionsInAtlas,
    struct avm_write_bit_buffer *wb) {
  avm_wb_write_bit(wb,
                   ats_reg_seg_map->ats_single_region_per_atlas_segment_flag);

  if (!ats_reg_seg_map->ats_single_region_per_atlas_segment_flag) {
    avm_wb_write_uvlc(wb, ats_reg_seg_map->ats_num_atlas_segments_minus_1);
    const int NumSegments = ats_reg_seg_map->ats_num_atlas_segments_minus_1 + 1;
    for (int i = 0; i < NumSegments; i++) {
      avm_wb_write_uvlc(wb, ats_reg_seg_map->ats_top_left_region_column[i]);
      avm_wb_write_uvlc(wb, ats_reg_seg_map->ats_top_left_region_row[i]);
      // check if ats_bottom_right_region_column is derived correctly
      assert(ats_reg_seg_map->ats_bottom_right_region_column[i] ==
             (ats_reg_seg_map->ats_top_left_region_column[i] +
              ats_reg_seg_map->ats_bottom_right_region_column_offset[i]));
      avm_wb_write_uvlc(
          wb, ats_reg_seg_map->ats_bottom_right_region_column_offset[i]);
      // check if ats_bottom_right_region_row is derived correctly
      assert(ats_reg_seg_map->ats_bottom_right_region_row[i] ==
             (ats_reg_seg_map->ats_top_left_region_row[i] +
              ats_reg_seg_map->ats_bottom_right_region_row_offset[i]));
      avm_wb_write_uvlc(wb,
                        ats_reg_seg_map->ats_bottom_right_region_row_offset[i]);
    }
  } else
    ats_reg_seg_map->ats_num_atlas_segments_minus_1 = NumRegionsInAtlas - 1;
  return 0;
}

static uint32_t write_ats_label_segment_info(AV2_COMP *cpi, int NumSegments,
                                             struct avm_write_bit_buffer *wb) {
  assert(NumSegments >= 0);
  struct AtlasLabelSegmentInfo *ats_label =
      &cpi->common.atlas_params.ats_label_seg;

  avm_wb_write_bit(wb, ats_label->ats_signalled_atlas_segment_ids_flag);
  if (ats_label->ats_signalled_atlas_segment_ids_flag) {
    for (int i = 0; i < NumSegments; i++) {
      avm_wb_write_literal(wb, ats_label->ats_atlas_segment_id[i],
                           ATLAS_LABEL_SEG_ID_BITS);
    }
  }
  return 0;
}

uint32_t av2_write_atlas_segment_info_obu(AV2_COMP *cpi, uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  struct AtlasSegmentInfo *atlas_params = &cpi->common.atlas_params;

  avm_wb_write_literal(&wb, atlas_params->atlas_segment_id, 3);
  avm_wb_write_uvlc(&wb, atlas_params->atlas_segment_mode_idc);
  int num_segments = -1;  // invalid
  if (atlas_params->atlas_segment_mode_idc == ENHANCED_ATLAS) {
    write_ats_region_info(&atlas_params->ats_reg_params, &wb);
    write_ats_region_to_segment_mapping(
        &atlas_params->ats_reg_seg_map,
        atlas_params->ats_reg_params.NumRegionsInAtlas, &wb);
    num_segments =
        atlas_params->ats_reg_seg_map.ats_num_atlas_segments_minus_1 + 1;
  } else if (atlas_params->atlas_segment_mode_idc == BASIC_ATLAS) {
    write_ats_basic_info(atlas_params->ats_basic_info, &wb);
    num_segments =
        atlas_params->ats_basic_info->ats_num_atlas_segments_minus_1 + 1;
  } else if (atlas_params->atlas_segment_mode_idc == SINGLE_ATLAS) {
    atlas_params->ats_reg_seg_map.ats_num_atlas_segments_minus_1 = 0;
    num_segments = 0;
    avm_wb_write_uvlc(&wb, atlas_params->ats_nominal_width_minus1);
    avm_wb_write_uvlc(&wb, atlas_params->ats_nominal_height_minus1);
  } else if (atlas_params->atlas_segment_mode_idc == MULTISTREAM_ATLAS) {
    write_ats_multistream_atlas_info(atlas_params->ats_basic_info, &wb);
    num_segments =
        atlas_params->ats_basic_info->ats_num_atlas_segments_minus_1 + 1;
  } else if (atlas_params->atlas_segment_mode_idc == MULTISTREAM_ALPHA_ATLAS) {
    write_ats_multistream_alpha_atlas_info(atlas_params->ats_basic_info, &wb);
    num_segments =
        atlas_params->ats_basic_info->ats_num_atlas_segments_minus_1 + 1;
  }
  // Label each atlas segment
  write_ats_label_segment_info(cpi, num_segments, &wb);
#if CONFIG_F414_OBU_EXTENSION
  avm_wb_write_bit(&wb, atlas_params->ats_extension_present_flag);
  assert(!atlas_params->ats_extension_present_flag);
#endif  // CONFIG_F414_OBU_EXTENSION
  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}

int av2_set_atlas_segment_info_params(AV2_COMP *cpi,
                                      struct AtlasSegmentInfo *atlas,
                                      int xlayer_id) {
  (void)xlayer_id;
  AV2_COMMON *cm = &cpi->common;
  memcpy(atlas, cm->atlas, sizeof(struct AtlasSegmentInfo));
  atlas->atlas_segment_id = 1;
  atlas->valid = 1;
  atlas->obu_xlayer_id = xlayer_id;
  return 0;
}
