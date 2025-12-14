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

// TODO(hegilmez) to be specified, depending on profile, tier definitions
static int read_lcr_profile_tier_level(int isGlobal, int xId) {
#if MULTILAYER_HLS_REMOVE_LOGS
  (void)isGlobal;
  (void)xId;
#else
  printf(
      "read_lcr_profile_tier_level(isGlobal=%d,xId=%d): profile, tier, level "
      "are not defined yet\n",
      isGlobal, xId);
#endif  // MULTILAYER_HLS_REMOVE_LOGS
  return 0;
}

static int read_lcr_xlayer_color_info(struct AV2Decoder *pbi, int isGlobal,
                                      int xId, struct avm_read_bit_buffer *rb) {
  struct XLayerColorInfo *xlayer = &pbi->common.lcr_params.xlayer_col_params;
  // layer_color_description_idc: indicates the combination of color primaries,
  // transfer characteristics and matrix coefficients as defined in CWG-F270.
  // The value of color_description_idc shall be in the range of 0 to 15,
  // inclusive. Values larger than 4 are reserved for future use by AOMedia and
  // should be ignored by decoders conforming to this version of this
  // specification.
  xlayer->layer_color_description_idc[isGlobal][xId] =
      avm_rb_read_rice_golomb(rb, 2);
  if (xlayer->layer_color_description_idc[isGlobal][xId] == 0) {
    xlayer->layer_color_primaries[isGlobal][xId] = avm_rb_read_literal(rb, 8);
    xlayer->layer_transfer_characteristics[isGlobal][xId] =
        avm_rb_read_literal(rb, 8);
    xlayer->layer_matrix_coefficients[isGlobal][xId] =
        avm_rb_read_literal(rb, 8);
  }
  xlayer->layer_full_range_flag[isGlobal][xId] = avm_rb_read_bit(rb);

  return 0;
}

static int read_lcr_embedded_layer_info(
    struct AV2Decoder *pbi, struct LayerConfigurationRecord *lcr_params,
    int isGlobal, int xId, struct avm_read_bit_buffer *rb) {
  struct EmbeddedLayerInfo *mlayer_params = &lcr_params->mlayer_params;
  mlayer_params->MLayerCount[isGlobal][xId] = 0;

  mlayer_params->lcr_mlayer_map[isGlobal][xId] =
      avm_rb_read_literal(rb, MAX_NUM_MLAYERS);

  for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
    if ((mlayer_params->lcr_mlayer_map[isGlobal][xId] & (1 << j))) {
      mlayer_params->LcrMlayerID[isGlobal][xId]
                                [mlayer_params->MLayerCount[isGlobal][xId]] = j;

      mlayer_params->lcr_tlayer_map[isGlobal][xId][j] =
          avm_rb_read_literal(rb, MAX_NUM_TLAYERS);

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
            avm_rb_read_literal(rb, 8);

        mlayer_params->lcr_priority_order[isGlobal][xId][j] =
            avm_rb_read_literal(rb, 8);

        mlayer_params->lcr_rendering_method[isGlobal][xId][j] =
            avm_rb_read_literal(rb, 8);
      }
      mlayer_params->lcr_layer_type[isGlobal][xId][j] =
          avm_rb_read_literal(rb, 8);

      if (mlayer_params->lcr_layer_type[isGlobal][xId][j] == AUX_LAYER)
        mlayer_params->lcr_auxiliary_type[isGlobal][xId][j] =
            avm_rb_read_literal(rb, 8);

      mlayer_params->lcr_view_type[isGlobal][xId][j] =
          avm_rb_read_literal(rb, 8);

      if (mlayer_params->lcr_view_type[isGlobal][xId][j] == VIEW_EXPLICIT)
        mlayer_params->lcr_view_id[isGlobal][xId][j] =
            avm_rb_read_literal(rb, 8);

      if (j > 0)
        mlayer_params->lcr_dependent_layer_map[isGlobal][xId][j] =
            avm_rb_read_literal(rb, j);

      // Resolution and cropping info.
      // If the information is the same as what is in the SCR, then no need to
      // signal
      struct CroppingWindow *crop_params =
          &lcr_params->crop_win_list[isGlobal][xId];

      crop_params->crop_info_seq_flag = avm_rb_read_bit(rb);
      if (!crop_params->crop_info_seq_flag) {
        crop_params->crop_max_width = avm_rb_read_uvlc(rb);
        crop_params->crop_max_height = avm_rb_read_uvlc(rb);
      }
      // byte alignment
      if (av2_check_byte_alignment(&pbi->common, rb) != 0) {
        avm_internal_error(
            &pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
            "Byte alignment error in read_lcr_embedded_layer_info()");
      }
      mlayer_params->MLayerCount[isGlobal][xId]++;
    }
  }
  return 0;
}

