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

static void validate_lcr_auxiliary_type(int lcr_aux_type, int layer_id,
                                        struct avm_internal_error_info *error) {
  if ((lcr_aux_type >= LCR_AUX_TYPE_RESERVED_START &&
       lcr_aux_type <= LCR_AUX_TYPE_RESERVED_END) ||
      (lcr_aux_type >= LCR_AUX_TYPE_RESERVED2_START &&
       lcr_aux_type <= LCR_AUX_TYPE_RESERVED2_END)) {
    avm_internal_error(
        error, AVM_CODEC_UNSUP_BITSTREAM,
        "Reserved lcr_auxiliary_type value %d for embedded layer %d. "
        "Valid values are in range: %d-%d or unspecified range %d-%d",
        lcr_aux_type, layer_id, LCR_ALPHA_AUX, LCR_GAIN_MAP_AUX,
        LCR_AUX_TYPE_UNSPECIFIED_START, LCR_AUX_TYPE_UNSPECIFIED_END);
  }
}

#if CONFIG_AV2_LCR_PROFILES
static void read_lcr_aggregate_profile_tier_level_info(
    struct LcrAggregateProfileTierLevelInfo *ptl,
    struct avm_read_bit_buffer *rb) {
  ptl->lcr_config_idc = avm_rb_read_literal(rb, CONFIG_BITS);
  ptl->lcr_aggregate_level_idx = avm_rb_read_literal(rb, LEVEL_BITS);
  ptl->lcr_max_tier_flag = avm_rb_read_bit(rb);
  ptl->lcr_max_interop = avm_rb_read_literal(rb, INTEROP_BITS);
}

static void read_lcr_seq_profile_tier_level_info(
    struct LcrSeqProfileTierLevelInfo *ptl, struct avm_read_bit_buffer *rb) {
  ptl->lcr_seq_profile_idc = avm_rb_read_literal(rb, PROFILE_BITS);
  ptl->lcr_max_level_idx = avm_rb_read_literal(rb, LEVEL_BITS);
  ptl->lcr_tier_flag = avm_rb_read_bit(rb);
  ptl->lcr_max_mlayer_count = avm_rb_read_literal(rb, 3);
  ptl->lcr_reserved_2bits = avm_rb_read_literal(rb, 2);
}

static int read_lcr_xlayer_color_info(struct LCRXLayerInfo *xlayer_info,
                                      struct avm_read_bit_buffer *rb) {
  struct XLayerColorInfo *xlayer = &xlayer_info->xlayer_col_params;
  xlayer->layer_color_description_idc = avm_rb_read_rice_golomb(rb, 2);
  if (xlayer->layer_color_description_idc == AVM_COLOR_DESC_IDC_EXPLICIT) {
    xlayer->layer_color_primaries = avm_rb_read_literal(rb, 8);
    xlayer->layer_transfer_characteristics = avm_rb_read_literal(rb, 8);
    xlayer->layer_matrix_coefficients = avm_rb_read_literal(rb, 8);
  }
  xlayer->layer_full_range_flag = avm_rb_read_bit(rb);
  return 0;
}

