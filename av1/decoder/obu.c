/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
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
#if CONFIG_BAND_METADATA
#include "av1/common/banding_metadata.h"
#endif  // CONFIG_BAND_METADATA
#include "config/aom_scale_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_dsp/bitreader_buffer.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem_ops.h"

#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/common/timing.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/obu.h"

aom_codec_err_t aom_get_num_layers_from_operating_point_idc(
    int operating_point_idc, unsigned int *number_mlayers,
    unsigned int *number_tlayers) {
  // derive number of embedded/temporal layers from operating_point_idc
  if (!number_mlayers || !number_tlayers) return AOM_CODEC_INVALID_PARAM;

  if (operating_point_idc == 0) {
    *number_tlayers = 1;
    *number_mlayers = 1;
  } else {
    *number_mlayers = 0;
    *number_tlayers = 0;
    for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
      *number_mlayers += (operating_point_idc >> (j + MAX_NUM_TLAYERS)) & 0x1;
    }
    for (int j = 0; j < MAX_NUM_TLAYERS; j++) {
      *number_tlayers += (operating_point_idc >> j) & 0x1;
    }
  }
  return AOM_CODEC_OK;
}

static int is_obu_in_current_operating_point(AV1Decoder *pbi,
                                             ObuHeader obu_header) {
  if (!pbi->current_operating_point) {
    return 1;
  }

  if ((pbi->current_operating_point >> obu_header.obu_tlayer_id) & 0x1 &&
      (pbi->current_operating_point >>
       (obu_header.obu_mlayer_id + MAX_NUM_TLAYERS)) &
          0x1) {
    return 1;
  }
  return 0;
}

static uint32_t read_temporal_delimiter_obu() { return 0; }

// Returns a boolean that indicates success.
static int read_bitstream_level(AV1_LEVEL *seq_level_idx,
                                struct aom_read_bit_buffer *rb) {
  *seq_level_idx = aom_rb_read_literal(rb, LEVEL_BITS);
  if (!is_valid_seq_level_idx(*seq_level_idx)) return 0;
  return 1;
}

static void av1_read_tlayer_dependency_info(SequenceHeader *const seq,
                                            struct aom_read_bit_buffer *rb) {
  const int max_layer_id = seq->max_tlayer_id;
  for (int curr_layer_id = 1; curr_layer_id <= max_layer_id; curr_layer_id++) {
    for (int ref_layer_id = curr_layer_id; ref_layer_id >= 0; ref_layer_id--) {
      seq->tlayer_dependency_map[curr_layer_id][ref_layer_id] =
          aom_rb_read_bit(rb);
    }
  }
}

static void av1_read_mlayer_dependency_info(SequenceHeader *const seq,
                                            struct aom_read_bit_buffer *rb) {
  const int max_layer_id = seq->max_mlayer_id;
  for (int curr_layer_id = 1; curr_layer_id <= max_layer_id; curr_layer_id++) {
    for (int ref_layer_id = curr_layer_id; ref_layer_id >= 0; ref_layer_id--) {
      seq->mlayer_dependency_map[curr_layer_id][ref_layer_id] =
          aom_rb_read_bit(rb);
    }
  }
}

#if CONFIG_CROP_WIN_CWG_F220
// This function validates the conformance window params
static void av1_validate_seq_conformance_window(
    const struct SequenceHeader *seq_params,
    struct aom_internal_error_info *error_info) {
  const struct CropWindow *conf = &seq_params->conf;
  if (!conf->conf_win_enabled_flag) return;

  if (conf->conf_win_left_offset >= seq_params->max_frame_width) {
    aom_internal_error(
        error_info, AOM_CODEC_UNSUP_BITSTREAM,
        "Conformance window left offset %d exceeds max width %d\n",
        conf->conf_win_left_offset, seq_params->max_frame_width);
  }

  if (conf->conf_win_right_offset >= seq_params->max_frame_width) {
    aom_internal_error(
        error_info, AOM_CODEC_UNSUP_BITSTREAM,
        "Conformance window right offset %d exceeds max width %d\n",
        conf->conf_win_right_offset, seq_params->max_frame_width);
  }

  if (conf->conf_win_top_offset >= seq_params->max_frame_height) {
    aom_internal_error(
        error_info, AOM_CODEC_UNSUP_BITSTREAM,
        "Conformance window top offset %d exceeds max height %d\n",
        conf->conf_win_top_offset, seq_params->max_frame_height);
  }

  if (conf->conf_win_bottom_offset >= seq_params->max_frame_height) {
    aom_internal_error(
        error_info, AOM_CODEC_UNSUP_BITSTREAM,
        "Conformance window bottom offset %d exceeds max height %d\n",
        conf->conf_win_bottom_offset, seq_params->max_frame_height);
  }
}
#endif  // CONFIG_CROP_WIN_CWG_F220

// Returns whether two sequence headers are consistent with each other.
// Note that the 'op_params' field is not compared per Section 7.5 in the spec:
//   Within a particular coded video sequence, the contents of
//   sequence_header_obu must be bit-identical each time the sequence header
//   appears except for the contents of operating_parameters_info.
#if !CONFIG_F024_KEYOBU
static
#endif  // !CONFIG_F024_KEYOBU
    int
    are_seq_headers_consistent(const SequenceHeader *seq_params_old,
                               const SequenceHeader *seq_params_new) {
  return !memcmp(seq_params_old, seq_params_new,
                 offsetof(SequenceHeader, op_params));
}
static uint32_t read_multi_stream_decoder_operation_obu(
    AV1Decoder *pbi, struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  // Verify rb has been configured to report errors.
  assert(rb->error_handler);
  const int num_streams =
      aom_rb_read_literal(rb, 3) + 2;  // read number of streams
  if (num_streams > AOM_MAX_NUM_STREAMS) {
    aom_internal_error(
        &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
        "The number of streams cannot exceed the max value (4).");
  }
  cm->num_streams = num_streams;

  const int multistream_profile_idx =
      aom_rb_read_literal(rb, PROFILE_BITS);  // read profile of multistream
  (void)multistream_profile_idx;

  const int multistream_level_idx =
      aom_rb_read_literal(rb, LEVEL_BITS);  // read level of multistream
  (void)multistream_level_idx;

  const int multistream_tier_idx =
      aom_rb_read_bit(rb);  // read tier of multistream
  (void)multistream_tier_idx;

  const int multistream_even_allocation_flag =
      aom_rb_read_bit(rb);  // read multistream_even_allocation_flag

  if (!multistream_even_allocation_flag) {
    const int multistream_large_picture_idc =
        aom_rb_read_literal(rb, 3);  // read multistream_large_picture_idc
    (void)multistream_large_picture_idc;
  }

  for (int i = 0; i < num_streams; i++) {
    cm->stream_ids[i] = aom_rb_read_literal(rb, XLAYER_BITS);  // read stream ID
    const int substream_profile_idx =
        aom_rb_read_literal(rb, PROFILE_BITS);  // read profile of multistream
    (void)substream_profile_idx;

    const int substream_level_idx =
        aom_rb_read_literal(rb, LEVEL_BITS);  // read level of multistream
    (void)substream_level_idx;

    const int substream_tier_idx =
        aom_rb_read_bit(rb);  // read tier of multistream
    (void)substream_tier_idx;
  }

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

#if CONFIG_MULTI_FRAME_HEADER
static INLINE void reset_mfh_valid(AV1_COMMON *cm) {
  for (int i = 0; i < MAX_MFH_NUM; i++) {
    cm->mfh_valid[i] = false;
#if CONFIG_MFH_SIGNAL_TILE_INFO
    cm->mfh_params[i].mfh_tile_info_present_flag = 0;
#endif  // CONFIG_MFH_SIGNAL_TILE_INFO
  }
}
#endif  // CONFIG_MULTI_FRAME_HEADER

// On success, sets pbi->sequence_header_ready to 1 and returns the number of
// bytes read from 'rb'.
// On failure, sets pbi->common.error.error_code and returns 0.
static uint32_t read_sequence_header_obu(AV1Decoder *pbi,
                                         struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  // Verify rb has been configured to report errors.
  assert(rb->error_handler);

#if CONFIG_CWG_E242_SEQ_HDR_ID
  // Use an element in the pbi->seq_list array to store the information as we
  // decode. At the end, if no errors have occurred, cm->seq_params is updated.
  uint32_t seq_header_id = aom_rb_read_uvlc(rb);
  if (seq_header_id >= MAX_SEQ_NUM) {
    cm->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }
  struct SequenceHeader *seq_params;
  int seq_header_pos = -1;
  for (int i = 0; i < pbi->seq_header_count; i++) {
    if (pbi->seq_list[i].seq_header_id == (int)seq_header_id) {
      seq_header_pos = i;
      break;
    }
  }
  if (seq_header_pos != -1) {
    seq_params = &pbi->seq_list[seq_header_pos];
  } else {
    assert(pbi->seq_header_count < MAX_SEQ_NUM);
    seq_params = &pbi->seq_list[pbi->seq_header_count];
    pbi->seq_header_count++;
    seq_params->seq_header_id = seq_header_id;
  }
#else
  // Use a local variable to store the information as we decode. At the end,
  // if no errors have occurred, cm->seq_params is updated.
  SequenceHeader sh = cm->seq_params;
  SequenceHeader *const seq_params = &sh;
#endif  // CONFIG_CWG_E242_SEQ_HDR_ID

#if !CONFIG_LCR_ID_IN_SH
  int seq_lcr_id = aom_rb_read_literal(rb, 3);
  if (seq_lcr_id > MAX_NUM_SEQ_LCR_ID) {
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Unsupported LCR id in the Sequence Header.\n");
  }
  seq_params->seq_lcr_id = seq_lcr_id;
#endif  // !CONFIG_LCR_ID_IN_SH

  seq_params->profile = av1_read_profile(rb);
  if (seq_params->profile > CONFIG_MAX_DECODE_PROFILE) {
    cm->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }
#if CONFIG_MODIFY_SH
  seq_params->single_picture_header_flag = aom_rb_read_bit(rb);
  if (seq_params->single_picture_header_flag) {
#if CONFIG_LCR_ID_IN_SH
    seq_params->seq_lcr_id = LCR_ID_UNSPECIFIED;
#endif  // CONFIG_LCR_ID_IN_SH
    seq_params->still_picture = 1;
  } else {
#if CONFIG_LCR_ID_IN_SH
    int seq_lcr_id = aom_rb_read_literal(rb, 3);
    if (seq_lcr_id > MAX_NUM_SEQ_LCR_ID) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Unsupported LCR id in the Sequence Header.\n");
    }
    seq_params->seq_lcr_id = seq_lcr_id;
#endif  // CONFIG_LCR_ID_IN_SH
    seq_params->still_picture = aom_rb_read_bit(rb);
  }
  if (!read_bitstream_level(&seq_params->seq_level_idx[0], rb)) {
    cm->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }
  if (seq_params->seq_level_idx[0] >= SEQ_LEVEL_4_0 &&
      !seq_params->single_picture_header_flag)
    seq_params->tier[0] = aom_rb_read_bit(rb);
  else
    seq_params->tier[0] = 0;
#endif  // CONFIG_MODIFY_SH

  const int num_bits_width = aom_rb_read_literal(rb, 4) + 1;
  const int num_bits_height = aom_rb_read_literal(rb, 4) + 1;
  const int max_frame_width = aom_rb_read_literal(rb, num_bits_width) + 1;
  const int max_frame_height = aom_rb_read_literal(rb, num_bits_height) + 1;

  seq_params->num_bits_width = num_bits_width;
  seq_params->num_bits_height = num_bits_height;
  seq_params->max_frame_width = max_frame_width;
  seq_params->max_frame_height = max_frame_height;

#if CONFIG_CROP_WIN_CWG_F220
  av1_read_conformance_window(rb, seq_params);
  av1_validate_seq_conformance_window(seq_params, &cm->error);
#endif  // CONFIG_CROP_WIN_CWG_F220

#if CONFIG_CWG_F270_CI_OBU
  av1_read_chroma_format_bitdepth(rb, seq_params, &cm->error);
#else
  av1_read_color_config(rb, seq_params, &cm->error);
#endif  // CONFIG_CWG_F270_CI_OBU

#if !CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  if (!(seq_params->subsampling_x == 0 && seq_params->subsampling_y == 0) &&
      !(seq_params->subsampling_x == 1 && seq_params->subsampling_y == 1) &&
      !(seq_params->subsampling_x == 1 && seq_params->subsampling_y == 0)) {
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Only 4:4:4, 4:2:2 and 4:2:0 are currently supported, "
                       "%d %d subsampling is not supported.\n",
                       seq_params->subsampling_x, seq_params->subsampling_y);
  }
