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

// TODO(hegilmez) to be specified, depending on profile, tier definitions
static int write_lcr_profile_tier_level(int isGlobal, int xId) {
#if CONFIG_MULTILAYER_HLS_REMOVE_LOGS
  (void)isGlobal;
  (void)xId;
#else
  printf(
      "write_lcr_profile_tier_level(isGlobal=%d,xId=%d): profile, tier, level "
      "definitions are not defined yet\n",
      isGlobal, xId);
#endif  // CONFIG_MULTILAYER_HLS_REMOVE_LOGS
  return 0;
}

static int write_lcr_xlayer_color_info(struct AV1_COMP *cpi, int isGlobal,
                                       int xId,
                                       struct aom_write_bit_buffer *wb) {
  struct XLayerColorInfo *xlayer = &cpi->common.lcr_params.xlayer_col_params;
  aom_wb_write_uvlc(wb, xlayer->layer_color_description_idc[isGlobal][xId]);
  if (xlayer->layer_color_description_idc[isGlobal][xId] == 0) {
    aom_wb_write_literal(wb, xlayer->layer_color_primaries[isGlobal][xId], 8);
    aom_wb_write_literal(wb, xlayer->layer_matrix_coefficients[isGlobal][xId],
                         8);
    aom_wb_write_literal(
        wb, xlayer->layer_transfer_characteristics[isGlobal][xId], 8);
  }
  aom_wb_write_bit(wb, xlayer->layer_full_range_flag[isGlobal][xId]);

  return 0;
}

int write_lcr_embedded_layer_info(AV1_COMP *cpi, int isGlobal, int xId,
                                  struct aom_write_bit_buffer *wb) {
  struct LayerConfigurationRecord *lcr_params = &cpi->common.lcr_params;
  struct EmbeddedLayerInfo *mlayer_params = &lcr_params->mlayer_params;

  /* indicate how many spatial embedded layers are present */
  aom_wb_write_literal(wb, mlayer_params->lcr_mlayer_map[isGlobal][xId],
                       MAX_NUM_MLAYERS);

  for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
    if ((mlayer_params->lcr_mlayer_map[isGlobal][xId] & (1 << j))) {
      aom_wb_write_literal(wb, mlayer_params->lcr_tlayer_map[isGlobal][xId][j],
                           MAX_NUM_TLAYERS);
      int atlasSegmentPresent =
          isGlobal ? lcr_params->lcr_global_atlas_id_present_flag
                   : lcr_params->lcr_local_atlas_id_present_flag[xId];

      if (atlasSegmentPresent) {
        aom_wb_write_literal(
            wb, mlayer_params->lcr_layer_atlas_segment_id[isGlobal][xId][j], 8);

        aom_wb_write_literal(
            wb, mlayer_params->lcr_priority_order[isGlobal][xId][j], 8);

        aom_wb_write_literal(
            wb, mlayer_params->lcr_rendering_method[isGlobal][xId][j], 8);
      }
      aom_wb_write_literal(wb, mlayer_params->lcr_layer_type[isGlobal][xId][j],
                           8);
      if (mlayer_params->lcr_layer_type[isGlobal][xId][j] == AUX_LAYER)
        aom_wb_write_literal(
            wb, mlayer_params->lcr_auxiliary_type[isGlobal][xId][j], 8);
      aom_wb_write_literal(wb, mlayer_params->lcr_view_type[isGlobal][xId][j],
                           8);
      if (mlayer_params->lcr_view_type[isGlobal][xId][j] == VIEW_EXPLICIT)
        aom_wb_write_literal(wb, mlayer_params->lcr_view_id[isGlobal][xId][j],
                             8);
      if (j > 0)
        aom_wb_write_literal(
            wb, mlayer_params->lcr_dependent_layer_map[isGlobal][xId][j], j);

      // Resolution and cropping info.
      // If the information is the same as what is in the SCR, then no need to
      // signal
      struct CroppingWindow *crop_params =
          &lcr_params->crop_win_list[isGlobal][xId];
      aom_wb_write_bit(wb, crop_params->crop_info_seq_flag);
      if (!crop_params->crop_info_seq_flag) {
        aom_wb_write_uvlc(wb, crop_params->crop_max_width);
        aom_wb_write_uvlc(wb, crop_params->crop_max_height);
      }
      // byte alignment
      aom_wb_write_literal(wb, 0, (8 - wb->bit_offset % CHAR_BIT));
    }
  }
  return 0;
}