static int read_lcr_embedded_layer_info(struct LCRXLayerInfo *xlayer_info,
                                        int atlas_id_present,
                                        struct avm_read_bit_buffer *rb,
                                        struct avm_internal_error_info *error) {
  EmbeddedLayerInfo *mlayer_params = &xlayer_info->mlayer_params;
  mlayer_params->MLayerCount = 0;
  mlayer_params->lcr_mlayer_map = avm_rb_read_literal(rb, MAX_NUM_MLAYERS);
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    if ((mlayer_params->lcr_mlayer_map & (1 << i))) {
      mlayer_params->LcrMlayerID[mlayer_params->MLayerCount] = i;
      mlayer_params->lcr_tlayer_map[i] =
          avm_rb_read_literal(rb, MAX_NUM_TLAYERS);

      mlayer_params->TLayerCount[i] = 0;
      for (int k = 0; k < MAX_NUM_TLAYERS; k++) {
        if ((mlayer_params->lcr_tlayer_map[i] & (1 << k))) {
          mlayer_params->LcrTlayerID[mlayer_params->TLayerCount[i]] = k;
          mlayer_params->TLayerCount[i]++;
        }
      }
      if (atlas_id_present) {
        mlayer_params->lcr_layer_atlas_segment_id[i] =
            avm_rb_read_literal(rb, 8);
        mlayer_params->lcr_priority_order[i] = avm_rb_read_literal(rb, 8);
        mlayer_params->lcr_rendering_method[i] = avm_rb_read_literal(rb, 8);
      }
      mlayer_params->lcr_layer_type[i] = avm_rb_read_literal(rb, 8);

      if (mlayer_params->lcr_layer_type[i] == AUX_LAYER) {
        mlayer_params->lcr_auxiliary_type[i] = avm_rb_read_literal(rb, 8);
        validate_lcr_auxiliary_type(mlayer_params->lcr_auxiliary_type[i], i,
                                    error);
      }

      mlayer_params->lcr_view_type[i] = avm_rb_read_literal(rb, 8);

      if (mlayer_params->lcr_view_type[i] == VIEW_EXPLICIT) {
        mlayer_params->lcr_view_id[i] = avm_rb_read_literal(rb, 8);
      }
      if (i > 0) {
        mlayer_params->lcr_dependent_layer_map[i] = avm_rb_read_literal(rb, i);
      }

      mlayer_params->lcr_same_sh_max_resolution_flag[i] = avm_rb_read_bit(rb);
      if (!mlayer_params->lcr_same_sh_max_resolution_flag[i]) {
        mlayer_params->lcr_max_expected_width[i] = avm_rb_read_uvlc(rb);
        mlayer_params->lcr_max_expected_height[i] = avm_rb_read_uvlc(rb);
      }
      // Byte alignment
      int remaining_bits = rb->bit_offset % 8;
      if (remaining_bits != 0) {
        avm_rb_read_literal(rb, (8 - remaining_bits));
      }
      mlayer_params->MLayerCount++;
    }
  }
  return 0;
}