#endif  // !CONFIG_CWG_E242_CHROMA_FORMAT_IDC

#if !CONFIG_MODIFY_SH
  // Still picture or not
  seq_params->still_picture = aom_rb_read_bit(rb);
  seq_params->single_picture_header_flag = aom_rb_read_bit(rb);
  // Video must have single_picture_header_flag = 0
  if (!seq_params->still_picture && seq_params->single_picture_header_flag) {
    cm->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }
#endif  // !CONFIG_MODIFY_SH

  if (seq_params->single_picture_header_flag) {
#if !CONFIG_CWG_F270_CI_OBU
    seq_params->timing_info_present = 0;
#endif  // !CONFIG_CWG_F270_CI_OBU
    seq_params->decoder_model_info_present_flag = 0;
    seq_params->display_model_info_present_flag = 0;
    seq_params->operating_points_cnt_minus_1 = 0;
    seq_params->operating_point_idc[0] = 0;
#if !CONFIG_MODIFY_SH
    if (!read_bitstream_level(&seq_params->seq_level_idx[0], rb)) {
      cm->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
      return 0;
    }
    seq_params->tier[0] = 0;
#endif  // !CONFIG_MODIFY_SH
    seq_params->op_params[0].decoder_model_param_present_flag = 0;
    seq_params->op_params[0].display_model_param_present_flag = 0;
  } else {
#if !CONFIG_CWG_F270_CI_OBU
    seq_params->timing_info_present = aom_rb_read_bit(rb);
    if (seq_params->timing_info_present) {
      av1_read_timing_info_header(&seq_params->timing_info, &cm->error, rb);
#endif  // !CONFIG_CWG_F270_CI_OBU
      seq_params->decoder_model_info_present_flag = aom_rb_read_bit(rb);
      if (seq_params->decoder_model_info_present_flag)
        av1_read_decoder_model_info(&seq_params->decoder_model_info, rb);
#if !CONFIG_CWG_F270_CI_OBU
    } else {
      seq_params->decoder_model_info_present_flag = 0;
    }
#endif  // !CONFIG_CWG_F270_CI_OBU
    seq_params->display_model_info_present_flag = aom_rb_read_bit(rb);
    seq_params->operating_points_cnt_minus_1 =
        aom_rb_read_literal(rb, OP_POINTS_CNT_MINUS_1_BITS);
    for (int i = 0; i < seq_params->operating_points_cnt_minus_1 + 1; i++) {
      seq_params->operating_point_idc[i] =
          aom_rb_read_literal(rb, OP_POINTS_IDC_BITS);
#if !CONFIG_MODIFY_SH
      if (!read_bitstream_level(&seq_params->seq_level_idx[i], rb)) {
        cm->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
        return 0;
      }
      // This is the seq_level_idx[i] > 7 check in the spec. seq_level_idx 7
      // is equivalent to level 3.3.
      if (seq_params->seq_level_idx[i] >= SEQ_LEVEL_4_0)
        seq_params->tier[i] = aom_rb_read_bit(rb);
      else
        seq_params->tier[i] = 0;
#endif  // !CONFIG_MODIFY_SH
      if (seq_params->decoder_model_info_present_flag) {
        seq_params->op_params[i].decoder_model_param_present_flag =
            aom_rb_read_bit(rb);
        if (seq_params->op_params[i].decoder_model_param_present_flag)
          av1_read_op_parameters_info(&seq_params->op_params[i],
                                      seq_params->decoder_model_info
                                          .encoder_decoder_buffer_delay_length,
                                      rb);
      } else {
        seq_params->op_params[i].decoder_model_param_present_flag = 0;
      }
#if CONFIG_CWG_F270_CI_OBU
      if (seq_params->op_params[i].decoder_model_param_present_flag) {
#else
      if (seq_params->timing_info_present &&
          (seq_params->timing_info.equal_picture_interval ||
           seq_params->op_params[i].decoder_model_param_present_flag)) {
#endif  // CONFIG_CWG_F270_CI_OBU
        seq_params->op_params[i].bitrate = av1_max_level_bitrate(
            seq_params->profile, seq_params->seq_level_idx[i],
            seq_params->tier[i]);
        // Level with seq_level_idx = 31 returns a high "dummy" bitrate to pass
        // the check
        if (seq_params->op_params[i].bitrate == 0)
          aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                             "AV1 does not support this combination of "
                             "profile, level, and tier.");
        // Buffer size in bits/s is bitrate in bits/s * 1 s
        seq_params->op_params[i].buffer_size = seq_params->op_params[i].bitrate;
      }
      if (
#if !CONFIG_CWG_F270_CI_OBU
          seq_params->timing_info_present &&
          seq_params->timing_info.equal_picture_interval &&
#endif  // !CONFIG_CWG_F270_CI_OBU
          !seq_params->op_params[i].decoder_model_param_present_flag) {
        // When the decoder_model_parameters are not sent for this op, set
        // the default ones that can be used with the resource availability mode
        seq_params->op_params[i].decoder_buffer_delay = 70000;
        seq_params->op_params[i].encoder_buffer_delay = 20000;
        seq_params->op_params[i].low_delay_mode_flag = 0;
      }

      if (seq_params->display_model_info_present_flag) {
        seq_params->op_params[i].display_model_param_present_flag =
            aom_rb_read_bit(rb);
        if (seq_params->op_params[i].display_model_param_present_flag) {
          seq_params->op_params[i].initial_display_delay =
              aom_rb_read_literal(rb, 4) + 1;
          if (seq_params->op_params[i].initial_display_delay > 10)
            aom_internal_error(
                &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                "AV1 does not support more than 10 decoded frames delay");
        } else {
          seq_params->op_params[i].initial_display_delay = 10;
        }
      } else {
        seq_params->op_params[i].display_model_param_present_flag = 0;
        seq_params->op_params[i].initial_display_delay = 10;
      }
    }
  }
  // This decoder supports all levels.  Choose operating point provided by
  // external means
  int operating_point = pbi->operating_point;
  if (operating_point < 0 ||
      operating_point > seq_params->operating_points_cnt_minus_1)
    operating_point = 0;
  pbi->current_operating_point =
      seq_params->operating_point_idc[operating_point];
  if (aom_get_num_layers_from_operating_point_idc(
          pbi->current_operating_point, &cm->number_mlayers,
          &cm->number_tlayers) != AOM_CODEC_OK) {
    cm->error.error_code = AOM_CODEC_ERROR;
    return 0;
  }

  if (seq_params->single_picture_header_flag) {
    seq_params->max_tlayer_id = 0;
    seq_params->max_mlayer_id = 0;
  } else {
    seq_params->max_tlayer_id = aom_rb_read_literal(rb, TLAYER_BITS);
    seq_params->max_mlayer_id = aom_rb_read_literal(rb, MLAYER_BITS);
  }

  // setup default temporal layer dependency
  setup_default_temporal_layer_dependency_structure(seq_params);
  // setup default embedded layer dependency
  setup_default_embedded_layer_dependency_structure(seq_params);

  // tlayer dependency description
  seq_params->tlayer_dependency_present_flag = 0;
  if (seq_params->max_tlayer_id > 0) {
    seq_params->tlayer_dependency_present_flag = aom_rb_read_bit(rb);
    if (seq_params->tlayer_dependency_present_flag) {
      av1_read_tlayer_dependency_info(seq_params, rb);
    }
  }

  // mlayer dependency description
  seq_params->mlayer_dependency_present_flag = 0;
  if (seq_params->max_mlayer_id > 0) {
    seq_params->mlayer_dependency_present_flag = aom_rb_read_bit(rb);
    if (seq_params->mlayer_dependency_present_flag) {
      av1_read_mlayer_dependency_info(seq_params, rb);
    }
  }

  av1_read_sequence_header(rb, seq_params
#if CONFIG_IMPROVED_REORDER_SEQ_FLAGS && !CONFIG_F255_QMOBU
                           ,
                           &cm->quant_params, &cm->error
#endif  // CONFIG_IMPROVED_REORDER_SEQ_FLAGS && !CONFIG_F255_QMOBU
  );

  seq_params->film_grain_params_present = aom_rb_read_bit(rb);

