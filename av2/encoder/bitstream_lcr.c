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

#if !CONFIG_LCR_UPDATE
// TODO(hegilmez) to be specified, depending on profile, tier definitions
static int write_lcr_profile_tier_level(int isGlobal, int xId) {
#if MULTILAYER_HLS_REMOVE_LOGS
  (void)isGlobal;
  (void)xId;
#else
  printf(
      "write_lcr_profile_tier_level(isGlobal=%d,xId=%d): profile, tier, level "
      "definitions are not defined yet\n",
      isGlobal, xId);
#endif  // MULTILAYER_HLS_REMOVE_LOGS
  return 0;
}
#endif  // !CONFIG_LCR_UPDATE

#if CONFIG_LCR_UPDATE
// LCR aggregate profile tier level info syntax (Section 5.8.3)
static void write_lcr_aggregate_profile_tier_level_info(
    struct LcrAggregatePtlInfo *lcr_aggregate_ptl_info,
    struct avm_write_bit_buffer *wb) {
  avm_wb_write_literal(wb, lcr_aggregate_ptl_info->lcr_config_idc, 6);
  avm_wb_write_literal(wb, lcr_aggregate_ptl_info->lcr_aggregate_level_idx, 5);
  avm_wb_write_bit(wb, lcr_aggregate_ptl_info->lcr_max_tier_flag);
  avm_wb_write_literal(wb, lcr_aggregate_ptl_info->lcr_max_interop, 4);
}

// LCR sequence profile tier level info syntax (Section 5.8.4)
static void write_lcr_seq_profile_tier_level_info(
    struct LcrSeqPtlInfo *lcr_seq_ptl_info, struct avm_write_bit_buffer *wb) {
  avm_wb_write_literal(wb, lcr_seq_ptl_info->lcr_seq_profile_idc, 5);
  avm_wb_write_literal(wb, lcr_seq_ptl_info->lcr_max_level_idx, 5);
  avm_wb_write_bit(wb, lcr_seq_ptl_info->lcr_tier_flag);
  avm_wb_write_literal(wb, lcr_seq_ptl_info->lcr_max_mlayer_count, 3);
  avm_wb_write_literal(wb, 0, 2);  // lcr_reserved_zero_2bits
}
#endif  // CONFIG_LCR_UPDATE

static int write_lcr_xlayer_color_info(
#if CONFIG_LCR_UPDATE
    struct LcrXlayerInfo *xlayer_info,
#else
    struct AV2_COMP *cpi, int isGlobal, int xId,
#endif
    struct avm_write_bit_buffer *wb) {
#if CONFIG_LCR_UPDATE
  struct XLayerColorInfo *xlayer = &xlayer_info->xlayer_col_params;
  avm_wb_write_rice_golomb(wb, xlayer->layer_color_description_idc, 2);
  if (xlayer->layer_color_description_idc == 0) {
    avm_wb_write_literal(wb, xlayer->layer_color_primaries, 8);
    avm_wb_write_literal(wb, xlayer->layer_transfer_characteristics, 8);
    avm_wb_write_literal(wb, xlayer->layer_matrix_coefficients, 8);
  }
  avm_wb_write_bit(wb, xlayer->layer_full_range_flag);
#else
  struct XLayerColorInfo *xlayer = &cpi->common.lcr_params.xlayer_col_params;
  avm_wb_write_rice_golomb(
      wb, xlayer->layer_color_description_idc[isGlobal][xId], 2);
  if (xlayer->layer_color_description_idc[isGlobal][xId] == 0) {
    avm_wb_write_literal(wb, xlayer->layer_color_primaries[isGlobal][xId], 8);
    avm_wb_write_literal(
        wb, xlayer->layer_transfer_characteristics[isGlobal][xId], 8);
    avm_wb_write_literal(wb, xlayer->layer_matrix_coefficients[isGlobal][xId],
                         8);
  }
  avm_wb_write_bit(wb, xlayer->layer_full_range_flag[isGlobal][xId]);
#endif  // CONFIG_LCR_UPDATE
  return 0;
}

