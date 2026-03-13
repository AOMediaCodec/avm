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

#if CONFIG_AV2_LCR_PROFILES
static void write_lcr_aggregate_profile_tier_level_info(
    struct LcrAggregateProfileTierLevelInfo *ptl,
    struct avm_write_bit_buffer *wb) {
  avm_wb_write_literal(wb, ptl->lcr_config_idc, 6);
  avm_wb_write_literal(wb, ptl->lcr_aggregate_level_idx, 5);
  avm_wb_write_bit(wb, ptl->lcr_max_tier_flag);
  avm_wb_write_literal(wb, ptl->lcr_max_interop, 4);
}

static void write_lcr_seq_profile_tier_level_info(
    struct LcrSeqProfileTierLevelInfo *ptl, struct avm_write_bit_buffer *wb) {
  avm_wb_write_literal(wb, ptl->lcr_seq_profile_idc, 5);
  avm_wb_write_literal(wb, ptl->lcr_max_level_idx, 5);
  avm_wb_write_bit(wb, ptl->lcr_tier_flag);
  avm_wb_write_literal(wb, ptl->lcr_max_mlayer_count, 3);
  avm_wb_write_literal(wb, ptl->lcr_reserved_2bits, 2);
}

static int write_lcr_xlayer_color_info(struct LCRXLayerInfo *xlayer_info,
                                       struct avm_write_bit_buffer *wb) {
  struct XLayerColorInfo *xlayer = &xlayer_info->xlayer_col_params;
  avm_wb_write_rice_golomb(wb, xlayer->layer_color_description_idc, 2);
  if (xlayer->layer_color_description_idc == 0) {
    avm_wb_write_literal(wb, xlayer->layer_color_primaries, 8);
    avm_wb_write_literal(wb, xlayer->layer_transfer_characteristics, 8);
    avm_wb_write_literal(wb, xlayer->layer_matrix_coefficients, 8);
  }
  avm_wb_write_bit(wb, xlayer->layer_full_range_flag);
  return 0;
}

static int write_lcr_embedded_layer_info(struct LCRXLayerInfo *xlayer_info,
                                         int atlas_id_present,
                                         struct avm_write_bit_buffer *wb) {
  struct EmbeddedLayerInfo *mlayer_params = &xlayer_info->mlayer_params;
  avm_wb_write_literal(wb, mlayer_params->lcr_mlayer_map, MAX_NUM_MLAYERS);
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    if ((mlayer_params->lcr_mlayer_map & (1 << i))) {
      avm_wb_write_literal(wb, mlayer_params->lcr_tlayer_map[i],
                           MAX_NUM_TLAYERS);
      if (atlas_id_present) {
        avm_wb_write_literal(wb, mlayer_params->lcr_layer_atlas_segment_id[i],
                             8);
        avm_wb_write_literal(wb, mlayer_params->lcr_priority_order[i], 8);
        avm_wb_write_literal(wb, mlayer_params->lcr_rendering_method[i], 8);
      }
      avm_wb_write_literal(wb, mlayer_params->lcr_layer_type[i], 8);
      if (mlayer_params->lcr_layer_type[i] == AUX_LAYER) {
        assert(mlayer_params->lcr_auxiliary_type[i] <= LCR_GAIN_MAP_AUX ||
               (mlayer_params->lcr_auxiliary_type[i] >=
                    LCR_AUX_TYPE_UNSPECIFIED_START &&
                mlayer_params->lcr_auxiliary_type[i] <=
                    LCR_AUX_TYPE_UNSPECIFIED_END));
        avm_wb_write_literal(wb, mlayer_params->lcr_auxiliary_type[i], 8);
      }

      avm_wb_write_literal(wb, mlayer_params->lcr_view_type[i], 8);

      if (mlayer_params->lcr_view_type[i] == VIEW_EXPLICIT) {
        avm_wb_write_literal(wb, mlayer_params->lcr_view_id[i], 8);
      }

      if (i > 0) {
        avm_wb_write_literal(wb, mlayer_params->lcr_dependent_layer_map[i], i);
      }
      avm_wb_write_bit(wb, mlayer_params->lcr_same_sh_max_resolution_flag[i]);
      if (!mlayer_params->lcr_same_sh_max_resolution_flag[i]) {
        avm_wb_write_uvlc(wb, mlayer_params->lcr_max_expected_width[i]);
        avm_wb_write_uvlc(wb, mlayer_params->lcr_max_expected_height[i]);
      }
      // Byte alignment
      int remaining_bits = wb->bit_offset % 8;
      if (remaining_bits != 0) {
        avm_wb_write_literal(wb, 0, 8 - remaining_bits);
      }
    }
  }
  return 0;
}