static int read_lcr_rep_info(struct LCRXLayerInfo *xlayer_info,
                             struct avm_read_bit_buffer *rb) {
  RepresentationInfo *rep_params = &xlayer_info->rep_params;
  CroppingWindow *crop_win = &xlayer_info->crop_win;

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

static int read_lcr_xlayer_info(struct LCRXLayerInfo *xlayer_info,
                                bool is_global, int atlas_id_present,
                                struct avm_read_bit_buffer *rb,
                                struct avm_internal_error_info *error) {
  xlayer_info->lcr_rep_info_present_flag = avm_rb_read_bit(rb);
  xlayer_info->lcr_xlayer_purpose_present_flag = avm_rb_read_bit(rb);
  xlayer_info->lcr_xlayer_color_info_present_flag = avm_rb_read_bit(rb);
  xlayer_info->lcr_embedded_layer_info_present_flag = avm_rb_read_bit(rb);

  if (xlayer_info->lcr_rep_info_present_flag)
    read_lcr_rep_info(xlayer_info, rb);

  if (xlayer_info->lcr_xlayer_purpose_present_flag)
    xlayer_info->lcr_xlayer_purpose_id = avm_rb_read_literal(rb, 7);

  if (xlayer_info->lcr_xlayer_color_info_present_flag)
    read_lcr_xlayer_color_info(xlayer_info, rb);

  // Byte alignment
  avm_rb_read_literal(rb, (8 - rb->bit_offset % CHAR_BIT) % CHAR_BIT);

  if (xlayer_info->lcr_embedded_layer_info_present_flag) {
    read_lcr_embedded_layer_info(xlayer_info, atlas_id_present, rb, error);
  } else {
    if (is_global && atlas_id_present) {
      xlayer_info->lcr_xlayer_atlas_segment_id = avm_rb_read_literal(rb, 8);
      xlayer_info->lcr_xlayer_priority_order = avm_rb_read_literal(rb, 8);
      xlayer_info->lcr_xlayer_rendering_method = avm_rb_read_literal(rb, 8);
    }
  }
  return 0;
}

static void read_lcr_global_payload(struct GlobalLayerConfigurationRecord *glcr,
                                    int i, struct avm_read_bit_buffer *rb,
                                    struct avm_internal_error_info *error) {
  int n = glcr->LcrXLayerID[i];  // actual ID
  if (glcr->lcr_dependent_xlayers_flag && n > 0) {
    glcr->lcr_num_dependent_xlayer_map[i] = avm_rb_read_literal(rb, n);
  }
  // xlayer info[i] corresponds to LcrXLayerID
  read_lcr_xlayer_info(&glcr->xlayer_info[i], true,
                       glcr->lcr_global_atlas_id_present_flag, rb, error);
}

static void read_lcr_global_info(struct AV2Decoder *pbi,
                                 struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  int lcr_global_config_record_id = avm_rb_read_literal(rb, 3);
  if (lcr_global_config_record_id == LCR_ID_UNSPECIFIED) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Invalid lcr_global_config_record_id: "
                       "LCR_ID_UNSPECIFIED (0) is not a valid LCR ID.");
  }
  LayerConfigurationRecord *lcr =
      &pbi->lcr_list[GLOBAL_XLAYER_ID][lcr_global_config_record_id];

  // Snapshot the active LCR's global record if it has the same ID, so we can
  // verify the newly parsed copy is identical after parsing completes.
  const bool check_identity =
      pbi->active_lcr != NULL && pbi->active_lcr->is_global &&
      pbi->active_lcr->global_lcr.lcr_global_config_record_id ==
          lcr_global_config_record_id;
  GlobalLayerConfigurationRecord prev_glcr;
  if (check_identity) prev_glcr = pbi->active_lcr->global_lcr;

  GlobalLayerConfigurationRecord *glcr = &lcr->global_lcr;

  lcr->valid = 1;
  lcr->is_global = 1;
  lcr->xlayer_id = GLOBAL_XLAYER_ID;
  lcr->lcr_id = lcr_global_config_record_id;

  glcr->lcr_global_config_record_id = lcr_global_config_record_id;
  glcr->lcr_xlayer_map = avm_rb_read_literal(rb, 31);
  glcr->LcrMaxNumXLayerCount = 0;
  for (int i = 0; i < 31; i++) {
    if (glcr->lcr_xlayer_map & (1 << i)) {
      glcr->LcrXLayerID[glcr->LcrMaxNumXLayerCount] = i;
      glcr->LcrMaxNumXLayerCount++;
    }
  }
  glcr->lcr_aggregate_profile_tier_level_info_present_flag =
      avm_rb_read_bit(rb);
  glcr->lcr_seq_profile_tier_level_info_present_flag = avm_rb_read_bit(rb);
  glcr->lcr_global_payload_present_flag = avm_rb_read_bit(rb);
  glcr->lcr_dependent_xlayers_flag = avm_rb_read_bit(rb);
  glcr->lcr_global_atlas_id_present_flag = avm_rb_read_bit(rb);
  glcr->lcr_global_purpose_id = avm_rb_read_literal(rb, 7);
  glcr->lcr_enforce_tu_alignment_flag = avm_rb_read_bit(rb);
  glcr->lcr_enforce_tile_alignment_flag = avm_rb_read_bit(rb);
  if (glcr->lcr_global_atlas_id_present_flag)
    glcr->lcr_global_atlas_id = avm_rb_read_literal(rb, 3);
  else
    glcr->lcr_reserved_zero_3bits = avm_rb_read_literal(rb, 3);
  glcr->lcr_reserved_zero_5bits = avm_rb_read_literal(rb, 5);

  if (glcr->lcr_aggregate_profile_tier_level_info_present_flag)
    read_lcr_aggregate_profile_tier_level_info(&glcr->aggregate_ptl, rb);

  if (glcr->lcr_seq_profile_tier_level_info_present_flag) {
    for (int i = 0; i < glcr->LcrMaxNumXLayerCount; i++) {
      read_lcr_seq_profile_tier_level_info(&glcr->seq_ptl[i], rb);
    }
  }
  if (glcr->lcr_global_payload_present_flag) {
    int xlayer_count = glcr->LcrMaxNumXLayerCount;
    for (int i = 0; i < xlayer_count; i++) {
      glcr->lcr_data_size[i] = avm_rb_read_uleb(rb);
      read_lcr_global_payload(glcr, i, rb, &cm->error);
    }
  }

  // Conformance: if the active LCR has the same global config record ID, the
  // newly parsed copy must be bit-identical to it.
  if (check_identity && memcmp(&prev_glcr, glcr, sizeof(prev_glcr)) != 0) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Global LCR with lcr_global_config_record_id %d "
                       "differs from the active LCR with the same ID.",
                       lcr_global_config_record_id);
  }

  // Set the GLCR flag if this Global LCR describes multiple extended layers.
  // This triggers multi_stream_mode even without an MSDO OBU present.
  if (glcr->LcrMaxNumXLayerCount > 1) {
    pbi->glcr_is_present_in_tu = 1;
    pbi->glcr_num_xlayers = glcr->LcrMaxNumXLayerCount;
    // Copy xlayer IDs for stream_info allocation
    cm->num_streams = glcr->LcrMaxNumXLayerCount;
    for (int i = 0; i < glcr->LcrMaxNumXLayerCount; i++) {
      cm->stream_ids[i] = glcr->LcrXLayerID[i];
    }
  }
}

