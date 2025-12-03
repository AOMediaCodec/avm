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

// TODO(hegilmez) to be specified, depending on profile, tier definitions
static int read_lcr_profile_tier_level(int isGlobal, int xId) {
#if CONFIG_MULTILAYER_HLS_REMOVE_LOGS
  (void)isGlobal;
  (void)xId;
#else
  printf(
      "read_lcr_profile_tier_level(isGlobal=%d,xId=%d): profile, tier, level "
      "are not defined yet\n",
      isGlobal, xId);
#endif  // CONFIG_MULTILAYER_HLS_REMOVE_LOGS
  return 0;
}

static int read_lcr_xlayer_color_info(struct AV1Decoder *pbi, int isGlobal,
                                      int xId, struct aom_read_bit_buffer *rb) {
  struct XLayerColorInfo *xlayer = &pbi->common.lcr_params.xlayer_col_params;
  // layer_color_description_idc: indicates the combination of color primaries,
  // transfer characteristics and matrix coefficients as defined in CWG-F270.
  // The value of color_description_idc shall be in the range of 0 to 15,
  // inclusive. Values larger than 4 are reserved for future use by AOMedia and
  // should be ignored by decoders conforming to this version of this
  // specification.
  xlayer->layer_color_description_idc[isGlobal][xId] =
      aom_rb_read_rice_golomb(rb, 2);
  if (xlayer->layer_color_description_idc[isGlobal][xId] == 0) {
    xlayer->layer_color_primaries[isGlobal][xId] = aom_rb_read_literal(rb, 8);
    xlayer->layer_transfer_characteristics[isGlobal][xId] =
        aom_rb_read_literal(rb, 8);
    xlayer->layer_matrix_coefficients[isGlobal][xId] =
        aom_rb_read_literal(rb, 8);
  }
  xlayer->layer_full_range_flag[isGlobal][xId] = aom_rb_read_bit(rb);

  return 0;
}

static int read_lcr_embedded_layer_info(
    struct AV1Decoder *pbi, struct LayerConfigurationRecord *lcr_params,
    int isGlobal, int xId, struct aom_read_bit_buffer *rb) {
  struct EmbeddedLayerInfo *mlayer_params = &lcr_params->mlayer_params;
  mlayer_params->MLayerCount[isGlobal][xId] = 0;

  mlayer_params->lcr_mlayer_map[isGlobal][xId] =
      aom_rb_read_literal(rb, MAX_NUM_MLAYERS);

  for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
    if ((mlayer_params->lcr_mlayer_map[isGlobal][xId] & (1 << j))) {
      mlayer_params->LcrMlayerID[isGlobal][xId]
                                [mlayer_params->MLayerCount[isGlobal][xId]] = j;

      mlayer_params->lcr_tlayer_map[isGlobal][xId][j] =
          aom_rb_read_literal(rb, MAX_NUM_TLAYERS);

      mlayer_params->TLayerCount[isGlobal][xId][j] = 0;
      for (int k = 0; k < MAX_NUM_TLAYERS; k++) {
        if ((mlayer_params->lcr_tlayer_map[isGlobal][xId][j] & (1 << k))) {
          mlayer_params
              ->LcrTlayerID[isGlobal][xId]
                           [mlayer_params->TLayerCount[isGlobal][xId][j]] = k;
          mlayer_params->TLayerCount[isGlobal][xId][j]++;
        }
      }
      int atlasSegmentPresent =
          isGlobal ? lcr_params->lcr_global_atlas_id_present_flag
                   : lcr_params->lcr_local_atlas_id_present_flag[xId];

      if (atlasSegmentPresent) {
        mlayer_params->lcr_layer_atlas_segment_id[isGlobal][xId][j] =
            aom_rb_read_literal(rb, 8);

        mlayer_params->lcr_priority_order[isGlobal][xId][j] =
            aom_rb_read_literal(rb, 8);

        mlayer_params->lcr_rendering_method[isGlobal][xId][j] =
            aom_rb_read_literal(rb, 8);
      }
      mlayer_params->lcr_layer_type[isGlobal][xId][j] =
          aom_rb_read_literal(rb, 8);

      if (mlayer_params->lcr_layer_type[isGlobal][xId][j] == AUX_LAYER)
        mlayer_params->lcr_auxiliary_type[isGlobal][xId][j] =
            aom_rb_read_literal(rb, 8);

      mlayer_params->lcr_view_type[isGlobal][xId][j] =
          aom_rb_read_literal(rb, 8);

      if (mlayer_params->lcr_view_type[isGlobal][xId][j] == VIEW_EXPLICIT)
        mlayer_params->lcr_view_id[isGlobal][xId][j] =
            aom_rb_read_literal(rb, 8);

      if (j > 0)
        mlayer_params->lcr_dependent_layer_map[isGlobal][xId][j] =
            aom_rb_read_literal(rb, j);

      // Resolution and cropping info.
      // If the information is the same as what is in the SCR, then no need to
      // signal
      struct CroppingWindow *crop_params =
          &lcr_params->crop_win_list[isGlobal][xId];

      crop_params->crop_info_seq_flag = aom_rb_read_bit(rb);
      if (!crop_params->crop_info_seq_flag) {
        crop_params->crop_max_width = aom_rb_read_uvlc(rb);
        crop_params->crop_max_height = aom_rb_read_uvlc(rb);
      }
      // byte alignment
      if (av1_check_byte_alignment(&pbi->common, rb) != 0) {
        aom_internal_error(
            &pbi->common.error, AOM_CODEC_CORRUPT_FRAME,
            "Byte alignment error in read_lcr_embedded_layer_info()");
      }
      mlayer_params->MLayerCount[isGlobal][xId]++;
    }
  }
  return 0;
}