static int write_lcr_rep_info(struct LCRXLayerInfo *xlayer_info,
                              struct avm_write_bit_buffer *wb) {
  struct RepresentationInfo *rep_params = &xlayer_info->rep_params;
  struct CroppingWindow *crop_win = &xlayer_info->crop_win;

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

static int write_lcr_xlayer_info(struct LCRXLayerInfo *xlayer_info,
                                 bool is_global, int atlas_id_present,
                                 struct avm_write_bit_buffer *wb) {
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

  // Byte alignment
  avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % CHAR_BIT) % CHAR_BIT);

  // Add embedded layer information
  if (xlayer_info->lcr_embedded_layer_info_present_flag) {
    write_lcr_embedded_layer_info(xlayer_info, atlas_id_present, wb);
  } else {
    // If no embedded layer info present and if global LCR
    if (is_global && atlas_id_present) {
      avm_wb_write_literal(wb, xlayer_info->lcr_xlayer_atlas_segment_id, 8);
      avm_wb_write_literal(wb, xlayer_info->lcr_xlayer_priority_order, 8);
      avm_wb_write_literal(wb, xlayer_info->lcr_xlayer_rendering_method, 8);
    }
  }
  return 0;
}

void write_lcr_global_payload(struct GlobalLayerConfigurationRecord *global_lcr,
                              int i, struct avm_write_bit_buffer *wb) {
  // xlayer ID is derived from lcr_xlayer_map by the decoder (LcrXLayerID[i]),
  // so it is NOT written to the bitstream here.
  int n = global_lcr->LcrXLayerID[i];

  if (global_lcr->lcr_dependent_xlayers_flag && n > 0)
    avm_wb_write_unsigned_literal(
        wb, global_lcr->lcr_num_dependent_xlayer_map[i], n);

  write_lcr_xlayer_info(&global_lcr->xlayer_info[i], true,
                        global_lcr->lcr_global_atlas_id_present_flag, wb);
}

static int write_lcr_global_info(struct LayerConfigurationRecord *lcr_params,
                                 struct avm_write_bit_buffer *wb) {
  struct GlobalLayerConfigurationRecord *glcr = &lcr_params->global_lcr;

  avm_wb_write_literal(wb, glcr->lcr_global_config_record_id, 3);
  avm_wb_write_literal(wb, glcr->lcr_xlayer_map, 31);
  avm_wb_write_bit(wb,
                   glcr->lcr_aggregate_profile_tier_level_info_present_flag);
  avm_wb_write_bit(wb, glcr->lcr_seq_profile_tier_level_info_present_flag);
  avm_wb_write_bit(wb, glcr->lcr_global_payload_present_flag);
  avm_wb_write_bit(wb, glcr->lcr_dependent_xlayers_flag);
  avm_wb_write_bit(wb, glcr->lcr_global_atlas_id_present_flag);
  avm_wb_write_literal(wb, glcr->lcr_global_purpose_id, 7);
  avm_wb_write_bit(wb, glcr->lcr_enforce_tu_alignment_flag);
  avm_wb_write_bit(wb, glcr->lcr_enforce_tile_alignment_flag);
  if (glcr->lcr_global_atlas_id_present_flag)
    avm_wb_write_literal(wb, glcr->lcr_global_atlas_id, 3);
  else
    avm_wb_write_literal(wb, glcr->lcr_reserved_zero_3bits, 3);
  avm_wb_write_literal(wb, glcr->lcr_reserved_zero_5bits, 5);

  if (glcr->lcr_aggregate_profile_tier_level_info_present_flag)
    write_lcr_aggregate_profile_tier_level_info(&glcr->aggregate_ptl, wb);

  if (glcr->lcr_seq_profile_tier_level_info_present_flag) {
    for (int i = 0; i < glcr->LcrMaxNumXLayerCount; i++) {
      write_lcr_seq_profile_tier_level_info(&glcr->seq_ptl[i], wb);
    }
  }

  if (glcr->lcr_global_payload_present_flag) {
    int xlayer_count = glcr->LcrMaxNumXLayerCount;
    for (int i = 0; i < xlayer_count; i++) {
      avm_wb_write_uleb(wb, glcr->lcr_data_size[i]);
      write_lcr_global_payload(glcr, i, wb);
    }
  }
  return 0;
}