static int write_lcr_rep_info(struct LayerConfigurationRecord *lcr_params,
                              int isGlobal, int xId,
                              struct aom_write_bit_buffer *wb) {
  struct RepresentationInfo *rep_params = &lcr_params->rep_list[isGlobal][xId];
  struct CroppingWindow *crop_win = &lcr_params->crop_win_list[isGlobal][xId];

  aom_wb_write_uvlc(wb, rep_params->lcr_max_pic_width);
  aom_wb_write_uvlc(wb, rep_params->lcr_max_pic_height);
  aom_wb_write_bit(wb, rep_params->lcr_format_info_present_flag);
  aom_wb_write_bit(wb, crop_win->crop_window_present_flag);
  if (rep_params->lcr_format_info_present_flag) {
    aom_wb_write_uvlc(wb, rep_params->lcr_bit_depth_idc);
    aom_wb_write_uvlc(wb, rep_params->lcr_chroma_format_idc);
  }
  if (crop_win->crop_window_present_flag) {
    aom_wb_write_uvlc(wb, crop_win->crop_win_left_offset);
    aom_wb_write_uvlc(wb, crop_win->crop_win_right_offset);
    aom_wb_write_uvlc(wb, crop_win->crop_win_top_offset);
    aom_wb_write_uvlc(wb, crop_win->crop_win_bottom_offset);
  }
  return 0;
}

static int write_lcr_xlayer_info(AV1_COMP *cpi, int isGlobal, int xId,
                                 struct aom_write_bit_buffer *wb) {
  struct LayerConfigurationRecord *lcr_params = &cpi->common.lcr_params;

  aom_wb_write_bit(wb, lcr_params->lcr_rep_info_present_flag[isGlobal][xId]);
  aom_wb_write_bit(wb,
                   lcr_params->lcr_xlayer_purpose_present_flag[isGlobal][xId]);
  aom_wb_write_bit(
      wb, lcr_params->lcr_xlayer_color_info_present_flag[isGlobal][xId]);
  aom_wb_write_bit(
      wb, lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId]);
  write_lcr_profile_tier_level(isGlobal, xId);

  if (lcr_params->lcr_rep_info_present_flag[isGlobal][xId])
    write_lcr_rep_info(lcr_params, isGlobal, xId, wb);

  if (lcr_params->lcr_xlayer_purpose_present_flag[isGlobal][xId])
    aom_wb_write_literal(wb, lcr_params->lcr_xlayer_purpose_id[isGlobal][xId],
                         7);

  if (lcr_params->lcr_xlayer_color_info_present_flag[isGlobal][xId])
    write_lcr_xlayer_color_info(cpi, isGlobal, xId, wb);

  // byte alignment
  aom_wb_write_literal(wb, 0, (8 - wb->bit_offset % CHAR_BIT));

  // Add embedded layer information if desired
  if (lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId])
    write_lcr_embedded_layer_info(cpi, isGlobal, xId, wb);
  else {
    // If no embedded layer info present and if extended layer 31
    // then we may wish to have atlas mapping at the xlayer level
    if (isGlobal && lcr_params->lcr_global_atlas_id_present_flag) {
      aom_wb_write_literal(wb, lcr_params->lcr_xlayer_atlas_segment_id[xId], 8);
      aom_wb_write_literal(wb, lcr_params->lcr_xlayer_priority_order[xId], 8);
      aom_wb_write_literal(wb, lcr_params->lcr_xlayer_rendering_method[xId], 8);
    }
  }
  return 0;
}