static int read_lcr_rep_info(struct LayerConfigurationRecord *lcr_params,
                             int isGlobal, int xId,
                             struct aom_read_bit_buffer *rb) {
  struct RepresentationInfo *rep_params = &lcr_params->rep_list[isGlobal][xId];
  struct CroppingWindow *crop_win = &lcr_params->crop_win_list[isGlobal][xId];

  rep_params->lcr_max_pic_width = aom_rb_read_uvlc(rb);
  rep_params->lcr_max_pic_height = aom_rb_read_uvlc(rb);
  rep_params->lcr_format_info_present_flag = aom_rb_read_bit(rb);

  crop_win->crop_window_present_flag = aom_rb_read_bit(rb);
  if (rep_params->lcr_format_info_present_flag) {
    rep_params->lcr_bit_depth_idc = aom_rb_read_uvlc(rb);
    rep_params->lcr_chroma_format_idc = aom_rb_read_uvlc(rb);
  }
  if (crop_win->crop_window_present_flag) {
    crop_win->crop_win_left_offset = aom_rb_read_uvlc(rb);
    crop_win->crop_win_right_offset = aom_rb_read_uvlc(rb);
    crop_win->crop_win_top_offset = aom_rb_read_uvlc(rb);
    crop_win->crop_win_bottom_offset = aom_rb_read_uvlc(rb);
  }
  return 0;
}

static int read_lcr_xlayer_info(struct AV1Decoder *pbi,
                                struct LayerConfigurationRecord *lcr_params,
                                int isGlobal, int xId,
                                struct aom_read_bit_buffer *rb) {
  lcr_params->lcr_rep_info_present_flag[isGlobal][xId] = aom_rb_read_bit(rb);
  lcr_params->lcr_xlayer_purpose_present_flag[isGlobal][xId] =
      aom_rb_read_bit(rb);
  lcr_params->lcr_xlayer_color_info_present_flag[isGlobal][xId] =
      aom_rb_read_bit(rb);
  lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId] =
      aom_rb_read_bit(rb);

  read_lcr_profile_tier_level(isGlobal, xId);

  if (lcr_params->lcr_rep_info_present_flag[isGlobal][xId])
    read_lcr_rep_info(lcr_params, isGlobal, xId, rb);

  if (lcr_params->lcr_xlayer_purpose_present_flag[isGlobal][xId])
    lcr_params->lcr_xlayer_purpose_id[isGlobal][xId] =
        aom_rb_read_literal(rb, 7);

  if (lcr_params->lcr_xlayer_color_info_present_flag[isGlobal][xId]) {
    read_lcr_xlayer_color_info(pbi, isGlobal, xId, rb);
  } else {
    lcr_params->xlayer_col_params.layer_color_description_idc[isGlobal][xId] =
        0;
    lcr_params->xlayer_col_params.layer_color_primaries[isGlobal][xId] =
        AOM_CICP_CP_UNSPECIFIED;
    lcr_params->xlayer_col_params
        .layer_transfer_characteristics[isGlobal][xId] =
        AOM_CICP_TC_UNSPECIFIED;
    lcr_params->xlayer_col_params.layer_matrix_coefficients[isGlobal][xId] =
        AOM_CICP_MC_UNSPECIFIED;
    lcr_params->xlayer_col_params.layer_full_range_flag[isGlobal][xId] = 0;
  }

  // byte alignment
  if (av1_check_byte_alignment(&pbi->common, rb) != 0) {
    aom_internal_error(&pbi->common.error, AOM_CODEC_CORRUPT_FRAME,
                       "Byte alignment error in read_lcr_xlayer_info()");
  }

  // Add embedded layer information if desired
  if (lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId])
    read_lcr_embedded_layer_info(pbi, lcr_params, isGlobal, xId, rb);
  else {
    // If no embedded layer info present and if extended layer 31
    // then we may wish to have atlas mapping at the xlayer level
    if (isGlobal && lcr_params->lcr_global_atlas_id_present_flag) {
      lcr_params->lcr_xlayer_atlas_segment_id[xId] = aom_rb_read_literal(rb, 8);
      lcr_params->lcr_xlayer_priority_order[xId] = aom_rb_read_literal(rb, 8);
      lcr_params->lcr_xlayer_rendering_method[xId] = aom_rb_read_literal(rb, 8);
    }
  }
  return 0;
}