static int write_lcr_local_info(struct LayerConfigurationRecord *lcr_params,
                                struct avm_write_bit_buffer *wb) {
  struct LocalLayerConfigurationRecord *llcr = &lcr_params->local_lcr;

  avm_wb_write_literal(wb, llcr->lcr_global_id, 3);
  avm_wb_write_literal(wb, llcr->lcr_local_id, 3);
  avm_wb_write_bit(wb, llcr->lcr_profile_tier_level_info_present_flag);
  avm_wb_write_bit(wb, llcr->lcr_local_atlas_id_present_flag);

  if (llcr->lcr_profile_tier_level_info_present_flag)
    write_lcr_seq_profile_tier_level_info(&llcr->seq_ptl, wb);

  if (llcr->lcr_local_atlas_id_present_flag)
    avm_wb_write_literal(wb, llcr->lcr_local_atlas_id, 3);
  else
    avm_wb_write_literal(wb, llcr->lcr_reserved_zero_3bits, 3);
  avm_wb_write_literal(wb, llcr->lcr_reserved_zero_5bits, 5);

  write_lcr_xlayer_info(&llcr->xlayer_info, false,
                        llcr->lcr_local_atlas_id_present_flag, wb);
  return 0;
}

uint32_t av2_write_layer_configuration_record_obu(AV2_COMP *cpi, int xlayer_id,
                                                  uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;
  struct LayerConfigurationRecord *lcr_params = &cpi->common.lcr_params;
  if (xlayer_id == GLOBAL_XLAYER_ID)
    write_lcr_global_info(lcr_params, &wb);
  else
    write_lcr_local_info(lcr_params, &wb);

  avm_wb_write_bit(&wb, lcr_params->lcr_extension_present_flag);
  assert(!cpi->common.lcr_params.lcr_extension_present_flag);
  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}
#else
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

static int write_lcr_xlayer_color_info(struct AV2_COMP *cpi, int isGlobal,
                                       int xId,
                                       struct avm_write_bit_buffer *wb) {
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

  return 0;
}

int write_lcr_embedded_layer_info(AV2_COMP *cpi, int isGlobal, int xId,
                                  struct avm_write_bit_buffer *wb) {
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
      if (mlayer_params->lcr_layer_type[isGlobal][xId][j] == AUX_LAYER) {
        assert(mlayer_params->lcr_auxiliary_type[isGlobal][xId][j] <=
                   LCR_GAIN_MAP_AUX ||
               (mlayer_params->lcr_auxiliary_type[isGlobal][xId][j] >=
                    LCR_AUX_TYPE_UNSPECIFIED_START &&
                mlayer_params->lcr_auxiliary_type[isGlobal][xId][j] <=
                    LCR_AUX_TYPE_UNSPECIFIED_END));
        avm_wb_write_literal(
            wb, mlayer_params->lcr_auxiliary_type[isGlobal][xId][j], 8);
      }
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
#if CONFIG_AV2_PROFILES
      avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % 8) % 8);