static int read_lcr_rep_info(struct LayerConfigurationRecord *lcr_params,
                             int isGlobal, int xId,
                             struct avm_read_bit_buffer *rb) {
  struct RepresentationInfo *rep_params = &lcr_params->rep_list[isGlobal][xId];
  struct CroppingWindow *crop_win = &lcr_params->crop_win_list[isGlobal][xId];

  rep_params->lcr_max_pic_width = avm_rb_read_uvlc(rb);
  rep_params->lcr_max_pic_height = avm_rb_read_uvlc(rb);
  rep_params->lcr_format_info_present_flag = avm_rb_read_bit(rb);

  crop_win->crop_window_present_flag = avm_rb_read_bit(rb);
  if (rep_params->lcr_format_info_present_flag) {
    rep_params->lcr_bit_depth_idc = avm_rb_read_uvlc(rb);
    rep_params->lcr_chroma_format_idc = avm_rb_read_uvlc(rb);
  }
  if (crop_win->crop_window_present_flag) {
    crop_win->crop_win_left_offset = avm_rb_read_uvlc(rb);
    crop_win->crop_win_right_offset = avm_rb_read_uvlc(rb);
    crop_win->crop_win_top_offset = avm_rb_read_uvlc(rb);
    crop_win->crop_win_bottom_offset = avm_rb_read_uvlc(rb);
  }
  return 0;
}

static int read_lcr_xlayer_info(struct AV2Decoder *pbi,
                                struct LayerConfigurationRecord *lcr_params,
                                int isGlobal, int xId,
                                struct avm_read_bit_buffer *rb) {
  lcr_params->lcr_rep_info_present_flag[isGlobal][xId] = avm_rb_read_bit(rb);
  lcr_params->lcr_xlayer_purpose_present_flag[isGlobal][xId] =
      avm_rb_read_bit(rb);
  lcr_params->lcr_xlayer_color_info_present_flag[isGlobal][xId] =
      avm_rb_read_bit(rb);
  lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId] =
      avm_rb_read_bit(rb);

  read_lcr_profile_tier_level(isGlobal, xId);

  if (lcr_params->lcr_rep_info_present_flag[isGlobal][xId])
    read_lcr_rep_info(lcr_params, isGlobal, xId, rb);

  if (lcr_params->lcr_xlayer_purpose_present_flag[isGlobal][xId])
    lcr_params->lcr_xlayer_purpose_id[isGlobal][xId] =
        avm_rb_read_literal(rb, 7);

  if (lcr_params->lcr_xlayer_color_info_present_flag[isGlobal][xId]) {
    read_lcr_xlayer_color_info(pbi, isGlobal, xId, rb);
  } else {
    lcr_params->xlayer_col_params.layer_color_description_idc[isGlobal][xId] =
        0;
    lcr_params->xlayer_col_params.layer_color_primaries[isGlobal][xId] =
        AVM_CICP_CP_UNSPECIFIED;
    lcr_params->xlayer_col_params
        .layer_transfer_characteristics[isGlobal][xId] =
        AVM_CICP_TC_UNSPECIFIED;
    lcr_params->xlayer_col_params.layer_matrix_coefficients[isGlobal][xId] =
        AVM_CICP_MC_UNSPECIFIED;
    lcr_params->xlayer_col_params.layer_full_range_flag[isGlobal][xId] = 0;
  }

  // byte alignment
  if (av2_check_byte_alignment(&pbi->common, rb) != 0) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
                       "Byte alignment error in read_lcr_xlayer_info()");
  }

  // Add embedded layer information if desired
  if (lcr_params->lcr_embedded_layer_info_present_flag[isGlobal][xId])
    read_lcr_embedded_layer_info(pbi, lcr_params, isGlobal, xId, rb);
  else {
    // If no embedded layer info present and if extended layer 31
    // then we may wish to have atlas mapping at the xlayer level
    if (isGlobal && lcr_params->lcr_global_atlas_id_present_flag) {
      lcr_params->lcr_xlayer_atlas_segment_id[xId] = avm_rb_read_literal(rb, 8);
      lcr_params->lcr_xlayer_priority_order[xId] = avm_rb_read_literal(rb, 8);
      lcr_params->lcr_xlayer_rendering_method[xId] = avm_rb_read_literal(rb, 8);
    }
  }
  return 0;
}