int write_lcr_embedded_layer_info(
#if CONFIG_LCR_UPDATE
    struct LcrXlayerInfo *xlayer_info, bool atlasSegmentPresent,
#else
    AV2_COMP *cpi, int isGlobal, int xId,
#endif
    struct avm_write_bit_buffer *wb) {
#if CONFIG_LCR_UPDATE
  struct EmbeddedLayerInfo *mlayer_params = &xlayer_info->mlayer_params;

  /* indicate how many spatial embedded layers are present */
  avm_wb_write_literal(wb, mlayer_params->lcr_mlayer_map, MAX_NUM_MLAYERS);

  for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
    if ((mlayer_params->lcr_mlayer_map & (1 << j))) {
      avm_wb_write_literal(wb, mlayer_params->lcr_tlayer_map[j],
                           MAX_NUM_TLAYERS);

      if (atlasSegmentPresent) {
        avm_wb_write_literal(wb, mlayer_params->lcr_layer_atlas_segment_id[j],
                             8);
        avm_wb_write_literal(wb, mlayer_params->lcr_priority_order[j], 8);
        avm_wb_write_literal(wb, mlayer_params->lcr_rendering_method[j], 8);
      }
      avm_wb_write_literal(wb, mlayer_params->lcr_layer_type[j], 8);
      if (mlayer_params->lcr_layer_type[j] == AUX_LAYER)
        avm_wb_write_literal(wb, mlayer_params->lcr_auxiliary_type[j], 8);
      avm_wb_write_literal(wb, mlayer_params->lcr_view_type[j], 8);
      if (mlayer_params->lcr_view_type[j] == VIEW_EXPLICIT)
        avm_wb_write_literal(wb, mlayer_params->lcr_view_id[j], 8);
      if (j > 0)
        avm_wb_write_literal(wb, mlayer_params->lcr_dependent_layer_map[j], j);

      // Resolution and cropping info.
      avm_wb_write_bit(wb, mlayer_params->lcr_crop_info_in_scr_flag[j]);
      if (!mlayer_params->lcr_crop_info_in_scr_flag[j]) {
        avm_wb_write_uvlc(wb, mlayer_params->lcr_crop_max_width[j]);
        avm_wb_write_uvlc(wb, mlayer_params->lcr_crop_max_height[j]);
      }
      // byte alignment
      avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % CHAR_BIT));
    }
  }
#else
  struct LayerConfigurationRecord *lcr_params = &cpi->common.lcr_params;
  struct EmbeddedLayerInfo *mlayer_params = &lcr_params->mlayer_params;

  /* indicate how many spatial embedded layers are present */
  avm_wb_write_literal(wb, mlayer_params->lcr_mlayer_map[isGlobal][xId],
                       MAX_NUM_MLAYERS);

  for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
    if ((mlayer_params->lcr_mlayer_map[isGlobal][xId] & (1 << j))) {
      avm_wb_write_literal(wb, mlayer_params->lcr_tlayer_map[isGlobal][xId][j],
                           MAX_NUM_TLAYERS);
      int atlasSegmentPresent =
          isGlobal ? lcr_params->lcr_global_atlas_id_present_flag
                   : lcr_params->lcr_local_atlas_id_present_flag[xId];

      if (atlasSegmentPresent) {
        avm_wb_write_literal(
            wb, mlayer_params->lcr_layer_atlas_segment_id[isGlobal][xId][j], 8);

        avm_wb_write_literal(
            wb, mlayer_params->lcr_priority_order[isGlobal][xId][j], 8);

        avm_wb_write_literal(
            wb, mlayer_params->lcr_rendering_method[isGlobal][xId][j], 8);
      }
      avm_wb_write_literal(wb, mlayer_params->lcr_layer_type[isGlobal][xId][j],
                           8);
      if (mlayer_params->lcr_layer_type[isGlobal][xId][j] == AUX_LAYER)
        avm_wb_write_literal(
            wb, mlayer_params->lcr_auxiliary_type[isGlobal][xId][j], 8);
      avm_wb_write_literal(wb, mlayer_params->lcr_view_type[isGlobal][xId][j],
                           8);
      if (mlayer_params->lcr_view_type[isGlobal][xId][j] == VIEW_EXPLICIT)
        avm_wb_write_literal(wb, mlayer_params->lcr_view_id[isGlobal][xId][j],
                             8);
      if (j > 0)
        avm_wb_write_literal(
            wb, mlayer_params->lcr_dependent_layer_map[isGlobal][xId][j], j);

      // Resolution and cropping info.
      // If the information is the same as what is in the SCR, then no need to
      // signal
      struct CroppingWindow *crop_params =
          &lcr_params->crop_win_list[isGlobal][xId];
      avm_wb_write_bit(wb, crop_params->crop_info_seq_flag);
      if (!crop_params->crop_info_seq_flag) {
        avm_wb_write_uvlc(wb, crop_params->crop_max_width);
        avm_wb_write_uvlc(wb, crop_params->crop_max_height);
      }
      // byte alignment
      avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % CHAR_BIT));
    }
  }