#else
      avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % CHAR_BIT));
#endif  // CONFIG_AV2_PROFILES
    }
  }
  return 0;
}

static int write_lcr_rep_info(struct LayerConfigurationRecord *lcr_params,
                              int isGlobal, int xId,
                              struct avm_write_bit_buffer *wb) {
  struct RepresentationInfo *rep_params = &lcr_params->rep_list[isGlobal][xId];
  struct CroppingWindow *crop_win = &lcr_params->crop_win_list[isGlobal][xId];

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

static int write_lcr_xlayer_info(AV2_COMP *cpi, int isGlobal, int xId,
                                 struct avm_write_bit_buffer *wb) {
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

    // byte alignment
#if CONFIG_AV2_PROFILES
  avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % 8) % 8);
#else
  avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % CHAR_BIT));
#endif  //  CONFIG_AV2_PROFILES

  // Add embedded layer information if desired
  if (lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId])
    write_lcr_embedded_layer_info(cpi, isGlobal, xId, wb);
  else {
    // If no embedded layer info present and if extended layer 31
    // then we may wish to have atlas mapping at the xlayer level
    if (isGlobal && lcr_params->lcr_global_atlas_id_present_flag) {
      avm_wb_write_literal(wb, lcr_params->lcr_xlayer_atlas_segment_id[xId], 8);
      avm_wb_write_literal(wb, lcr_params->lcr_xlayer_priority_order[xId], 8);
      avm_wb_write_literal(wb, lcr_params->lcr_xlayer_rendering_method[xId], 8);
    }
  }
  return 0;
}

void write_lcr_global_payload(AV2_COMP *cpi, int i, int sizePresent,
                              struct avm_write_bit_buffer *wb) {
  struct LayerConfigurationRecord lcr_params = cpi->common.lcr_params;
  (void)sizePresent;

  // xlayer ID is derived from lcr_xlayer_map by the decoder (LcrXLayerID[i]),
  // so it is NOT written to the bitstream here.
  int n = lcr_params.lcr_xLayer_id[i];
  if (lcr_params.lcr_dependent_xlayers_flag && n > 0)
    avm_wb_write_unsigned_literal(
        wb, lcr_params.lcr_num_dependent_xlayer_map[n], n);

  write_lcr_xlayer_info(cpi, 1, n, wb);
}

static int write_lcr_global_info(AV2_COMP *cpi,
                                 struct avm_write_bit_buffer *wb) {
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
  avm_wb_write_bit(wb, lcr_params.lcr_enforce_tu_alignment_flag);
  avm_wb_write_bit(wb, lcr_params.lcr_enforce_tile_alignment_flag);
  avm_wb_write_literal(wb, lcr_params.lcr_reserved_zero_6bits, 6);
  if (lcr_params.lcr_max_profile_tier_level_info_present_flag)
    write_lcr_profile_tier_level(31, 31);

  for (int i = 0; i < lcr_params.lcr_max_num_extended_layers_minus_1 + 1; i++) {
    if (lcr_params.lcr_data_size_present_flag)
      avm_wb_write_uleb(wb, lcr_params.lcr_data_size[i]);
    write_lcr_global_payload(cpi, i, lcr_params.lcr_data_size_present_flag, wb);
  }
  return 0;
}

static int write_lcr_local_info(AV2_COMP *cpi, int xlayerId,
                                struct avm_write_bit_buffer *wb) {
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

  return 0;
}

uint32_t av2_write_layer_configuration_record_obu(AV2_COMP *cpi, int xlayer_id,
                                                  uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  if (xlayer_id == 31)
    write_lcr_global_info(cpi, &wb);
  else
    write_lcr_local_info(cpi, xlayer_id, &wb);
  avm_wb_write_bit(&wb, cpi->common.lcr_params.lcr_extension_present_flag);
  assert(!cpi->common.lcr_params.lcr_extension_present_flag);
  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}

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
#endif  // CONFIG_AV2_LCR_PROFILES