static void read_lcr_global_payload(struct AV1Decoder *pbi,
                                    struct LayerConfigurationRecord *lcr_params,
                                    int i, struct aom_read_bit_buffer *rb) {
  lcr_params->lcr_xLayer_id[i] = aom_rb_read_literal(rb, 5);
  int n = lcr_params->lcr_xLayer_id[i];
  if (lcr_params->lcr_dependent_xlayers_flag && n > 0) {
    lcr_params->lcr_num_dependent_xlayer_map[n] =
        aom_rb_read_unsigned_literal(rb, 32);
  }
  read_lcr_xlayer_info(pbi, lcr_params, 1, n, rb);
}

static int read_lcr_global_info(struct AV1Decoder *pbi,
                                struct aom_read_bit_buffer *rb) {
  int lcr_global_config_record_id = aom_rb_read_literal(rb, 3);

  struct LayerConfigurationRecord *lcr_params;
  int lcr_pos = -1;
  for (int i = 0; i < pbi->lcr_counter; i++) {
    if (pbi->lcr_list[i].lcr_global_config_record_id ==
        lcr_global_config_record_id) {
      lcr_pos = i;
      break;
    }
  }
  if (lcr_pos != -1) {
    lcr_params = &pbi->lcr_list[lcr_pos];
  } else {
    if (pbi->lcr_counter >= MAX_NUM_LCR) {
      aom_internal_error(&pbi->common.error, AOM_CODEC_ERROR,
                         "Failed to decode in read_lcr_global_info()");
    }
    lcr_params = &pbi->lcr_list[pbi->lcr_counter];
    pbi->lcr_counter++;
  }

  lcr_params->lcr_global_config_record_id = lcr_global_config_record_id;
  lcr_params->lcr_max_num_extended_layers_minus_1 = aom_rb_read_literal(rb, 5);
  lcr_params->lcr_max_profile_tier_level_info_present_flag =
      aom_rb_read_bit(rb);
  lcr_params->lcr_global_atlas_id_present_flag = aom_rb_read_bit(rb);
  lcr_params->lcr_dependent_xlayers_flag = aom_rb_read_bit(rb);
  if (lcr_params->lcr_dependent_xlayers_flag != 0) {
    aom_internal_error(
        &pbi->common.error, AOM_CODEC_UNSUP_BITSTREAM,
        "lcr_dependent_xlayers_flag is reserved for future extensions. In the "
        "current version of the specification, lcr_dependent_xlayers_flag "
        "shall be equal to 0.");
  }
  lcr_params->lcr_reserved_zero_2bits = aom_rb_read_literal(rb, 2);

  if (lcr_params->lcr_global_atlas_id_present_flag) {
    lcr_params->lcr_global_atlas_id = aom_rb_read_literal(rb, 3);
  } else {
#if CONFIG_LCR_ID_IN_SH
    lcr_params->lcr_global_atlas_id = LCR_ID_UNSPECIFIED;
#endif  // CONFIG_LCR_ID_IN_SH
    lcr_params->lcr_reserved_zero_3bits = aom_rb_read_literal(rb, 3);
  }
  lcr_params->lcr_data_size_present_flag = aom_rb_read_bit(rb);
  lcr_params->lcr_global_purpose_id = aom_rb_read_literal(rb, 7);

  if (lcr_params->lcr_max_profile_tier_level_info_present_flag)
    read_lcr_profile_tier_level(31, 31);

  for (int i = 0; i < lcr_params->lcr_max_num_extended_layers_minus_1 + 1;
       i++) {
    lcr_params->lcr_data_size[i] = 0;
    if (lcr_params->lcr_data_size_present_flag)
      lcr_params->lcr_data_size[i] = aom_rb_read_uleb(rb);
    read_lcr_global_payload(pbi, lcr_params, i, rb);
  }
#if CONFIG_CWG_F248_RENDER_SIZE
  lcr_params->is_local_lcr = 0;
  lcr_params->xlayer_id = GLOBAL_LCR_XLAYER_ID;
  // NOTE: lcr_params->lcr_xLayer_id indicates the corresponding extended layer
  // ID for the indicated extended layer in the Global LCR and
  // lcr_params->xlayer_id is the obu_layer_id.
#endif  // CONFIG_CWG_F248_RENDER_SIZE
  return 0;
}