#endif  // CONFIG_LCR_UPDATE
  return 0;
}

static int write_lcr_rep_info(
#if CONFIG_LCR_UPDATE
    struct LcrXlayerInfo *xlayer_info,
#else
    struct LayerConfigurationRecord *lcr_params, int isGlobal, int xId,
#endif
    struct avm_write_bit_buffer *wb) {
#if CONFIG_LCR_UPDATE
  struct RepresentationInfo *rep_params = &xlayer_info->lcr_rep_info;
  struct CroppingWindow *crop_win = &xlayer_info->crop_win_list;
#else
  struct RepresentationInfo *rep_params = &lcr_params->rep_list[isGlobal][xId];
  struct CroppingWindow *crop_win = &lcr_params->crop_win_list[isGlobal][xId];
#endif

  avm_wb_write_uvlc(wb, rep_params->lcr_max_pic_width);
  avm_wb_write_uvlc(wb, rep_params->lcr_max_pic_height);
  avm_wb_write_bit(wb, rep_params->lcr_format_info_present_flag);
  avm_wb_write_bit(wb, crop_win->crop_window_present_flag);
  if (rep_params->lcr_format_info_present_flag) {
    avm_wb_write_uvlc(wb, rep_params->lcr_bit_depth_idc);
    avm_wb_write_uvlc(wb, rep_params->lcr_chroma_format_idc);
  }
  if (crop_win->crop_window_present_flag) {
    avm_wb_write_uvlc(wb, crop_win->crop_win_left_offset);
    avm_wb_write_uvlc(wb, crop_win->crop_win_right_offset);
    avm_wb_write_uvlc(wb, crop_win->crop_win_top_offset);
    avm_wb_write_uvlc(wb, crop_win->crop_win_bottom_offset);
  }
  return 0;
}

static int write_lcr_xlayer_info(
#if CONFIG_LCR_UPDATE
    struct LayerConfigurationRecord *lcr_params,
#else
    AV2_COMP *cpi,
#endif
    int isGlobal, int xId, struct avm_write_bit_buffer *wb) {
#if CONFIG_LCR_UPDATE
  struct LcrXlayerInfo *xlayer_info =
      isGlobal ? &lcr_params->lcr_global_payload[xId].lcr_xlayer_info
               : &lcr_params->lcr_xlayer_info;

  avm_wb_write_bit(wb, xlayer_info->lcr_rep_info_present_flag);
  avm_wb_write_bit(wb, xlayer_info->lcr_xlayer_purpose_present_flag);
  avm_wb_write_bit(wb, xlayer_info->lcr_xlayer_color_info_present_flag);
  avm_wb_write_bit(wb, xlayer_info->lcr_embedded_layer_info_present_flag);

  if (xlayer_info->lcr_rep_info_present_flag)
    write_lcr_rep_info(xlayer_info, wb);

  if (xlayer_info->lcr_xlayer_purpose_present_flag)
    avm_wb_write_literal(wb, xlayer_info->lcr_xlayer_purpose_id, 7);

  if (xlayer_info->lcr_xlayer_color_info_present_flag)
    write_lcr_xlayer_color_info(xlayer_info, wb);
#else
  struct LayerConfigurationRecord *lcr_params = &cpi->common.lcr_params;

  avm_wb_write_bit(wb, lcr_params->lcr_rep_info_present_flag[isGlobal][xId]);
  avm_wb_write_bit(wb,
                   lcr_params->lcr_xlayer_purpose_present_flag[isGlobal][xId]);
  avm_wb_write_bit(
      wb, lcr_params->lcr_xlayer_color_info_present_flag[isGlobal][xId]);
  avm_wb_write_bit(
      wb, lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId]);
  write_lcr_profile_tier_level(isGlobal, xId);

  if (lcr_params->lcr_rep_info_present_flag[isGlobal][xId])
    write_lcr_rep_info(lcr_params, isGlobal, xId, wb);

  if (lcr_params->lcr_xlayer_purpose_present_flag[isGlobal][xId])
    avm_wb_write_literal(wb, lcr_params->lcr_xlayer_purpose_id[isGlobal][xId],
                         7);

  if (lcr_params->lcr_xlayer_color_info_present_flag[isGlobal][xId])
    write_lcr_xlayer_color_info(cpi, isGlobal, xId, wb);