static void read_lcr_local_info(struct AV2Decoder *pbi, int xlayer_id,
                                struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  int lcr_global_id = avm_rb_read_literal(rb, 3);
  if (lcr_global_id == LCR_ID_UNSPECIFIED) {
    avm_internal_error(
        &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
        "Invalid lcr_global_id: LCR_ID_UNSPECIFIED (0) is not a valid LCR ID.");
  }
  int lcr_local_id = avm_rb_read_literal(rb, 3);
  LayerConfigurationRecord *lcr = &pbi->lcr_list[xlayer_id][lcr_local_id];

  // Snapshot the active LCR's local record if it matches this
  // (xlayer_id, lcr_local_id, lcr_global_id), so we can verify the newly
  // parsed copy is identical after parsing completes.
  const bool check_identity =
      pbi->active_lcr != NULL && !pbi->active_lcr->is_global &&
      pbi->active_lcr->xlayer_id == xlayer_id &&
      pbi->active_lcr->local_lcr.lcr_local_id == lcr_local_id &&
      pbi->active_lcr->local_lcr.lcr_global_id == lcr_global_id;
  LocalLayerConfigurationRecord prev_llcr;
  if (check_identity) prev_llcr = pbi->active_lcr->local_lcr;

  LocalLayerConfigurationRecord *llcr = &lcr->local_lcr;

  lcr->valid = 1;
  lcr->is_global = false;
  lcr->xlayer_id = xlayer_id;
  llcr->lcr_global_id = lcr_global_id;
  llcr->lcr_local_id = lcr_local_id;

  llcr->lcr_profile_tier_level_info_present_flag = avm_rb_read_bit(rb);
  llcr->lcr_local_atlas_id_present_flag = avm_rb_read_bit(rb);

  if (llcr->lcr_profile_tier_level_info_present_flag)
    read_lcr_seq_profile_tier_level_info(&llcr->seq_ptl, rb);

  if (llcr->lcr_local_atlas_id_present_flag)
    llcr->lcr_local_atlas_id = avm_rb_read_literal(rb, 3);
  else
    llcr->lcr_reserved_zero_3bits = avm_rb_read_literal(rb, 3);
  llcr->lcr_reserved_zero_5bits = avm_rb_read_literal(rb, 5);

  read_lcr_xlayer_info(&llcr->xlayer_info, false,
                       llcr->lcr_local_atlas_id_present_flag, rb, &cm->error);

  // Conformance: if the active LCR matches this local LCR's identity, the
  // newly parsed copy must be bit-identical to it.
  if (check_identity && memcmp(&prev_llcr, llcr, sizeof(prev_llcr)) != 0) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Local LCR for xlayer_id %d with lcr_local_id %d and "
                       "lcr_global_id %d differs from the active LCR with "
                       "the same ID.",
                       xlayer_id, lcr_local_id, lcr_global_id);
  }
}