void write_lcr_global_payload(AV1_COMP *cpi, int i, int sizePresent,
                              struct aom_write_bit_buffer *wb) {
  struct LayerConfigurationRecord lcr_params = cpi->common.lcr_params;
  (void)sizePresent;

  aom_wb_write_literal(wb, lcr_params.lcr_xLayer_id[i], 5);
  int n = lcr_params.lcr_xLayer_id[i];
  if (lcr_params.lcr_dependent_xlayers_flag && n > 0)
    aom_wb_write_literal(wb, lcr_params.lcr_num_dependent_xlayer_map[n], 32);

  write_lcr_xlayer_info(cpi, 1, n, wb);
}

static int write_lcr_global_info(AV1_COMP *cpi,
                                 struct aom_write_bit_buffer *wb) {
  struct LayerConfigurationRecord lcr_params = cpi->common.lcr_params;

  aom_wb_write_literal(wb, lcr_params.lcr_global_config_record_id, 3);

  aom_wb_write_literal(wb, lcr_params.lcr_max_num_extended_layers_minus_1, 5);

  aom_wb_write_bit(wb, lcr_params.lcr_max_profile_tier_level_info_present_flag);

  aom_wb_write_bit(wb, lcr_params.lcr_global_atlas_id_present_flag);

  aom_wb_write_bit(wb, lcr_params.lcr_dependent_xlayers_flag);

  aom_wb_write_literal(wb, lcr_params.lcr_reserved_zero_2bits, 2);

  if (lcr_params.lcr_global_atlas_id_present_flag)
    aom_wb_write_literal(wb, lcr_params.lcr_global_atlas_id, 3);
  else
    aom_wb_write_literal(wb, lcr_params.lcr_reserved_zero_3bits, 3);

  aom_wb_write_bit(wb, lcr_params.lcr_data_size_present_flag);

  aom_wb_write_literal(wb, lcr_params.lcr_global_purpose_id, 7);

  if (lcr_params.lcr_max_profile_tier_level_info_present_flag)
    write_lcr_profile_tier_level(31, 31);

  for (int i = 0; i < lcr_params.lcr_max_num_extended_layers_minus_1 + 1; i++) {
    if (lcr_params.lcr_data_size_present_flag)
      aom_wb_write_uleb(wb, lcr_params.lcr_data_size[i]);
    write_lcr_global_payload(cpi, i, lcr_params.lcr_data_size_present_flag, wb);
  }
  return 0;
}

static int write_lcr_local_info(AV1_COMP *cpi, int xlayerId,
                                struct aom_write_bit_buffer *wb) {
  AV1_COMMON *cm = &cpi->common;
  struct LayerConfigurationRecord lcr_params = cm->lcr_params;

  aom_wb_write_literal(wb, lcr_params.lcr_global_id[xlayerId], 3);
  aom_wb_write_literal(wb, lcr_params.lcr_local_id[xlayerId], 3);
  aom_wb_write_bit(wb, lcr_params.lcr_local_atlas_id_present_flag[xlayerId]);

  if (lcr_params.lcr_local_atlas_id_present_flag[xlayerId])
    aom_wb_write_literal(wb, lcr_params.lcr_local_atlas_id[xlayerId], 3);
  else
    aom_wb_write_literal(wb, lcr_params.lcr_reserved_zero_3bits, 3);

  aom_wb_write_literal(wb, lcr_params.lcr_reserved_zero_6bits, 6);

  write_lcr_xlayer_info(cpi, 0, xlayerId, wb);

  return 0;
}

uint32_t av1_write_layer_configuration_record_obu(AV1_COMP *cpi, int xlayer_id,
                                                  uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  if (xlayer_id == 31)
    write_lcr_global_info(cpi, &wb);
  else
    write_lcr_local_info(cpi, xlayer_id, &wb);

  av1_add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}

int av1_set_lcr_params(AV1_COMP *cpi, struct LayerConfigurationRecord *lcr,
                       int global_id, int xlayer_id) {
  AV1_COMMON *cm = &cpi->common;
  memcpy(lcr, cm->lcr, sizeof(struct LayerConfigurationRecord));
  lcr->lcr_global_id[xlayer_id] = global_id;
  return 0;
}