#endif  // CONFIG_LCR_UPDATE
  // byte alignment
  avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % CHAR_BIT));

  // Add embedded layer information if desired
#if CONFIG_LCR_UPDATE
  if (xlayer_info->lcr_embedded_layer_info_present_flag) {
    bool atlasSegmentPresent =
        isGlobal ? lcr_params->lcr_global_atlas_id_present_flag
                 : lcr_params->lcr_local_atlas_id_present_flag;
    write_lcr_embedded_layer_info(xlayer_info, atlasSegmentPresent, wb);
  }
#else
  if (lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId])
    write_lcr_embedded_layer_info(cpi, isGlobal, xId, wb);
#endif  // CONFIG_LCR_UPDATE
  else {
    // If no embedded layer info present and if extended layer 31
    // then we may wish to have atlas mapping at the xlayer level
    if (isGlobal && lcr_params->lcr_global_atlas_id_present_flag) {
#if CONFIG_LCR_UPDATE
      avm_wb_write_literal(wb, xlayer_info->lcr_xlayer_atlas_segment_id, 8);
      avm_wb_write_literal(wb, xlayer_info->lcr_xlayer_priority_order, 8);
      avm_wb_write_literal(wb, xlayer_info->lcr_xlayer_rendering_method, 8);
#else
      avm_wb_write_literal(wb, lcr_params->lcr_xlayer_atlas_segment_id[xId], 8);
      avm_wb_write_literal(wb, lcr_params->lcr_xlayer_priority_order[xId], 8);
      avm_wb_write_literal(wb, lcr_params->lcr_xlayer_rendering_method[xId], 8);
#endif  // CONFIG_LCR_UPDATE
    }
  }
  return 0;
}

void write_lcr_global_payload(
#if CONFIG_LCR_UPDATE
    struct LayerConfigurationRecord *lcr_params, int i,
#else
    AV2_COMP *cpi, int i, int sizePresent,
#endif
    struct avm_write_bit_buffer *wb) {

#if CONFIG_LCR_UPDATE
  int n = i;  // In updated structure, i is the xlayer index directly
  if (lcr_params->lcr_dependent_xlayers_flag && n > 0)
    avm_wb_write_literal(
        wb, lcr_params->lcr_global_payload[i].lcr_num_dependent_xlayer_map, n);

  write_lcr_xlayer_info(lcr_params, 1, n, wb);
#else
  struct LayerConfigurationRecord lcr_params = cpi->common.lcr_params;
  (void)sizePresent;

  avm_wb_write_literal(wb, lcr_params.lcr_xLayer_id[i], 5);
  int n = lcr_params.lcr_xLayer_id[i];
  if (lcr_params.lcr_dependent_xlayers_flag && n > 0)
    avm_wb_write_unsigned_literal(
        wb, lcr_params.lcr_num_dependent_xlayer_map[n], 32);

  write_lcr_xlayer_info(cpi, 1, n, wb);
#endif  // CONFIG_LCR_UPDATE
}