#if !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  // Sequence header for coding tools beyond AV1
  av1_read_sequence_header_beyond_av1(rb, seq_params
#if !CONFIG_F255_QMOBU
                                      ,
                                      &cm->quant_params, &cm->error
#endif  // !CONFIG_F255_QMOBU
  );
#endif  // !CONFIG_IMPROVED_REORDER_SEQ_FLAGS

#if !CONFIG_CWG_F270_CI_OBU
#if CONFIG_SCAN_TYPE_METADATA
  seq_params->scan_type_info_present_flag = aom_rb_read_bit(rb);
  if (seq_params->scan_type_info_present_flag) {
    seq_params->scan_type_idc = aom_rb_read_literal(rb, 2);
    seq_params->fixed_cvs_pic_rate_flag = aom_rb_read_bit(rb);
    if (seq_params->fixed_cvs_pic_rate_flag)
      seq_params->elemental_ct_duration_minus_1 = aom_rb_read_uvlc(rb);
    else
      seq_params->elemental_ct_duration_minus_1 = -1;
  } else {
    seq_params->scan_type_idc = 0;
    seq_params->fixed_cvs_pic_rate_flag = 0;
    seq_params->elemental_ct_duration_minus_1 = -1;
  }
  if (seq_params->elemental_ct_duration_minus_1 + 1 < 0 ||
      seq_params->elemental_ct_duration_minus_1 + 1 > 2046) {
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "The value of elemental_ct_duration_minus_1 + 1 shall "
                       "be in the range of 0 to 2046.\n",
                       seq_params->elemental_ct_duration_minus_1);
  }
#endif  // CONFIG_SCAN_TYPE_METADATA
#endif  // !CONFIG_CWG_F270_CI_OBU

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }

  // If a sequence header has been decoded before, we check if the new
  // one is consistent with the old one.
  if (pbi->sequence_header_ready) {
    if (!are_seq_headers_consistent(&cm->seq_params, seq_params)) {
      pbi->sequence_header_changed = 1;
#if !CONFIG_F255_QMOBU
      cm->quant_params.qmatrix_initialized = false;
#endif  // !CONFIG_F255_QMOBU
#if CONFIG_MULTI_FRAME_HEADER
      reset_mfh_valid(cm);
#endif  // CONFIG_MULTI_FRAME_HEADER
#if CONFIG_CWG_F270_CI_OBU
      pbi->ci_params_received = 0;
#endif  // CONFIG_CWG_F270_CI_OBU
    }
  }
#if CONFIG_F024_KEYOBU
  pbi->is_first_layer_decoded = true;
  for (int layer = 0; layer <= seq_params->max_mlayer_id; layer++)
    cm->olk_refresh_frame_flags[layer] = -1;
#endif
  cm->seq_params = *seq_params;
  av1_set_frame_sb_size(cm, cm->seq_params.sb_size);
  pbi->sequence_header_ready = 1;

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

#if CONFIG_MULTI_FRAME_HEADER
static uint32_t read_multi_frame_header_obu(AV1Decoder *pbi,
                                            struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  av1_read_multi_frame_header(cm, rb);

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
#endif  // CONFIG_MULTI_FRAME_HEADER

static uint32_t read_tilegroup_obu(AV1Decoder *pbi,
                                   struct aom_read_bit_buffer *rb,
                                   const uint8_t *data, const uint8_t *data_end,
                                   const uint8_t **p_data_end,
                                   OBU_TYPE obu_type, int *is_last_tg) {
  AV1_COMMON *const cm = &pbi->common;
  int start_tile, end_tile;
  int32_t header_size, tg_payload_size;

  assert(rb->bit_offset == 0);
  assert(rb->bit_buffer == data);

  int is_first_tg = 1;  // return from av1_read_tilegroup_header
  header_size =
      av1_read_tilegroup_header(pbi, rb, data, p_data_end, &is_first_tg,
                                &start_tile, &end_tile, obu_type);

  bool skip_payload = false;
#if CONFIG_F024_KEYOBU
  skip_payload |= (obu_type == OBU_LEADING_SEF);
  skip_payload |= (obu_type == OBU_REGULAR_SEF);
#else
  skip_payload |= (obu_type == OBU_SEF);
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_F024_KEYOBU
  skip_payload |= (obu_type == OBU_LEADING_TIP);
  skip_payload |= (obu_type == OBU_REGULAR_TIP);
#else
  skip_payload |= (obu_type == OBU_TIP);
#endif  // CONFIG_F024_KEYOBU
  skip_payload |= cm->bru.frame_inactive_flag;
#if CONFIG_CWG_F317
  skip_payload |= cm->bridge_frame_info.is_bridge_frame;
#endif  // CONFIG_CWG_F317

  if (skip_payload) {
    *is_last_tg = 1;
    tg_payload_size = 0;
    if (av1_check_trailing_bits(pbi, rb) != 0) {
      // cm->error.error_code is already set.
      return 0;
    }
    header_size = (int32_t)aom_rb_bytes_read(rb);
  } else {
    if (av1_check_byte_alignment(cm, rb)) return 0;
    data += header_size;

    av1_decode_tg_tiles_and_wrapup(pbi, data, data_end, p_data_end, start_tile,
                                   end_tile, is_first_tg);

    tg_payload_size = (uint32_t)(*p_data_end - data);
    *is_last_tg = end_tile == cm->tiles.rows * cm->tiles.cols - 1;
  }
  return header_size + tg_payload_size;
}

#if !CONFIG_METADATA || CONFIG_SHORT_METADATA
// Returns the last nonzero byte index in 'data'. If there is no nonzero byte in
// 'data', returns -1.
static int get_last_nonzero_byte_index(const uint8_t *data, size_t sz) {
  // Scan backward and return on the first nonzero byte.
  int i = (int)sz - 1;
  while (i >= 0 && data[i] == 0) {
    --i;
  }
  return i;
}
#endif  // !CONFIG_METADATA || CONFIG_SHORT_METADATA
// Allocates metadata that was read and adds it to the decoders metadata array.
static void alloc_read_metadata(AV1Decoder *const pbi,
                                OBU_METADATA_TYPE metadata_type,
                                const uint8_t *data, size_t sz,
                                aom_metadata_insert_flags_t insert_flag) {
  AV1_COMMON *const cm = &pbi->common;
  if (!pbi->metadata) {
    pbi->metadata = aom_img_metadata_array_alloc(0);
    if (!pbi->metadata) {
      aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate metadata array");
    }
  }
  aom_metadata_t *metadata =
      aom_img_metadata_alloc(metadata_type, data, sz, insert_flag);
  if (!metadata) {
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Error allocating metadata");
  }
  aom_metadata_t **metadata_array =
      (aom_metadata_t **)realloc(pbi->metadata->metadata_array,
                                 (pbi->metadata->sz + 1) * sizeof(metadata));
  if (!metadata_array) {
    aom_img_metadata_free(metadata);
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Error growing metadata array");
  }
  pbi->metadata->metadata_array = metadata_array;
  pbi->metadata->metadata_array[pbi->metadata->sz] = metadata;
  pbi->metadata->sz++;
}

// On failure, calls aom_internal_error() and does not return.
static void read_metadata_itut_t35(AV1Decoder *const pbi, const uint8_t *data,
                                   size_t sz) {
  AV1_COMMON *const cm = &pbi->common;
  if (sz == 0) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "itu_t_t35_country_code is missing");
  }
  int country_code_size = 1;
  if (*data == 0xFF) {
    if (sz == 1) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "itu_t_t35_country_code_extension_byte is missing");
    }
    ++country_code_size;
  }
#if CONFIG_METADATA
  const int end_index = (int)sz;
#else
  int end_index = get_last_nonzero_byte_index(data, sz);
#endif  // CONFIG_METADATA
  if (end_index < country_code_size) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "No trailing bits found in ITU-T T.35 metadata OBU");
  }
#if !CONFIG_METADATA
  // itu_t_t35_payload_bytes is byte aligned. Section 6.7.2 of the spec says:
  //   itu_t_t35_payload_bytes shall be bytes containing data registered as
  //   specified in Recommendation ITU-T T.35.
  // Therefore the first trailing byte should be 0x80.
  if (data[end_index] != 0x80) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "The last nonzero byte of the ITU-T T.35 metadata OBU "
                       "is 0x%02x, should be 0x80.",
                       data[end_index]);
  }
#endif  // !CONFIG_METADATA
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_ITUT_T35, data, end_index,
                      AOM_MIF_ANY_FRAME);
}