static void read_lcr_global_payload(struct AV2Decoder *pbi,
                                    struct LayerConfigurationRecord *lcr_params,
                                    int i, struct avm_read_bit_buffer *rb) {
  lcr_params->lcr_xLayer_id[i] = avm_rb_read_literal(rb, XLAYER_BITS);
  int n = lcr_params->lcr_xLayer_id[i];
  if (lcr_params->lcr_dependent_xlayers_flag && n > 0) {
    lcr_params->lcr_num_dependent_xlayer_map[n] =
        avm_rb_read_unsigned_literal(rb, 32);
  }
  read_lcr_xlayer_info(pbi, lcr_params, 1, n, rb);
}

static int read_lcr_global_info(struct AV2Decoder *pbi,
                                struct avm_read_bit_buffer *rb) {
  int lcr_global_config_record_id = avm_rb_read_literal(rb, 3);

  if (lcr_global_config_record_id == LCR_ID_UNSPECIFIED) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Invalid lcr_global_config_record_id: "
                       "LCR_ID_UNSPECIFIED (0) is not a valid LCR ID.");
  }

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
    const int idx = AVMMIN(pbi->lcr_counter, MAX_NUM_LCR - 1);
    lcr_params = &pbi->lcr_list[idx];
    pbi->lcr_counter = AVMMIN(pbi->lcr_counter + 1, MAX_NUM_LCR);
  }

  lcr_params->lcr_global_config_record_id = lcr_global_config_record_id;
  lcr_params->lcr_max_num_extended_layers_minus_1 =
      avm_rb_read_literal(rb, XLAYER_BITS);
  lcr_params->lcr_max_profile_tier_level_info_present_flag =
      avm_rb_read_bit(rb);
  lcr_params->lcr_global_atlas_id_present_flag = avm_rb_read_bit(rb);
  lcr_params->lcr_dependent_xlayers_flag = avm_rb_read_bit(rb);
  if (lcr_params->lcr_dependent_xlayers_flag != 0) {
    avm_internal_error(
        &pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
        "lcr_dependent_xlayers_flag is reserved for future extensions. In the "
        "current version of the specification, lcr_dependent_xlayers_flag "
        "shall be equal to 0.");
  }
  lcr_params->lcr_reserved_zero_2bits = avm_rb_read_literal(rb, 2);

  if (lcr_params->lcr_global_atlas_id_present_flag) {
    lcr_params->lcr_global_atlas_id = avm_rb_read_literal(rb, 3);
  } else {
    lcr_params->lcr_global_atlas_id = LCR_ID_UNSPECIFIED;
    lcr_params->lcr_reserved_zero_3bits = avm_rb_read_literal(rb, 3);
  }
  lcr_params->lcr_data_size_present_flag = avm_rb_read_bit(rb);
  lcr_params->lcr_global_purpose_id = avm_rb_read_literal(rb, 7);

  // TODO: align with signaling of profile, tier level
  if (lcr_params->lcr_max_profile_tier_level_info_present_flag)
    read_lcr_profile_tier_level(31, 31);

  for (int i = 0; i < lcr_params->lcr_max_num_extended_layers_minus_1 + 1;
       i++) {
    lcr_params->lcr_data_size[i] = 0;
    if (lcr_params->lcr_data_size_present_flag)
      lcr_params->lcr_data_size[i] = avm_rb_read_uleb(rb);
    read_lcr_global_payload(pbi, lcr_params, i, rb);
  }
  lcr_params->is_local_lcr = 0;
  lcr_params->xlayer_id = GLOBAL_XLAYER_ID;
  // NOTE: lcr_params->lcr_xLayer_id indicates the corresponding extended layer
  // ID for the indicated extended layer in the Global LCR and
  // lcr_params->xlayer_id is the obu_layer_id.
  return 0;
}