static int write_lcr_global_info(
#if CONFIG_LCR_UPDATE
    struct LayerConfigurationRecord *lcr_params,
#else
    AV2_COMP *cpi,
#endif
    struct avm_write_bit_buffer *wb) {
#if CONFIG_LCR_UPDATE
  avm_wb_write_literal(wb, lcr_params->lcr_global_config_record_id, 3);
  avm_wb_write_literal(wb, lcr_params->lcr_xlayer_map, 31);

  // Count number of extended layers in the map
  int XCount = 0;
  int LcrXLayerID[32];
  for (int i = 0; i < 31; i++) {
    if (lcr_params->lcr_xlayer_map & (1 << i)) {
      LcrXLayerID[XCount] = i;
      XCount++;
    }
  }

  avm_wb_write_bit(
      wb, lcr_params->lcr_aggregate_profile_tier_level_info_present_flag);
  avm_wb_write_bit(wb,
                   lcr_params->lcr_seq_profile_tier_level_info_present_flag);
  avm_wb_write_bit(wb, lcr_params->lcr_global_payload_present_flag);
  avm_wb_write_bit(wb, lcr_params->lcr_dependent_xlayers_flag);
  avm_wb_write_bit(wb, lcr_params->lcr_global_atlas_id_present_flag);

  avm_wb_write_literal(wb, lcr_params->lcr_global_purpose_id, 7);

  if (lcr_params->lcr_global_atlas_id_present_flag) {
    avm_wb_write_literal(wb, lcr_params->lcr_global_atlas_id, 3);
  } else {
    avm_wb_write_literal(wb, lcr_params->lcr_reserved_zero_3bits, 3);
  }

  avm_wb_write_literal(wb, lcr_params->lcr_reserved_zero_7bits, 7);

  if (lcr_params->lcr_aggregate_profile_tier_level_info_present_flag) {
    write_lcr_aggregate_profile_tier_level_info(
        &lcr_params->lcr_aggregate_ptl_info, wb);
  }

  if (lcr_params->lcr_seq_profile_tier_level_info_present_flag) {
    for (int i = 0; i < XCount; i++) {
      int n = LcrXLayerID[i];
      write_lcr_seq_profile_tier_level_info(&lcr_params->lcr_seq_ptl_info[n],
                                            wb);
    }
  }

  if (lcr_params->lcr_global_payload_present_flag) {
    for (int i = 0; i < XCount; i++) {
      avm_wb_write_uleb(wb, lcr_params->lcr_data_size[i]);
      write_lcr_global_payload(lcr_params, LcrXLayerID[i], wb);
    }
  }
#else
  struct LayerConfigurationRecord lcr_params = cpi->common.lcr_params;

  avm_wb_write_literal(wb, lcr_params.lcr_global_config_record_id, 3);

  avm_wb_write_literal(wb, lcr_params.lcr_max_num_extended_layers_minus_1, 5);

  avm_wb_write_bit(wb, lcr_params.lcr_max_profile_tier_level_info_present_flag);

  avm_wb_write_bit(wb, lcr_params.lcr_global_atlas_id_present_flag);

  avm_wb_write_bit(wb, lcr_params.lcr_dependent_xlayers_flag);

  avm_wb_write_literal(wb, lcr_params.lcr_reserved_zero_2bits, 2);

  if (lcr_params.lcr_global_atlas_id_present_flag)
    avm_wb_write_literal(wb, lcr_params.lcr_global_atlas_id, 3);
  else
    avm_wb_write_literal(wb, lcr_params.lcr_reserved_zero_3bits, 3);

  avm_wb_write_bit(wb, lcr_params.lcr_data_size_present_flag);

  avm_wb_write_literal(wb, lcr_params.lcr_global_purpose_id, 7);

  if (lcr_params.lcr_max_profile_tier_level_info_present_flag)
    write_lcr_profile_tier_level(31, 31);

  for (int i = 0; i < lcr_params.lcr_max_num_extended_layers_minus_1 + 1; i++) {
    if (lcr_params.lcr_data_size_present_flag)
      avm_wb_write_uleb(wb, lcr_params.lcr_data_size[i]);
    write_lcr_global_payload(cpi, i, lcr_params.lcr_data_size_present_flag, wb);
  }
#endif  // CONFIG_LCR_UPDATE
  return 0;
}