uint32_t av2_read_layer_configuration_record_obu(
    struct AV2Decoder *pbi, int xlayer_id, struct avm_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;
  assert(rb->error_handler);
  if (xlayer_id == GLOBAL_XLAYER_ID)
    read_lcr_global_info(pbi, rb);
  else
    read_lcr_local_info(pbi, xlayer_id, rb);

  size_t bits_before_ext = rb->bit_offset - saved_bit_offset;
  int lcr_extension_present_flag = avm_rb_read_bit(rb);
  if (lcr_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag) -
    // trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &pbi->common.error);
    if (extension_bits > 0) {
      rb->bit_offset += extension_bits;  // skip over the extension bits
    } else {
      // No extension data present
    }
  }
  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
#else
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

static void read_lcr_xlayer_color_info(struct AV2Decoder *pbi, int isGlobal,
                                       int xId,
                                       struct avm_read_bit_buffer *rb) {
  struct XLayerColorInfo *xlayer = &pbi->common.lcr_params.xlayer_col_params;
  av2_read_color_info(&xlayer->layer_color_description_idc[isGlobal][xId],
                      &xlayer->layer_color_primaries[isGlobal][xId],
                      &xlayer->layer_transfer_characteristics[isGlobal][xId],
                      &xlayer->layer_matrix_coefficients[isGlobal][xId],
                      &xlayer->layer_full_range_flag[isGlobal][xId], rb);
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

      if (mlayer_params->lcr_layer_type[isGlobal][xId][j] == AUX_LAYER) {
        mlayer_params->lcr_auxiliary_type[isGlobal][xId][j] =
            avm_rb_read_literal(rb, 8);
        validate_lcr_auxiliary_type(
            mlayer_params->lcr_auxiliary_type[isGlobal][xId][j], j,
            &pbi->common.error);
      }

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
        AVM_COLOR_DESC_IDC_EXPLICIT;
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

static LayerConfigurationRecord *read_lcr_global_info(
    struct AV2Decoder *pbi, struct avm_read_bit_buffer *rb) {
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
  lcr_params->lcr_enforce_tu_alignment_flag = avm_rb_read_bit(rb);
  lcr_params->lcr_enforce_tile_alignment_flag = avm_rb_read_bit(rb);
  lcr_params->lcr_reserved_zero_6bits = avm_rb_read_literal(rb, 6);

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

  // Set the GLCR flag if this Global LCR describes multiple extended layers.
  // This triggers multi_stream_mode even without an MSDO OBU present.
  const int num_xlayers = lcr_params->lcr_max_num_extended_layers_minus_1 + 1;
  if (num_xlayers > 1) {
    pbi->glcr_is_present_in_tu = 1;
    pbi->glcr_num_xlayers = num_xlayers;
    // Copy xlayer IDs for stream_info allocation
    pbi->common.num_streams = num_xlayers;
    for (int i = 0; i < num_xlayers; i++) {
      pbi->common.stream_ids[i] = lcr_params->lcr_xLayer_id[i];
    }
  }
  return lcr_params;
}

static LayerConfigurationRecord *read_lcr_local_info(
    struct AV2Decoder *pbi, int xlayerId, struct avm_read_bit_buffer *rb) {
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
  return lcr_params;
}

uint32_t av2_read_layer_configuration_record_obu(
    struct AV2Decoder *pbi, int xlayer_id, struct avm_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;
  assert(rb->error_handler);

  struct LayerConfigurationRecord *lcr_params;

  if (xlayer_id == GLOBAL_XLAYER_ID)
    lcr_params = read_lcr_global_info(pbi, rb);
  else
    lcr_params = read_lcr_local_info(pbi, xlayer_id, rb);

  size_t bits_before_ext = rb->bit_offset - saved_bit_offset;
  lcr_params->lcr_extension_present_flag = avm_rb_read_bit(rb);
  if (lcr_params->lcr_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag) -
    // trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &pbi->common.error);
    if (extension_bits > 0) {
      rb->bit_offset += extension_bits;  // skip over the extension bits
    } else {
      // No extension data present
    }
  }
  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
#endif  // CONFIG_AV2_LCR_PROFILES