#if CONFIG_SHORT_METADATA
// On failure, calls aom_internal_error() and does not return.
static void read_metadata_itut_t35_short(AV1Decoder *const pbi,
                                         const uint8_t *data, size_t sz) {
  AV1_COMMON *const cm = &pbi->common;
  if (sz == 0) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "itu_t_t35_country_code is missing");
  }
  int country_code_size = 1;
  if (*data == 0xFF) {
    if (sz == 1) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "itu_t_t35_country_code_extension_byte is missing");
    }
    ++country_code_size;
  }
  int end_index = get_last_nonzero_byte_index(data, sz);
  if (end_index < country_code_size) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "No trailing bits found in ITU-T T.35 metadata OBU");
  }
  // itu_t_t35_payload_bytes is byte aligned. Section 6.7.2 of the spec says:
  //   itu_t_t35_payload_bytes shall be bytes containing data registered as
  //   specified in Recommendation ITU-T T.35.
  // Therefore the first trailing byte should be 0x80.
  if (data[end_index] != 0x80) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "The last nonzero byte of the ITU-T T.35 metadata OBU "
                       "is 0x%02x, should be 0x80.",
                       data[end_index]);
  }

  alloc_read_metadata(pbi, OBU_METADATA_TYPE_ITUT_T35, data, end_index,
                      AOM_MIF_ANY_FRAME);
}
#endif  // CONFIG_SHORT_METADATA
// On success, returns the number of bytes read from 'data'. On failure, calls
// aom_internal_error() and does not return.
static size_t read_metadata_hdr_cll(AV1Decoder *const pbi, const uint8_t *data,
                                    size_t sz) {
  const size_t kHdrCllPayloadSize = 4;
  AV1_COMMON *const cm = &pbi->common;
  if (sz < kHdrCllPayloadSize) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Incorrect HDR CLL metadata payload size");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_HDR_CLL, data, kHdrCllPayloadSize,
                      AOM_MIF_ANY_FRAME);
  return kHdrCllPayloadSize;
}

// On success, returns the number of bytes read from 'data'. On failure, calls
// aom_internal_error() and does not return.
static size_t read_metadata_hdr_mdcv(AV1Decoder *const pbi, const uint8_t *data,
                                     size_t sz) {
  const size_t kMdcvPayloadSize = 24;
  AV1_COMMON *const cm = &pbi->common;
  if (sz < kMdcvPayloadSize) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Incorrect HDR MDCV metadata payload size");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_HDR_MDCV, data, kMdcvPayloadSize,
                      AOM_MIF_ANY_FRAME);
  return kMdcvPayloadSize;
}

#if CONFIG_BAND_METADATA
// On success, returns the number of bytes read from 'data'. On failure, calls
// aom_internal_error() and does not return.
static size_t read_metadata_banding_hints(AV1Decoder *const pbi,
                                          const uint8_t *data, size_t sz) {
  AV1_COMMON *const cm = &pbi->common;

  // Validate minimum payload size (at least 3 bits for basic flags)
  if (sz == 0) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Empty banding hints metadata payload");
  }

  // Store the raw payload in the generic metadata array
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_BANDING_HINTS, data, sz,
                      AOM_MIF_ANY_FRAME);

  return sz;
}
#endif  // CONFIG_BAND_METADATA

#if CONFIG_ICC_METADATA
// On success, returns the number of bytes read from 'data'. On failure, calls
// aom_internal_error() and does not return.
static size_t read_metadata_icc_profile(AV1Decoder *const pbi,
                                        const uint8_t *data, size_t sz) {
  const size_t kMinIccProfileHeaderSize = 128;
  AV1_COMMON *const cm = &pbi->common;
  if (sz < kMinIccProfileHeaderSize) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Incorrect ICC profile metadata payload size");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_ICC_PROFILE, data, sz,
                      AOM_MIF_ANY_FRAME);
  return sz;
}
#endif  // CONFIG_ICC_METADATA

#if CONFIG_SCAN_TYPE_METADATA
// On success, returns the number of bytes read from 'data'. On failure, calls
// aom_internal_error() and does not return.
static void read_metadata_scan_type(AV1Decoder *const pbi,
                                    struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  cm->pic_struct_metadata_params.mps_pic_struct_type =
      aom_rb_read_literal(rb, 5);
  cm->pic_struct_metadata_params.mps_source_scan_type_idc =
      aom_rb_read_literal(rb, 2);
  cm->pic_struct_metadata_params.mps_duplicate_flag = aom_rb_read_bit(rb);

#if CONFIG_SHORT_METADATA
  uint8_t payload[1];
  payload[0] = (cm->pic_struct_metadata_params.mps_pic_struct_type << 3) |
               (cm->pic_struct_metadata_params.mps_source_scan_type_idc << 1) |
               cm->pic_struct_metadata_params.mps_duplicate_flag;
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_SCAN_TYPE, payload, 1,
                      AOM_MIF_ANY_FRAME);
#endif  // CONFIG_SHORT_METADATA
}
#endif  // CONFIG_SCAN_TYPE_METADATA

static int read_metadata_frame_hash(AV1Decoder *const pbi,
                                    struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const unsigned hash_type = aom_rb_read_literal(rb, 4);
  const unsigned per_plane = aom_rb_read_bit(rb);
  const unsigned has_grain = aom_rb_read_bit(rb);
  aom_rb_read_literal(rb, 2);  // reserved

  // If hash_type is reserved for future use, ignore the entire OBU
  if (hash_type) return -1;

  FrameHash *const frame_hash = has_grain ? &cm->cur_frame->grain_frame_hash
                                          : &cm->cur_frame->raw_frame_hash;
  memset(frame_hash, 0, sizeof(*frame_hash));

  frame_hash->hash_type = hash_type;
  frame_hash->per_plane = per_plane;
  frame_hash->has_grain = has_grain;
  if (per_plane) {
    const int num_planes = av1_num_planes(cm);
    for (int i = 0; i < num_planes; ++i) {
      PlaneHash *plane = &frame_hash->plane[i];
      for (size_t j = 0; j < 16; ++j)
        plane->md5[j] = aom_rb_read_literal(rb, 8);
    }
  } else {
    PlaneHash *plane = &frame_hash->plane[0];
    for (size_t i = 0; i < 16; ++i) plane->md5[i] = aom_rb_read_literal(rb, 8);
  }
  frame_hash->is_present = 1;

  return 0;
}

static void scalability_structure(struct aom_read_bit_buffer *rb) {
  const int spatial_layers_cnt_minus_1 = aom_rb_read_literal(rb, 2);
  const int spatial_layer_dimensions_present_flag = aom_rb_read_bit(rb);
  const int spatial_layer_description_present_flag = aom_rb_read_bit(rb);
  const int temporal_group_description_present_flag = aom_rb_read_bit(rb);
  aom_rb_read_literal(rb, 3);  // reserved

  if (spatial_layer_dimensions_present_flag) {
    for (int i = 0; i <= spatial_layers_cnt_minus_1; i++) {
      aom_rb_read_literal(rb, 16);
      aom_rb_read_literal(rb, 16);
    }
  }
  if (spatial_layer_description_present_flag) {
    for (int i = 0; i <= spatial_layers_cnt_minus_1; i++) {
      aom_rb_read_literal(rb, 8);
    }
  }
  if (temporal_group_description_present_flag) {
    const int temporal_group_size = aom_rb_read_literal(rb, 8);
    for (int i = 0; i < temporal_group_size; i++) {
      aom_rb_read_literal(rb, 3);
      aom_rb_read_bit(rb);
      aom_rb_read_bit(rb);
      const int temporal_group_ref_cnt = aom_rb_read_literal(rb, 3);
      for (int j = 0; j < temporal_group_ref_cnt; j++) {
        aom_rb_read_literal(rb, 8);
      }
    }
  }
}

static void read_metadata_scalability(struct aom_read_bit_buffer *rb) {
  const int scalability_mode_idc = aom_rb_read_literal(rb, 8);
  if (scalability_mode_idc == SCALABILITY_SS) {
    scalability_structure(rb);
  }
}

static void read_metadata_timecode(struct aom_read_bit_buffer *rb) {
  aom_rb_read_literal(rb, 5);  // counting_type f(5)
  const int full_timestamp_flag =
      aom_rb_read_bit(rb);     // full_timestamp_flag f(1)
  aom_rb_read_bit(rb);         // discontinuity_flag (f1)
  aom_rb_read_bit(rb);         // cnt_dropped_flag f(1)
  aom_rb_read_literal(rb, 9);  // n_frames f(9)
  if (full_timestamp_flag) {
    aom_rb_read_literal(rb, 6);  // seconds_value f(6)
    aom_rb_read_literal(rb, 6);  // minutes_value f(6)
    aom_rb_read_literal(rb, 5);  // hours_value f(5)
  } else {
    const int seconds_flag = aom_rb_read_bit(rb);  // seconds_flag f(1)
    if (seconds_flag) {
      aom_rb_read_literal(rb, 6);                    // seconds_value f(6)
      const int minutes_flag = aom_rb_read_bit(rb);  // minutes_flag f(1)
      if (minutes_flag) {
        aom_rb_read_literal(rb, 6);                  // minutes_value f(6)
        const int hours_flag = aom_rb_read_bit(rb);  // hours_flag f(1)
        if (hours_flag) {
          aom_rb_read_literal(rb, 5);  // hours_value f(5)
        }
      }
    }
  }
  // time_offset_length f(5)
  const int time_offset_length = aom_rb_read_literal(rb, 5);
  if (time_offset_length) {
    // time_offset_value f(time_offset_length)
    aom_rb_read_literal(rb, time_offset_length);
  }
}

// Returns the last nonzero byte in 'data'. If there is no nonzero byte in
// 'data', returns 0.
//
// Call this function to check the following requirement in the spec:
//   This implies that when any payload data is present for this OBU type, at
//   least one byte of the payload data (including the trailing bit) shall not
//   be equal to 0.
static uint8_t get_last_nonzero_byte(const uint8_t *data, size_t sz) {
  // Scan backward and return on the first nonzero byte.
  size_t i = sz;
  while (i != 0) {
    --i;
    if (data[i] != 0) return data[i];
  }
  return 0;
}

// Checks the metadata for correct syntax but ignores the parsed metadata.
//
// On success, returns the number of bytes read from 'data'. On failure, sets
// pbi->common.error.error_code and returns 0, or calls aom_internal_error()
// and does not return.
#if CONFIG_METADATA
static size_t read_metadata_unit_payload(AV1Decoder *pbi, const uint8_t *data,
                                         aom_metadata_t *metadata)