static int write_lcr_local_info(
#if CONFIG_LCR_UPDATE
    struct LayerConfigurationRecord *lcr_params,
#else
    AV2_COMP *cpi,
#endif
    int xlayerId, struct avm_write_bit_buffer *wb) {
#if CONFIG_LCR_UPDATE
  avm_wb_write_literal(wb, lcr_params->lcr_global_id, 3);
  avm_wb_write_literal(wb, lcr_params->lcr_local_id, 3);
  avm_wb_write_bit(wb, lcr_params->lcr_profile_tier_level_info_present_flag);
  avm_wb_write_bit(wb, lcr_params->lcr_local_atlas_id_present_flag);

  if (lcr_params->lcr_profile_tier_level_info_present_flag) {
    write_lcr_seq_profile_tier_level_info(&lcr_params->lcr_seq_ptl_info[0], wb);
  }

  if (lcr_params->lcr_local_atlas_id_present_flag) {
    avm_wb_write_literal(wb, lcr_params->lcr_local_atlas_id, 3);
  } else {
    avm_wb_write_literal(wb, lcr_params->lcr_reserved_zero_3bits, 3);
  }

  avm_wb_write_literal(wb, lcr_params->lcr_reserved_zero_5bits, 5);

  write_lcr_xlayer_info(lcr_params, 0, xlayerId, wb);
#else
  AV2_COMMON *cm = &cpi->common;
  struct LayerConfigurationRecord lcr_params = cm->lcr_params;

  avm_wb_write_literal(wb, lcr_params.lcr_global_id[xlayerId], 3);
  avm_wb_write_literal(wb, lcr_params.lcr_local_id[xlayerId], 3);
  avm_wb_write_bit(wb, lcr_params.lcr_local_atlas_id_present_flag[xlayerId]);

  if (lcr_params.lcr_local_atlas_id_present_flag[xlayerId])
    avm_wb_write_literal(wb, lcr_params.lcr_local_atlas_id[xlayerId], 3);
  else
    avm_wb_write_literal(wb, lcr_params.lcr_reserved_zero_3bits, 3);

  avm_wb_write_literal(wb, lcr_params.lcr_reserved_zero_6bits, 6);

  write_lcr_xlayer_info(cpi, 0, xlayerId, wb);
#endif
  return 0;
}

uint32_t av2_write_layer_configuration_record_obu(AV2_COMP *cpi, int xlayer_id,
#if CONFIG_LCR_UPDATE
                                                  int lcr_id,
#endif
                                                  uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;
#if CONFIG_LCR_UPDATE
  if (xlayer_id == GLOBAL_XLAYER_ID)
    write_lcr_global_info(&cpi->lcr_list[xlayer_id][lcr_id], &wb);
  else
    write_lcr_local_info(&cpi->lcr_list[xlayer_id][lcr_id], xlayer_id, &wb);
#else
  if (xlayer_id == 31)
    write_lcr_global_info(cpi, &wb);
  else
    write_lcr_local_info(cpi, xlayer_id, &wb);
#endif

  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}
#if !CONFIG_LCR_UPDATE
int av2_set_lcr_params(AV2_COMP *cpi, struct LayerConfigurationRecord *lcr,
                       int global_id, int xlayer_id) {
  AV2_COMMON *cm = &cpi->common;
  memcpy(lcr, cm->lcr, sizeof(struct LayerConfigurationRecord));
  lcr->lcr_global_id[xlayer_id] = global_id;
  if (lcr->lcr_global_config_record_id == LCR_ID_UNSPECIFIED) {
    lcr->lcr_global_config_record_id = global_id + 1;
  }
  return 0;
}
#endif  // !CONFIG_LCR_UPDATE