static int read_lcr_local_info(struct AV2Decoder *pbi, int xlayerId,
                               struct avm_read_bit_buffer *rb) {
  int lcr_global_id = avm_rb_read_literal(rb, 3);

  if (lcr_global_id == LCR_ID_UNSPECIFIED) {
    avm_internal_error(
        &pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
        "Invalid lcr_global_id: LCR_ID_UNSPECIFIED (0) is not a valid LCR ID.");
  }

  struct LayerConfigurationRecord *lcr_params;
  int lcr_pos = -1;
  for (int i = 0; i < pbi->lcr_counter; i++) {
    if (pbi->lcr_list[i].lcr_global_config_record_id == lcr_global_id) {
      lcr_pos = i;
      break;
    }
  }
  if (lcr_pos != -1) {
    lcr_params = &pbi->lcr_list[lcr_pos];
  } else {
    const int idx = AVMMIN(pbi->lcr_counter, MAX_NUM_LCR - 1);
    lcr_params = &pbi->lcr_list[idx];
    pbi->lcr_counter = AVMMIN(pbi->lcr_counter + 1, MAX_NUM_LCR);
  }

  lcr_params->lcr_global_id[xlayerId] = lcr_global_id;
  // Keep a copy of the global LCR id to fascilitate LCR activation in SH
  lcr_params->lcr_global_config_record_id = lcr_global_id;
  lcr_params->lcr_local_id[xlayerId] = avm_rb_read_literal(rb, 3);

  lcr_params->lcr_local_atlas_id_present_flag[xlayerId] = avm_rb_read_bit(rb);

  if (lcr_params->lcr_local_atlas_id_present_flag[xlayerId]) {
    lcr_params->lcr_local_atlas_id[xlayerId] = avm_rb_read_literal(rb, 3);
  } else {
    lcr_params->lcr_local_atlas_id[xlayerId] = LCR_ID_UNSPECIFIED;
    lcr_params->lcr_reserved_zero_3bits = avm_rb_read_literal(rb, 3);
  }

  lcr_params->lcr_reserved_zero_6bits = avm_rb_read_literal(rb, 6);

  read_lcr_xlayer_info(pbi, lcr_params, 0, xlayerId, rb);
  lcr_params->is_local_lcr = 1;
  lcr_params->xlayer_id = xlayerId;
  return 0;
}

uint32_t av2_read_layer_configuration_record_obu(
    struct AV2Decoder *pbi, int xlayer_id, struct avm_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;
  assert(rb->error_handler);

  if (xlayer_id == GLOBAL_XLAYER_ID)
    read_lcr_global_info(pbi, rb);
  else
    read_lcr_local_info(pbi, xlayer_id, rb);

  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