static int read_lcr_local_info(struct AV1Decoder *pbi, int xlayerId,
                               struct aom_read_bit_buffer *rb) {
  int lcr_global_id = aom_rb_read_literal(rb, 3);

  struct LayerConfigurationRecord *lcr_params;
  int lcr_pos = -1;
  for (int i = 0; i < pbi->lcr_counter; i++) {
#if CONFIG_LCR_ID_IN_SH
    if (pbi->lcr_list[i].lcr_global_config_record_id == lcr_global_id) {
#else
    if (pbi->lcr_list[i].lcr_global_id[xlayerId] == lcr_global_id) {
#endif  // CONFIG_LCR_ID_IN_SH
      lcr_pos = i;
      break;
    }
  }  // i
  if (lcr_pos != -1) {
    lcr_params = &pbi->lcr_list[lcr_pos];
  } else {
    if (pbi->lcr_counter >= MAX_NUM_LCR) {
      aom_internal_error(&pbi->common.error, AOM_CODEC_ERROR,
                         "Failed to decode in read_lcr_local_info()");
    }
    lcr_params = &pbi->lcr_list[pbi->lcr_counter];
    pbi->lcr_counter++;
  }

  lcr_params->lcr_global_id[xlayerId] = lcr_global_id;
#if CONFIG_LCR_ID_IN_SH
  // Keep a copy of the global LCR id to fascilitate LCR activation in SH
  lcr_params->lcr_global_config_record_id = lcr_global_id;
#endif  // CONFIG_LCR_ID_IN_SH
  lcr_params->lcr_local_id[xlayerId] = aom_rb_read_literal(rb, 3);

  lcr_params->lcr_local_atlas_id_present_flag[xlayerId] = aom_rb_read_bit(rb);

  if (lcr_params->lcr_local_atlas_id_present_flag[xlayerId]) {
    lcr_params->lcr_local_atlas_id[xlayerId] = aom_rb_read_literal(rb, 3);
  } else {
#if CONFIG_LCR_ID_IN_SH
    lcr_params->lcr_local_atlas_id[xlayerId] = LCR_ID_UNSPECIFIED;
#endif  // CONFIG_LCR_ID_IN_SH
    lcr_params->lcr_reserved_zero_3bits = aom_rb_read_literal(rb, 3);
  }

  lcr_params->lcr_reserved_zero_6bits = aom_rb_read_literal(rb, 6);

  read_lcr_xlayer_info(pbi, lcr_params, 0, xlayerId, rb);
#if CONFIG_CWG_F248_RENDER_SIZE
  lcr_params->is_local_lcr = 1;
  lcr_params->xlayer_id = xlayerId;
#endif  // CONFIG_CWG_F248_RENDER_SIZE
  return 0;
}

uint32_t av1_read_layer_configuration_record_obu(
    struct AV1Decoder *pbi, int xlayer_id, struct aom_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;
  assert(rb->error_handler);

  if (xlayer_id == GLOBAL_LCR_XLAYER_ID)
    read_lcr_global_info(pbi, rb);
  else
    read_lcr_local_info(pbi, xlayer_id, rb);

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