#else
static size_t read_metadata(AV1Decoder *pbi, const uint8_t *data, size_t sz)
#endif  // CONFIG_METADATA
{
#if !CONFIG_METADATA
  AV1_COMMON *const cm = &pbi->common;
  size_t type_length;
  uint64_t type_value;
  if (aom_uleb_decode(data, sz, &type_value, &type_length) < 0) {
    cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }
  const OBU_METADATA_TYPE metadata_type = (OBU_METADATA_TYPE)type_value;
#else
  size_t type_length = 0;
  const OBU_METADATA_TYPE metadata_type = metadata->type;
  const size_t sz = metadata->sz;
#endif  // !CONFIG_METADATA

#if CONFIG_METADATA
  int known_metadata_type = metadata_type >= OBU_METADATA_TYPE_HDR_CLL &&
                            metadata_type < NUM_OBU_METADATA_TYPES;
  known_metadata_type |= metadata_type == OBU_METADATA_TYPE_ICC_PROFILE;
#if CONFIG_SCAN_TYPE_METADATA
  known_metadata_type |= metadata_type == OBU_METADATA_TYPE_SCAN_TYPE;
#endif  // CONFIG_SCAN_TYPE_METADATA
  if (!known_metadata_type)
#else  // CONFIG_ICC_METADATA
#if CONFIG_BAND_METADATA
  if (metadata_type == 0 || metadata_type >= 8)
#else
  if (metadata_type == 0 || metadata_type >= 7)
#endif  // CONFIG_BAND_METADATA
#endif  // CONFIG_ICC_METADATA
  {
#if !CONFIG_METADATA
    // If metadata_type is reserved for future use or a user private value,
    // ignore the entire OBU and just check trailing bits.
    if (get_last_nonzero_byte(data + type_length, sz - type_length) == 0) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
#endif  // !CONFIG_METADATA
    return sz;
  }
  if (metadata_type == OBU_METADATA_TYPE_ITUT_T35) {
#if !CONFIG_METADATA
    // read_metadata_itut_t35() checks trailing bits.
#endif  // !CONFIG_METADATA
    read_metadata_itut_t35(pbi, data + type_length, sz - type_length);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_CLL) {
#if !CONFIG_METADATA
    size_t bytes_read =
        type_length +
#endif  // !CONFIG_METADATA
        read_metadata_hdr_cll(pbi, data + type_length, sz - type_length);
#if !CONFIG_METADATA
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
#endif  // !CONFIG_METADATA
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_MDCV) {
#if !CONFIG_METADATA
    size_t bytes_read =
        type_length +
#endif  // !CONFIG_METADATA
        read_metadata_hdr_mdcv(pbi, data + type_length, sz - type_length);
#if !CONFIG_METADATA
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
#endif  // !CONFIG_METADATA
    return sz;
#if CONFIG_BAND_METADATA
  } else if (metadata_type == OBU_METADATA_TYPE_BANDING_HINTS) {
#if !CONFIG_METADATA
    size_t bytes_read =
        type_length +
#endif  // !CONFIG_METADATA
        read_metadata_banding_hints(pbi, data + type_length, sz - type_length);
#if !CONFIG_METADATA
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    return sz;
#endif  // !CONFIG_METADATA
#endif  // CONFIG_BAND_METADATA
#if CONFIG_SCAN_TYPE_METADATA
  } else if (metadata_type == OBU_METADATA_TYPE_SCAN_TYPE) {
    struct aom_read_bit_buffer rb;
    av1_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
    read_metadata_scan_type(pbi, &rb);
    return sz;
#endif  // CONFIG_SCAN_TYPE_METADATA
#if CONFIG_METADATA
  } else if (metadata_type == OBU_METADATA_TYPE_ICC_PROFILE) {
#if !CONFIG_METADATA
    size_t bytes_read =
        type_length +
#endif  // !CONFIG_METADATA
        read_metadata_icc_profile(pbi, data + type_length, sz - type_length);
#if !CONFIG_METADATA
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
#endif  // !CONFIG_METADATA
    return sz;
#endif  // CONFIG_ICC_METADATA
  }

  struct aom_read_bit_buffer rb;
  av1_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
  if (metadata_type == OBU_METADATA_TYPE_SCALABILITY) {
    read_metadata_scalability(&rb);
  } else if (metadata_type == OBU_METADATA_TYPE_DECODED_FRAME_HASH) {
    if (read_metadata_frame_hash(pbi, &rb)) {
#if !CONFIG_METADATA
      // Unsupported Decoded Frame Hash metadata. Ignoring the entire OBU and
      // just checking trailing bits
      if (get_last_nonzero_byte(data + type_length, sz - type_length) == 0) {
        cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
        return 0;
      }
#endif  // !CONFIG_METADATA
      return sz;
    }
  } else {
    assert(metadata_type == OBU_METADATA_TYPE_TIMECODE);
    read_metadata_timecode(&rb);
  }
#if !CONFIG_METADATA
  if (av1_check_trailing_bits(pbi, &rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
#endif  // !CONFIG_METADATA
  assert((rb.bit_offset & 7) == 0);
  return type_length + (rb.bit_offset >> 3);
}

#if CONFIG_METADATA
static size_t read_metadata_obsp(AV1Decoder *pbi, const uint8_t *data,
                                 size_t sz,
                                 aom_metadata_array_t *metadata_array,
                                 aom_metadata_t *metadata_base) {
  AV1_COMMON *const cm = &pbi->common;

  struct aom_read_bit_buffer rb;
  av1_init_read_bit_buffer(pbi, &rb, data, data + sz);

  metadata_base->is_suffix = aom_rb_read_literal(&rb, 1);
  metadata_base->necessity_idc =
      (aom_metadata_necessity_t)aom_rb_read_literal(&rb, 2);
  metadata_base->application_id =
      (aom_metadata_application_id_t)aom_rb_read_literal(&rb, 5);

  const size_t bytes_read = aom_rb_bytes_read(&rb);
  assert(bytes_read == 1);

  size_t count_length;
  uint64_t count_value;
  if (aom_uleb_decode(data + bytes_read, sz - bytes_read, &count_value,
                      &count_length) < 0) {
    cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  metadata_array->sz = count_value;

  return bytes_read + count_length;
}

static size_t read_metadata_unit_header(AV1Decoder *pbi, const uint8_t *data,
                                        size_t sz, aom_metadata_t *metadata,
                                        const ObuHeader *obu_header) {
  AV1_COMMON *const cm = &pbi->common;
  size_t type_length;
  uint64_t type_value;
  if (aom_uleb_decode(data, sz, &type_value, &type_length) < 0) {
    cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }
  metadata->type = (uint32_t)type_value;
  size_t bytes_read = type_length;

  struct aom_read_bit_buffer rb;
  av1_init_read_bit_buffer(pbi, &rb, data + bytes_read, data + sz);

  const size_t muh_header_size = aom_rb_read_literal(&rb, 7);
  metadata->cancel_flag = aom_rb_read_literal(&rb, 1);
  assert(aom_rb_bytes_read(&rb) == 1);
  bytes_read += aom_rb_bytes_read(&rb);

  const size_t total_size = bytes_read + muh_header_size;
  assert(total_size <= sz);

  if (!metadata->cancel_flag) {
    size_t size_length;
    uint64_t size_value = 0;
    if (aom_uleb_decode(data + bytes_read, sz - bytes_read, &size_value,
                        &size_length) < 0) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    metadata->sz = size_value;
    bytes_read += size_length;

    av1_init_read_bit_buffer(pbi, &rb, data + bytes_read, data + sz);

    metadata->layer_idc = (aom_metadata_layer_t)aom_rb_read_literal(&rb, 3);
    metadata->persistence_idc =
        (aom_metadata_persistence_t)aom_rb_read_literal(&rb, 3);
    metadata->priority = aom_rb_read_literal(&rb, 8);
    aom_rb_read_literal(&rb, 2);  // reserved bits

    assert(aom_rb_bytes_read(&rb) == 2);

    if (metadata->layer_idc == AOM_LAYER_VALUES) {
      if (obu_header->obu_xlayer_id == 31) {
        metadata->xlayer_map = aom_rb_read_unsigned_literal(&rb, 32);
        assert((metadata->xlayer_map & (1u << 31)) == 0);
        for (int n = 0; n < 31; n++) {
          if (metadata->xlayer_map & (1u << n)) {
            metadata->mlayer_map[n] = aom_rb_read_unsigned_literal(&rb, 8);
          }
        }
      } else {
        metadata->mlayer_map[obu_header->obu_xlayer_id] =
            aom_rb_read_unsigned_literal(&rb, 8);
      }
    }

    bytes_read += aom_rb_bytes_read(&rb);
  }

  assert(bytes_read <= total_size);
  return total_size;
}

static size_t read_metadata_obu(AV1Decoder *pbi, const uint8_t *data, size_t sz,
                                ObuHeader *obu_header) {
  AV1_COMMON *const cm = &pbi->common;

  aom_metadata_array_t metadata_array = { 0 };
  aom_metadata_t metadata_base;
  memset(&metadata_base, 0, sizeof(metadata_base));
  size_t bytes_read =
      read_metadata_obsp(pbi, data, sz, &metadata_array, &metadata_base);

  for (uint32_t i = 0; i < metadata_array.sz; i++) {
    aom_metadata_t metadata = { 0 };
    // copy shared fields read in `read_metadata_obsp`
    memcpy(&metadata, &metadata_base, sizeof(metadata));

    const size_t muh_size = read_metadata_unit_header(
        pbi, data + bytes_read, sz - bytes_read, &metadata, obu_header);
    if (muh_size == 0) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    bytes_read += muh_size;
    if (sz - bytes_read < metadata.sz) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    const size_t mup_size =
        read_metadata_unit_payload(pbi, data + bytes_read, &metadata);
    bytes_read += mup_size;
  }

  if (bytes_read >= sz || data[bytes_read] != 0x80) {
    cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }
  return bytes_read + 1;
}
#endif  // CONFIG_METADATA

#if CONFIG_SHORT_METADATA
// Checks the metadata for correct syntax but ignores the parsed metadata.
//
// On success, returns the number of bytes read from 'data'. On failure, sets
// pbi->common.error.error_code and returns 0, or calls aom_internal_error()
// and does not return.
static size_t read_metadata_short(AV1Decoder *pbi, const uint8_t *data,
                                  size_t sz) {
  AV1_COMMON *const cm = &pbi->common;
  size_t type_length;
  uint64_t type_value;
  // TODO: [@anorkin] this part may need to be revisited considering
  // CONFIG_SHORT_METADATA and CONFIG_METADATA
  struct aom_read_bit_buffer rb;
  av1_init_read_bit_buffer(pbi, &rb, data, data + sz);

  uint8_t metadata_is_suffix = aom_rb_read_bit(&rb);
  uint8_t muh_layer_idc = aom_rb_read_literal(&rb, 3);
  uint8_t muh_cancel_flag = aom_rb_read_bit(&rb);
  uint8_t muh_persistence_idc = aom_rb_read_literal(&rb, 3);
  if (aom_uleb_decode(
          data + 1,  // read type from the position data + 1
          sz - 1,    // one less bytes available due to extra parameters
          &type_value, &type_length) < 0) {
    cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  const OBU_METADATA_TYPE metadata_type = (OBU_METADATA_TYPE)type_value;

  // Increase the type_length by 1 byte since there is one prefix byte added
  // before the type
  ++type_length;
  if (muh_cancel_flag) return sz;

  // Update the metadata with the header fields we read
  if (pbi->metadata && pbi->metadata->sz > 0) {
    aom_metadata_t *last_metadata =
        pbi->metadata->metadata_array[pbi->metadata->sz - 1];
    if (last_metadata && last_metadata->type == OBU_METADATA_TYPE_ITUT_T35) {
      last_metadata->is_suffix = metadata_is_suffix;
      last_metadata->layer_idc = muh_layer_idc;
      last_metadata->cancel_flag = muh_cancel_flag;
      last_metadata->persistence_idc = muh_persistence_idc;
    }
  }

#if CONFIG_BAND_METADATA
  if (metadata_type == 0 || metadata_type >= 8)
#else
  if (metadata_type == 0 || metadata_type >= 7)
#endif  // CONFIG_BAND_METADATA
  {
    // If metadata_type is reserved for future use or a user private value,
    // ignore the entire OBU and just check trailing bits.
    if (get_last_nonzero_byte(data + type_length, sz - type_length) == 0) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    return sz;
  }
  if (metadata_type == OBU_METADATA_TYPE_ITUT_T35) {
    // read_metadata_itut_t35() checks trailing bits.
    read_metadata_itut_t35_short(pbi, data + type_length, sz - type_length);
    // Update the metadata with the header fields we read
    if (pbi->metadata && pbi->metadata->sz > 0) {
      aom_metadata_t *last_metadata =
          pbi->metadata->metadata_array[pbi->metadata->sz - 1];
      if (last_metadata && last_metadata->type == OBU_METADATA_TYPE_ITUT_T35) {
        last_metadata->is_suffix = metadata_is_suffix;
        last_metadata->layer_idc = muh_layer_idc;
        last_metadata->cancel_flag = muh_cancel_flag;
        last_metadata->persistence_idc = muh_persistence_idc;
      }
    }
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_CLL) {
    size_t bytes_read =
        type_length +
        read_metadata_hdr_cll(pbi, data + type_length, sz - type_length);
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    // Update the metadata with the header fields we read
    if (pbi->metadata && pbi->metadata->sz > 0) {
      aom_metadata_t *last_metadata =
          pbi->metadata->metadata_array[pbi->metadata->sz - 1];
      if (last_metadata && last_metadata->type == OBU_METADATA_TYPE_HDR_CLL) {
        last_metadata->is_suffix = metadata_is_suffix;
        last_metadata->layer_idc = muh_layer_idc;
        last_metadata->cancel_flag = muh_cancel_flag;
        last_metadata->persistence_idc = muh_persistence_idc;
      }
    }
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_MDCV) {
    size_t bytes_read =
        type_length +
        read_metadata_hdr_mdcv(pbi, data + type_length, sz - type_length);
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    // Update the metadata with the header fields we read
    if (pbi->metadata && pbi->metadata->sz > 0) {
      aom_metadata_t *last_metadata =
          pbi->metadata->metadata_array[pbi->metadata->sz - 1];
      if (last_metadata && last_metadata->type == OBU_METADATA_TYPE_HDR_MDCV) {
        last_metadata->is_suffix = metadata_is_suffix;
        last_metadata->layer_idc = muh_layer_idc;
        last_metadata->cancel_flag = muh_cancel_flag;
        last_metadata->persistence_idc = muh_persistence_idc;
      }
    }
    return sz;
#if CONFIG_BAND_METADATA
  } else if (metadata_type == OBU_METADATA_TYPE_BANDING_HINTS) {
    size_t bytes_read =
        type_length +
        read_metadata_banding_hints(pbi, data + type_length, sz - type_length);
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    return sz;
#endif  // CONFIG_BAND_METADATA
#if CONFIG_SCAN_TYPE_METADATA
  } else if (metadata_type == OBU_METADATA_TYPE_SCAN_TYPE) {
    const size_t kMinScanTypeHeaderSize = 1;
    if (sz < kMinScanTypeHeaderSize) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Incorrect scan type metadata payload size");
    }
    av1_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
    read_metadata_scan_type(pbi, &rb);
    return sz;
#endif  // CONFIG_SCAN_TYPE_METADATA
  }

  av1_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
  if (metadata_type == OBU_METADATA_TYPE_SCALABILITY) {
    read_metadata_scalability(&rb);
  } else if (metadata_type == OBU_METADATA_TYPE_DECODED_FRAME_HASH) {
    if (read_metadata_frame_hash(pbi, &rb)) {
      // Unsupported Decoded Frame Hash metadata. Ignoring the entire OBU and
      // just checking trailing bits
      if (get_last_nonzero_byte(data + type_length, sz - type_length) == 0) {
        cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
        return 0;
      }
      return sz;
    }
  } else {
    assert(metadata_type == OBU_METADATA_TYPE_TIMECODE);
    read_metadata_timecode(&rb);
  }
  if (av1_check_trailing_bits(pbi, &rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
  assert((rb.bit_offset & 7) == 0);
  return type_length + (rb.bit_offset >> 3);
}
#endif  // CONFIG_SHORT_METADATA
// On success, returns 'sz'. On failure, sets pbi->common.error.error_code and
// returns 0.
static size_t read_padding(AV1_COMMON *const cm, const uint8_t *data,
                           size_t sz) {
  // The spec allows a padding OBU to be header-only (i.e., obu_size = 0). So
  // check trailing bits only if sz > 0.
  if (sz > 0) {
    // The payload of a padding OBU is byte aligned. Therefore the first
    // trailing byte should be 0x80. See https://crbug.com/aomedia/2393.
    const uint8_t last_nonzero_byte = get_last_nonzero_byte(data, sz);
    if (last_nonzero_byte != 0x80) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return 0;
    }
  }
  return sz;
}

// Check the obu type is a kind of coded frame
static int is_coded_frame(OBU_TYPE obu_type) {
#if CONFIG_F024_KEYOBU
  return obu_type == OBU_REGULAR_SEF || obu_type == OBU_REGULAR_TIP ||
         obu_type == OBU_SWITCH || obu_type == OBU_RAS_FRAME ||
         obu_type == OBU_BRIDGE_FRAME || obu_type == OBU_REGULAR_TILE_GROUP ||
         obu_type == OBU_LEADING_SEF || obu_type == OBU_LEADING_TIP ||
         obu_type == OBU_LEADING_TILE_GROUP || obu_type == OBU_CLK ||
         obu_type == OBU_OLK;
#else
  return obu_type == OBU_SEF || obu_type == OBU_TIP || obu_type == OBU_SWITCH ||
         obu_type == OBU_RAS_FRAME || obu_type == OBU_BRIDGE_FRAME ||
         obu_type == OBU_TILE_GROUP;
#endif
}

// Check the obu type ordering within a temporal unit
// as a part of checking bitstream conformance.
// On success, return 0. If failed return 1.
#if OBU_ORDER_IN_TU
static int check_obu_order(OBU_TYPE prev_obu_type, OBU_TYPE curr_obu_type) {
  // TODO: avm#1115 - Rewrite check_obu_order() to better express all OBU
  // ordering constraints.
#if CONFIG_F153_FGM_OBU
  if (curr_obu_type == OBU_FGM || prev_obu_type == OBU_FGM) {
    return 0;
  }
#endif  // CONFIG_F153_FGM_OBU
#if CONFIG_F255_QMOBU
  if (is_coded_frame(curr_obu_type) && prev_obu_type == OBU_QM) {
    return 0;
  } else if (curr_obu_type == OBU_QM) {
    return 0;
  } else
#endif
      if ((prev_obu_type == OBU_TEMPORAL_DELIMITER) &&
          (curr_obu_type == OBU_MSDO ||
           curr_obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
           curr_obu_type == OBU_ATLAS_SEGMENT ||
           curr_obu_type == OBU_OPERATING_POINT_SET ||
           curr_obu_type == OBU_SEQUENCE_HEADER ||
#if CONFIG_CWG_F270_CI_OBU
           curr_obu_type == OBU_CONTENT_INTERPRETATION ||
#endif  // CONFIG_CWG_F270_CI_OBU
           curr_obu_type == OBU_MULTI_FRAME_HEADER ||
           is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
           || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
           )) {
    return 0;
  } else if ((prev_obu_type == OBU_MSDO) &&
             (curr_obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
              curr_obu_type == OBU_ATLAS_SEGMENT ||
              curr_obu_type == OBU_OPERATING_POINT_SET ||
              curr_obu_type == OBU_SEQUENCE_HEADER ||
#if CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_CONTENT_INTERPRETATION ||
#endif  // CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              )) {
    return 0;
  } else if ((prev_obu_type == OBU_LAYER_CONFIGURATION_RECORD) &&
             (curr_obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
              curr_obu_type == OBU_ATLAS_SEGMENT ||
              curr_obu_type == OBU_OPERATING_POINT_SET ||
              curr_obu_type == OBU_SEQUENCE_HEADER ||
#if CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_CONTENT_INTERPRETATION ||
#endif  // CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              )) {
    return 0;
  } else if ((prev_obu_type == OBU_OPERATING_POINT_SET) &&
             (curr_obu_type == OBU_OPERATING_POINT_SET ||
              curr_obu_type == OBU_ATLAS_SEGMENT ||
              curr_obu_type == OBU_SEQUENCE_HEADER ||
#if CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_CONTENT_INTERPRETATION ||
#endif  // CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              )) {
    return 0;
  } else if ((prev_obu_type == OBU_ATLAS_SEGMENT) &&
             (curr_obu_type == OBU_ATLAS_SEGMENT ||
              curr_obu_type == OBU_SEQUENCE_HEADER ||
#if CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_CONTENT_INTERPRETATION ||
#endif  // CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              )) {
    return 0;
  } else if ((prev_obu_type == OBU_SEQUENCE_HEADER) &&
             (curr_obu_type == OBU_SEQUENCE_HEADER ||
#if CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_CONTENT_INTERPRETATION ||
#endif  // CONFIG_CWG_F270_CI_OBU
              curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_BUFFER_REMOVAL_TIMING ||
              is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              )) {
    return 0;
#if CONFIG_CWG_F270_CI_OBU
  } else if ((prev_obu_type == OBU_CONTENT_INTERPRETATION) &&
             (curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              curr_obu_type == OBU_BUFFER_REMOVAL_TIMING ||
              is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              )) {
    return 0;
#endif  // CONFIG_CWG_F270_CI_OBU
  } else if ((prev_obu_type == OBU_BUFFER_REMOVAL_TIMING) &&
             (curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              )) {
    return 0;
  } else if ((prev_obu_type == OBU_MULTI_FRAME_HEADER) &&
             (curr_obu_type == OBU_MULTI_FRAME_HEADER ||
              is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || curr_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              )) {
    return 0;
  } else if ((prev_obu_type == OBU_METADATA
#if CONFIG_SHORT_METADATA
              || prev_obu_type == OBU_METADATA_GROUP
#endif  // CONFIG_SHORT_METADATA
              ) &&
             (is_coded_frame(curr_obu_type) || curr_obu_type == OBU_METADATA ||
#if CONFIG_SHORT_METADATA
              curr_obu_type == OBU_METADATA_GROUP ||
#endif  // CONFIG_SHORT_METADATA
              curr_obu_type == OBU_TEMPORAL_DELIMITER)) {
    return 0;
  } else if (prev_obu_type == OBU_TEMPORAL_DELIMITER ||
             is_coded_frame(prev_obu_type) || prev_obu_type == OBU_PADDING) {
    return 0;
  }
  return 1;
}
#endif  // OBU_ORDER_IN_TU

#if CONFIG_F024_KEYOBU
int av1_is_random_accessed_temporal_unit(const uint8_t *data, size_t data_sz) {
  const uint8_t *data_read = data;

  ObuHeader obu_header;
  memset(&obu_header, 0, sizeof(obu_header));
  while (data_read < data + data_sz) {
    size_t payload_size = 0;
    size_t bytes_read = 0;
    aom_read_obu_header_and_size(data_read, data_sz, &obu_header, &payload_size,
                                 &bytes_read);
    if (obu_header.type == OBU_CLK || obu_header.type == OBU_OLK) {
      return 1;
    }
    data_read += bytes_read + payload_size;
    data_sz -= bytes_read + payload_size;
  }
  return 0;
}

static int is_leading_vcl_obu(OBU_TYPE obu_type) {
  return (obu_type == OBU_LEADING_TILE_GROUP || obu_type == OBU_LEADING_SEF ||
          obu_type == OBU_LEADING_TIP);
}
#endif  // CONFIG_F024_KEYOBU
// On success, sets *p_data_end and returns a boolean that indicates whether
// the decoding of the current frame is finished. On failure, sets
// cm->error.error_code and returns -1.
int aom_decode_frame_from_obus(struct AV1Decoder *pbi, const uint8_t *data,
                               const uint8_t *data_end,
                               const uint8_t **p_data_end) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(pbi, aom_decode_frame_from_obus_time);
#endif
  AV1_COMMON *const cm = &pbi->common;
  int frame_decoding_finished = 0;
  ObuHeader obu_header;
  memset(&obu_header, 0, sizeof(obu_header));
  pbi->seen_frame_header = 0;
  pbi->next_start_tile = 0;
  pbi->num_tile_groups = 0;

  if (data_end < data) {
    cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return -1;
  }

#if OBU_ORDER_IN_TU
  OBU_TYPE prev_obu_type = 0;
  OBU_TYPE curr_obu_type = 0;
  int prev_obu_type_initialized = 0;
#endif  // OBU_ORDER_IN_TU

#if CONFIG_F255_QMOBU
  uint32_t acc_qm_id_bitmap = 0;
#endif
#if CONFIG_F153_FGM_OBU
  // acc_fgm_id_bitmap accumulates fgm_id_bitmap in FGM OBU to check if film
  // grain models signalled before a coded frame have the same fgm_id
  uint32_t acc_fgm_id_bitmap = 0;
  int fgm_seq_id_in_tu = -1;
#endif  // CONFIG_F153_FGM_OBU

  // decode frame as a series of OBUs
  while (!frame_decoding_finished && cm->error.error_code == AOM_CODEC_OK) {
    struct aom_read_bit_buffer rb;
    size_t payload_size = 0;
    size_t decoded_payload_size = 0;
    size_t obu_payload_offset = 0;
    size_t bytes_read = 0;
    const size_t bytes_available = data_end - data;

    if (bytes_available == 0 && !pbi->seen_frame_header) {
      cm->error.error_code = AOM_CODEC_OK;
      break;
    }

    aom_codec_err_t status = aom_read_obu_header_and_size(
        data, bytes_available, &obu_header, &payload_size, &bytes_read);

    if (status != AOM_CODEC_OK) {
      cm->error.error_code = status;
      return -1;
    }

#if CONFIG_F024_KEYOBU
    // Skip all obus till the random_accessed-th random access point
    // Remove all leading_vcl obus
    if (obu_header.type == OBU_LEADING_SEF ||
        obu_header.type == OBU_LEADING_TIP ||
        obu_header.type == OBU_LEADING_TILE_GROUP)
      cm->is_leading_picture = 1;
    else if (obu_header.type == OBU_REGULAR_SEF ||
             obu_header.type == OBU_REGULAR_TIP ||
             obu_header.type == OBU_REGULAR_TILE_GROUP)
      cm->is_leading_picture = 0;
    else
      cm->is_leading_picture = -1;
    if (obu_header.type == OBU_CLK || obu_header.type == OBU_OLK)
      pbi->random_access_point_count++;
    if (pbi->random_access_point_count < pbi->random_access_point_index) {
      pbi->random_accessed = 0;
      data += (bytes_read + payload_size);
      continue;
    } else {
      if (obu_header.type == OBU_OLK || obu_header.type == OBU_CLK)
        pbi->random_accessed = 1;
      if ((obu_header.type == OBU_OLK || obu_header.type == OBU_CLK) &&
          pbi->random_access_point_count != pbi->random_access_point_index)
        pbi->random_accessed = 0;
      if (pbi->random_accessed) {
        // drop all leading vcl obus
        if (is_leading_vcl_obu(obu_header.type)) {
          data += (bytes_read + payload_size);
          continue;
        }
      }
    }
#endif

#if OBU_ORDER_IN_TU
    curr_obu_type = obu_header.type;
    if (prev_obu_type_initialized &&
        check_obu_order(prev_obu_type, curr_obu_type)) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "OBU order is incorrect in TU previous %s current %s",
                         aom_obu_type_to_string(prev_obu_type),
                         aom_obu_type_to_string(curr_obu_type));
    }
    prev_obu_type = curr_obu_type;
    prev_obu_type_initialized = 1;
#endif  // OBU_ORDER_IN_TU

    if (obu_header.type == OBU_MSDO) {
      if (obu_header.obu_tlayer_id != 0)
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Incorrect tlayer_id for MSDO: %d",
                           obu_header.obu_tlayer_id);
      if (obu_header.obu_mlayer_id != 0)
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Incorrect obu_mlayer_id for MSDO: %d",
                           obu_header.obu_mlayer_id);
      if (obu_header.obu_xlayer_id != GLOBAL_XLAYER_ID)
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Incorrect obu_xlayer_id for MSDO: %d",
                           obu_header.obu_xlayer_id);
    }

    // Record obu size header information.
    pbi->obu_size_hdr.data = data + obu_header.size;
    pbi->obu_size_hdr.size = bytes_read - obu_header.size;
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    pbi->obu_type = obu_header.type;
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME

    // Note: aom_read_obu_header_and_size() takes care of checking that this
    // doesn't cause 'data' to advance past 'data_end'.
    data += bytes_read;

    if ((size_t)(data_end - data) < payload_size) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    cm->tlayer_id = obu_header.obu_tlayer_id;
    cm->mlayer_id = obu_header.obu_mlayer_id;
    cm->xlayer_id = obu_header.obu_xlayer_id;

    // check bitstream conformance if sequence header is parsed
    if (pbi->sequence_header_ready) {
      // bitstream constraint for tlayer_id
      if (cm->tlayer_id > cm->seq_params.max_tlayer_id) {
        aom_internal_error(
            &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
            "Inconsistent tlayer_id information: OBU header indicates "
            "tlayer_id is "
            "%d, yet max_tlayer_id in the sequence header is %d.",
            cm->tlayer_id, cm->seq_params.max_tlayer_id);
      }
      // bitstream constraint for mlayer_id
      if (cm->mlayer_id > cm->seq_params.max_mlayer_id) {
        aom_internal_error(
            &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
            "Inconsistent mlayer_id information: OBU header indicates "
            "mlayer_id is "
            "%d, yet max_mlayer_id in the sequence header is %d.",
            cm->mlayer_id, cm->seq_params.max_mlayer_id);
      }
    }

#if CONFIG_CWG_F317
    // Set is_bridge_frame flag based on OBU type
    if (obu_header.type == OBU_BRIDGE_FRAME) {
      cm->bridge_frame_info.is_bridge_frame = 1;
    } else {
      cm->bridge_frame_info.is_bridge_frame = 0;
    }
#endif  // CONFIG_CWG_F317

    if (obu_header.type != OBU_TEMPORAL_DELIMITER &&
        obu_header.type != OBU_SEQUENCE_HEADER) {
      // don't decode obu if it's not in current operating mode
      if (!is_obu_in_current_operating_point(pbi, obu_header)) {
        data += payload_size;
        continue;
      }
    }

    av1_init_read_bit_buffer(pbi, &rb, data, data + payload_size);
    switch (obu_header.type) {
      case OBU_TEMPORAL_DELIMITER:
        decoded_payload_size = read_temporal_delimiter_obu();
        pbi->seen_frame_header = 0;
        pbi->next_start_tile = 0;
        break;
      case OBU_MSDO:
        decoded_payload_size =
            read_multi_stream_decoder_operation_obu(pbi, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
      case OBU_SEQUENCE_HEADER:
        decoded_payload_size = read_sequence_header_obu(pbi, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
#if CONFIG_F153_FGM_OBU
        fgm_seq_id_in_tu =
            pbi->seq_list[pbi->seq_header_count - 1].seq_header_id;
#endif
        // The sequence header should not change in the middle of a frame.
        if (pbi->sequence_header_changed && pbi->seen_frame_header) {
          cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        break;
      case OBU_BUFFER_REMOVAL_TIMING:
        decoded_payload_size =
            av1_read_buffer_removal_timing_obu(pbi, &rb, cm->xlayer_id);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
      case OBU_LAYER_CONFIGURATION_RECORD:
        decoded_payload_size =
            av1_read_layer_configuration_record_obu(pbi, cm->xlayer_id, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
      case OBU_ATLAS_SEGMENT:
        decoded_payload_size =
            av1_read_atlas_segment_info_obu(pbi, cm->xlayer_id, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
      case OBU_OPERATING_POINT_SET:
        decoded_payload_size =
            av1_read_operating_point_set_obu(pbi, cm->xlayer_id, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
#if CONFIG_CWG_F270_CI_OBU
      case OBU_CONTENT_INTERPRETATION:
        if (!pbi->sequence_header_ready) {
          cm->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
          return -1;
        }
        decoded_payload_size = av1_read_content_interpretation_obu(pbi, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
#endif  // CONFIG_CWG_F270_CI_OBU
#if CONFIG_MULTI_FRAME_HEADER
      case OBU_MULTI_FRAME_HEADER:
        decoded_payload_size = read_multi_frame_header_obu(pbi, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
#endif  // CONFIG_MULTI_FRAME_HEADER
#if CONFIG_F024_KEYOBU
      case OBU_CLK:
      case OBU_OLK:
      case OBU_LEADING_TILE_GROUP:
      case OBU_REGULAR_TILE_GROUP:
#else
      case OBU_TILE_GROUP:
#endif  // CONFIG_F024_KEYOBU
      case OBU_SWITCH:
#if CONFIG_F024_KEYOBU
      case OBU_LEADING_SEF:
      case OBU_REGULAR_SEF:
#else
      case OBU_SEF:
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_F024_KEYOBU
      case OBU_LEADING_TIP:
      case OBU_REGULAR_TIP:
#else
      case OBU_TIP:
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
      case OBU_RAS_FRAME:
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_CWG_F317
      case OBU_BRIDGE_FRAME:
#endif  // CONFIG_CWG_F317
#if CONFIG_F255_QMOBU
        for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
          if (acc_qm_id_bitmap & (1 << i)) {
            pbi->qm_protected[i] &=
                (obu_header.type == OBU_CLK || obu_header.type == OBU_OLK);
          }
        }
        // It is a requirement that if multiple QM OBUs are present
        // consecutively prior to a coded frame, other than a QM OBU with
        // qm_bit_map equal to 0, such QM OBUs will not set the same QM ID more
        // than once.
        acc_qm_id_bitmap = 0;
#endif
#if CONFIG_F153_FGM_OBU
        // It is a requirement that if multiple FGM OBUs are present
        // consecutively prior to a coded frame, such FGM OBUs will not set
        // the same FGM ID more than once.
        acc_fgm_id_bitmap = 0;
#endif  // CONFIG_F153_FGM_OBU
        decoded_payload_size =
            read_tilegroup_obu(pbi, &rb, data, data + payload_size, p_data_end,
                               obu_header.type, &frame_decoding_finished);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
#if CONFIG_CWG_F317
        if (cm->bru.frame_inactive_flag ||
            cm->bridge_frame_info.is_bridge_frame) {
#else
        if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
          pbi->seen_frame_header = 0;
          frame_decoding_finished = 1;
          CommonTileParams *const tiles = &cm->tiles;
          av1_get_tile_limits(&cm->tiles, cm->mi_params.mi_rows,
                              cm->mi_params.mi_cols, cm->mib_size_log2,
                              cm->seq_params.mib_size_log2);
          tiles->uniform_spacing = 1;
          tiles->log2_cols = 0;
          av1_calculate_tile_cols(tiles);
          tiles->log2_rows = 0;
          av1_calculate_tile_rows(tiles);
          const int num_tiles = cm->tiles.cols * cm->tiles.rows;
          const int end_tile = num_tiles - 1;
          // skip parsing and go directly to decode
          av1_decode_tg_tiles_and_wrapup(pbi, data, data_end, p_data_end, 0,
                                         end_tile, 0);
#if CONFIG_CWG_F317
          if (cm->bridge_frame_info.is_bridge_frame) {
            *p_data_end = data + payload_size;
          }
#endif  // CONFIG_CWG_F317
          break;
        }
        if (obu_payload_offset > payload_size) {
          cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        if (frame_decoding_finished) pbi->seen_frame_header = 0;
        pbi->num_tile_groups++;
        break;
#if CONFIG_F255_QMOBU
      case OBU_QM:
        decoded_payload_size =
            read_qm_obu(pbi, obu_header.obu_tlayer_id, obu_header.obu_mlayer_id,
                        &acc_qm_id_bitmap, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
#endif  // CONFIG_F255_QMOBU
      case OBU_METADATA:
#if CONFIG_METADATA && !CONFIG_SHORT_METADATA
        decoded_payload_size =
            read_metadata_obu(pbi, data, payload_size, &obu_header);
#elif CONFIG_SHORT_METADATA
        decoded_payload_size = read_metadata_short(pbi, data, payload_size);
#else
      decoded_payload_size = read_metadata(pbi, data, payload_size);
#endif  // CONFIG_METADATA && !CONFIG_SHORT_METADATA
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
#if CONFIG_SHORT_METADATA
      case OBU_METADATA_GROUP:
        decoded_payload_size =
#if CONFIG_METADATA
            read_metadata_obu(pbi, data, payload_size, &obu_header);
#else
            read_metadata_short(pbi, data, payload_size);
#endif  // CONFIG_METADATA
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
#endif  // CONFIG_SHORT_METADATA
#if CONFIG_F153_FGM_OBU
      case OBU_FGM:
        decoded_payload_size = read_fgm_obu(
            pbi, obu_header.obu_tlayer_id, obu_header.obu_mlayer_id,
            &acc_fgm_id_bitmap, fgm_seq_id_in_tu, &rb);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
#endif  // CONFIG_F153_FGM_OBU
      case OBU_PADDING:
        decoded_payload_size = read_padding(cm, data, payload_size);
        if (cm->error.error_code != AOM_CODEC_OK) return -1;
        break;
      default:
        // Skip unrecognized OBUs
        if (payload_size > 0 &&
            get_last_nonzero_byte(data, payload_size) == 0) {
          cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        decoded_payload_size = payload_size;
        break;
    }

    // Check that the signalled OBU size matches the actual amount of data read
    if (decoded_payload_size > payload_size) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    // If there are extra padding bytes, they should all be zero
    while (decoded_payload_size < payload_size) {
      uint8_t padding_byte = data[decoded_payload_size++];
      if (padding_byte != 0) {
        cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
        return -1;
      }
    }

    data += payload_size;
  }

#if CONFIG_METADATA
  // check whether suffix metadata OBUs are present
  while (cm->error.error_code == AOM_CODEC_OK && data < data_end) {
    size_t payload_size = 0;
    size_t decoded_payload_size = 0;
    size_t bytes_read = 0;
    const size_t bytes_available = data_end - data;
    aom_codec_err_t status = aom_read_obu_header_and_size(
        data, bytes_available, &obu_header, &payload_size, &bytes_read);

    if (status != AOM_CODEC_OK) {
      cm->error.error_code = status;
      return -1;
    }
#if CONFIG_SHORT_METADATA
    if (obu_header.type != OBU_METADATA_GROUP || data + bytes_read >= data_end)
#else
    if (obu_header.type != OBU_METADATA || data + bytes_read >= data_end)
#endif  // CONFIG_SHORT_METADATA
      break;

    // check whether it is a suffix metadata OBU
    if (!(data[bytes_read] & 0x80)) break;

    data += bytes_read;

    decoded_payload_size =
        read_metadata_obu(pbi, data, payload_size, &obu_header);

    if (cm->error.error_code != AOM_CODEC_OK) return -1;

    // Check that the signalled OBU size matches the actual amount of data read
    if (decoded_payload_size > payload_size) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    data += payload_size;
  }
#endif  // CONFIG_METADATA

  if (cm->error.error_code != AOM_CODEC_OK) return -1;

  *p_data_end = data;

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(pbi, aom_decode_frame_from_obus_time);

  // Print out timing information.
  int i;
  fprintf(stderr, "\n Frame number: %d, Frame type: %s, Show Frame: %d\n",
          cm->current_frame.frame_number,
          get_frame_type_enum(cm->current_frame.frame_type), cm->show_frame);
  for (i = 0; i < kTimingComponents; i++) {
    pbi->component_time[i] += pbi->frame_component_time[i];
    fprintf(stderr, " %s:  %" PRId64 " us (total: %" PRId64 " us)\n",
            get_component_name(i), pbi->frame_component_time[i],
            pbi->component_time[i]);
    pbi->frame_component_time[i] = 0;
  }
#endif

  return frame_decoding_finished;
}
