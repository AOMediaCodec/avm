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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "avm/avm_image.h"
#include "avm/avm_integer.h"
#include "avm_dsp/bitreader_buffer.h"
#include "avm_dsp/bitwriter_buffer.h"
#include "av2/common/obu_util.h"
#include "common/av2_config.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/enums.h"

// Helper macros to reduce verbosity required to check for read errors.
//
// Note that when using these macros, even single line if statements should use
// curly braces to avoid unexpected behavior because all but the
// AV2C_POP_ERROR_HANDLER_DATA() macro consist of multiple statements.
#define AV2C_READ_BIT_OR_RETURN_ERROR(field)                                   \
  int field = 0;                                                               \
  do {                                                                         \
    field = avm_rb_read_bit(reader);                                           \
    if (result == -1) {                                                        \
      fprintf(stderr,                                                          \
              "av2c: Error reading bit for " #field ", value=%d result=%d.\n", \
              field, result);                                                  \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define AV2C_READ_BITS_OR_RETURN_ERROR(field, length) \
  int field = 0;                                      \
  do {                                                \
    field = avm_rb_read_literal(reader, (length));    \
    if (result == -1) {                               \
      fprintf(stderr,                                 \
              "av2c: Could not read bits for " #field \
              ", value=%d result=%d.\n",              \
              field, result);                         \
      return -1;                                      \
    }                                                 \
  } while (0)

// Helper macros for setting/restoring the error handler data in
// avm_read_bit_buffer.
#define AV2C_PUSH_ERROR_HANDLER_DATA(new_data)                \
  void *original_error_handler_data = NULL;                   \
  do {                                                        \
    original_error_handler_data = reader->error_handler_data; \
    reader->error_handler_data = &new_data;                   \
  } while (0)

#define AV2C_POP_ERROR_HANDLER_DATA()                         \
  do {                                                        \
    reader->error_handler_data = original_error_handler_data; \
  } while (0)

#define AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(field)                             \
  uint32_t field = 0;                                                          \
  do {                                                                         \
    field = avm_rb_read_uvlc(reader);                                          \
    if (result == -1) {                                                        \
      fprintf(stderr,                                                          \
              "av2c: Error reading bit for " #field ", value=%u result=%d.\n", \
              field, result);                                                  \
      return -1;                                                               \
    }                                                                          \
  } while (0)

static const size_t kAv2cSize = 4;

static void bitreader_error_handler(void *data, avm_codec_err_t error,
                                    const char *detail) {
  int *error_val = (int *)data;
  (void)error;
  (void)detail;
  *error_val = -1;
}

#if !CONFIG_CWG_F270_OPS
// Parse the AV2 timing_info() structure:
// timing_info( ) {
//   num_units_in_display_tick       f(32)
//   time_scale                      f(32)
#if CONFIG_CWG_F270_CI_OBU
//   equal_elemental_interval          f(1)
//   if (equal_elemental_interval)
#else
//   equal_picture_interval          f(1)
//   if (equal_picture_interval)
#endif  // CONFIG_CWG_F270_CI_OBU
//     num_ticks_per_picture_minus_1 uvlc()
//   }
static int parse_timing_info(struct avm_read_bit_buffer *reader) {
  int result = 0;
  AV2C_PUSH_ERROR_HANDLER_DATA(result);

  AV2C_READ_BITS_OR_RETURN_ERROR(num_units_in_display_tick, 32);
  AV2C_READ_BITS_OR_RETURN_ERROR(time_scale, 32);

#if CONFIG_CWG_F270_CI_OBU
  AV2C_READ_BIT_OR_RETURN_ERROR(equal_elemental_interval);
  if (equal_elemental_interval) {
#else
  AV2C_READ_BIT_OR_RETURN_ERROR(equal_picture_interval);
  if (equal_picture_interval) {
#endif  // CONFIG_CWG_F270_CI_OBU
    uint32_t num_ticks_per_picture_minus_1 = avm_rb_read_uvlc(reader);
    if (result == -1) {
      fprintf(stderr,
              "av2c: Could not read bits for "
              "num_ticks_per_picture_minus_1, value=%u.\n",
              num_ticks_per_picture_minus_1);
      return result;
    }
    if (num_ticks_per_picture_minus_1 == UINT32_MAX) {
      fprintf(stderr,
              "av2c: num_ticks_per_picture_minus_1 cannot be (1 << 32) − 1.\n");
      return -1;
    }
  }

  AV2C_POP_ERROR_HANDLER_DATA();
  return result;
}

// Parse the AV2 decoder_model_info() structure:
// decoder_model_info( ) {
//   buffer_delay_length_minus_1            f(5)
//   num_units_in_decoding_tick             f(32)
// }
//
// Returns -1 upon failure, or the value of buffer_delay_length_minus_1 + 1.
static int parse_decoder_model_info(struct avm_read_bit_buffer *reader) {
  int result = 0;
  AV2C_PUSH_ERROR_HANDLER_DATA(result);

  AV2C_READ_BITS_OR_RETURN_ERROR(buffer_delay_length_minus_1, 5);
  AV2C_READ_BITS_OR_RETURN_ERROR(num_units_in_decoding_tick, 32);

  AV2C_POP_ERROR_HANDLER_DATA();
  return buffer_delay_length_minus_1 + 1;
}

// Parse the AV2 operating_parameters_info() structure:
// operating_parameters_info( op ) {
//   n = buffer_delay_length_minus_1 + 1
//   decoder_buffer_delay[ op ] f(n)
//   encoder_buffer_delay[ op ] f(n)
//   low_delay_mode_flag[ op ] f(1)
// }
static int parse_operating_parameters_info(struct avm_read_bit_buffer *reader,
                                           int buffer_delay_length_minus_1) {
  int result = 0;
  AV2C_PUSH_ERROR_HANDLER_DATA(result);

  const int buffer_delay_length = buffer_delay_length_minus_1 + 1;
  AV2C_READ_BITS_OR_RETURN_ERROR(decoder_buffer_delay, buffer_delay_length);
  AV2C_READ_BITS_OR_RETURN_ERROR(encoder_buffer_delay, buffer_delay_length);
  AV2C_READ_BIT_OR_RETURN_ERROR(low_delay_mode_flag);

  AV2C_POP_ERROR_HANDLER_DATA();
  return result;
}
#endif  // !CONFIG_CWG_F270_OPS

#if CONFIG_CWG_E242_BITDEPTH
static int get_bitdepth(int bitdepth_lut_idx) {
  int bitdepth = -1;
  switch (bitdepth_lut_idx) {
    case AVM_BITDEPTH_0: bitdepth = AVM_BITS_10; break;
    case AVM_BITDEPTH_1: bitdepth = AVM_BITS_8; break;
    case AVM_BITDEPTH_2: bitdepth = AVM_BITS_12; break;
    default: break;
  }
  return bitdepth;
}
#endif  // CONFIG_CWG_E242_BITDEPTH

#if CONFIG_CWG_F270_CI_OBU
// Parse the chroma format and bitdepth in the sequence header.
static int parse_chroma_format_bitdepth(struct avm_read_bit_buffer *reader,
                                        Av2Config *config) {
#else
// Parse the AV2 color_config() structure..See:
// https://aomediacodec.github.io/av2-spec/av2-spec.pdf#page=44
static int parse_color_config(struct avm_read_bit_buffer *reader,
                              Av2Config *config) {
#endif  // CONFIG_CWG_F270_CI_OBU
  int result = 0;
  AV2C_PUSH_ERROR_HANDLER_DATA(result);

#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(chroma_format_idc);
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

#if CONFIG_CWG_E242_BITDEPTH
  AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(bitdepth_idx);
  if (bitdepth_idx > UINT8_MAX) {
    fprintf(stderr, "av2c: invalid value for bitdepth_idx: %u.\n",
            bitdepth_idx);
    return -1;
  }
  config->bitdepth_idx = (uint8_t)bitdepth_idx;
  int bit_depth = get_bitdepth((int)config->bitdepth_idx);
  if (bit_depth < 0) {
    fprintf(stderr, "av2c: invalid value for bitdepth_idx: %u.\n",
            bitdepth_idx);
    return -1;
  }
#else
  AV2C_READ_BIT_OR_RETURN_ERROR(high_bitdepth);
  config->high_bitdepth = high_bitdepth;

  int bit_depth = 0;
  if (config->seq_profile == 2 && config->high_bitdepth) {
    AV2C_READ_BIT_OR_RETURN_ERROR(twelve_bit);
    config->twelve_bit = twelve_bit;
    bit_depth = config->twelve_bit ? 12 : 10;
  } else {
    bit_depth = config->high_bitdepth ? 10 : 8;
  }
#endif  // CONFIG_CWG_E242_BITDEPTH

#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  (void)bit_depth;
  assert(bit_depth == 8 || bit_depth == 10 || bit_depth == 12);
  config->monochrome = (chroma_format_idc == CHROMA_FORMAT_400);
#else
  if (config->seq_profile != 1) {
    AV2C_READ_BIT_OR_RETURN_ERROR(mono_chrome);
    config->monochrome = mono_chrome;
  }
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

  int color_primaries = AVM_CICP_CP_UNSPECIFIED;
  int transfer_characteristics = AVM_CICP_TC_UNSPECIFIED;
  int matrix_coefficients = AVM_CICP_MC_UNSPECIFIED;

  AV2C_READ_BIT_OR_RETURN_ERROR(color_description_present_flag);
  if (color_description_present_flag) {
    AV2C_READ_BITS_OR_RETURN_ERROR(color_primaries_val, 8);
    color_primaries = color_primaries_val;
    AV2C_READ_BITS_OR_RETURN_ERROR(transfer_characteristics_val, 8);
    transfer_characteristics = transfer_characteristics_val;
    AV2C_READ_BITS_OR_RETURN_ERROR(matrix_coefficients_val, 8);
    matrix_coefficients = matrix_coefficients_val;
  }

  if (config->monochrome) {
    AV2C_READ_BIT_OR_RETURN_ERROR(color_range);
    config->chroma_subsampling_x = 1;
    config->chroma_subsampling_y = 1;
  } else if (color_primaries == AVM_CICP_CP_BT_709 &&
             transfer_characteristics == AVM_CICP_TC_SRGB &&
             matrix_coefficients == AVM_CICP_MC_IDENTITY) {
    config->chroma_subsampling_x = 0;
    config->chroma_subsampling_y = 0;
  } else {
    AV2C_READ_BIT_OR_RETURN_ERROR(color_range);
#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
    int subsampling_x;
    int subsampling_y;
    avm_codec_err_t err = av2_get_chroma_subsampling(
        chroma_format_idc, &subsampling_x, &subsampling_y);
    if (err != AVM_CODEC_OK) {
      fprintf(stderr, "chroma format idc %d not supported.\n",
              chroma_format_idc);
      return -1;
    }
    config->chroma_subsampling_x = (uint8_t)subsampling_x;
    config->chroma_subsampling_y = (uint8_t)subsampling_y;
#else
    if (config->seq_profile == 0) {
      config->chroma_subsampling_x = 1;
      config->chroma_subsampling_y = 1;
    } else if (config->seq_profile == 1) {
      config->chroma_subsampling_x = 0;
      config->chroma_subsampling_y = 0;
    } else {
      if (bit_depth == 12) {
        AV2C_READ_BIT_OR_RETURN_ERROR(subsampling_x);
        config->chroma_subsampling_x = subsampling_x;
        if (subsampling_x) {
          AV2C_READ_BIT_OR_RETURN_ERROR(subsampling_y);
          config->chroma_subsampling_y = subsampling_y;
        } else {
          config->chroma_subsampling_y = 0;
        }
      } else {
        config->chroma_subsampling_x = 1;
        config->chroma_subsampling_y = 0;
      }
    }
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

#if !CONFIG_CWG_F270_CI_OBU
    if (config->chroma_subsampling_x && !config->chroma_subsampling_y) {
      // YUV 4:2:2
      config->chroma_sample_position = AVM_CSP_UNSPECIFIED;
      AV2C_READ_BIT_OR_RETURN_ERROR(csp_present_flag);
      if (csp_present_flag) {
        AV2C_READ_BIT_OR_RETURN_ERROR(chroma_sample_position);
        config->chroma_sample_position = chroma_sample_position;
      }
    } else if (config->chroma_subsampling_x && config->chroma_subsampling_y) {
      // YUV 4:2:0
      config->chroma_sample_position = AVM_CSP_UNSPECIFIED;
      AV2C_READ_BIT_OR_RETURN_ERROR(csp_present_flag);
      if (csp_present_flag) {
        AV2C_READ_BITS_OR_RETURN_ERROR(chroma_sample_position, 3);
        if (chroma_sample_position > AVM_CSP_BOTTOM) {
          fprintf(stderr, "av2c: Invalid chroma_sample_position, value=%d.\n",
                  chroma_sample_position);
          return -1;
        }
        config->chroma_sample_position = chroma_sample_position;
      }
    }
#endif  // !CONFIG_CWG_F270_CI_OBU
  }

  AV2C_POP_ERROR_HANDLER_DATA();
  return result;
}

// Parse AV2 Sequence Header OBU. See:
// https://aomediacodec.github.io/av2-spec/av2-spec.pdf#page=41
static int parse_sequence_header(const uint8_t *const buffer, size_t length,
                                 Av2Config *config) {
  int result = 0;
  // The reader instance is local to this function, but a pointer to the
  // reader instance is used within this function and throughout this file to
  // allow use of the helper macros that reduce parse error checking verbosity.
  struct avm_read_bit_buffer reader_instance = { buffer, buffer + length, 0,
                                                 &result,
                                                 bitreader_error_handler };
  struct avm_read_bit_buffer *reader = &reader_instance;

#if CONFIG_CWG_E242_SEQ_HDR_ID
  uint32_t seq_header_id = avm_rb_read_uvlc(reader);
  if (seq_header_id >= MAX_SEQ_NUM) {
    fprintf(stderr, "av2c: unsupported seq_header_id.\n");
    return -1;
  }
  config->seq_header_id = seq_header_id;
#endif  // CONFIG_CWG_E242_SEQ_HDR_ID

  AV2C_READ_BITS_OR_RETURN_ERROR(seq_profile, 3);
  config->seq_profile = seq_profile;
  AV2C_READ_BIT_OR_RETURN_ERROR(single_picture_header_flag);
  if (!single_picture_header_flag) {
    AV2C_READ_BITS_OR_RETURN_ERROR(seq_lcr_id, 3);
    config->seq_lcr_id = seq_lcr_id;
    AV2C_READ_BIT_OR_RETURN_ERROR(still_picture);
  }
  AV2C_READ_BITS_OR_RETURN_ERROR(seq_level_idx_0, 5);
  config->seq_level_idx_0 = seq_level_idx_0;
  if (seq_level_idx_0 > 7 && !single_picture_header_flag) {
    AV2C_READ_BIT_OR_RETURN_ERROR(single_tier_0);
    config->seq_tier_0 = 0;
  }

  AV2C_READ_BITS_OR_RETURN_ERROR(frame_width_bits_minus_1, 4);
  AV2C_READ_BITS_OR_RETURN_ERROR(frame_height_bits_minus_1, 4);
  AV2C_READ_BITS_OR_RETURN_ERROR(max_frame_width_minus_1,
                                 frame_width_bits_minus_1 + 1);
  AV2C_READ_BITS_OR_RETURN_ERROR(max_frame_height_minus_1,
                                 frame_height_bits_minus_1 + 1);

  AV2C_READ_BIT_OR_RETURN_ERROR(conf_window_flag);
  config->conf_win_enabled_flag = conf_window_flag;
  if (config->conf_win_enabled_flag) {
    config->conf_win_left_offset = avm_rb_read_uvlc(reader);
    config->conf_win_right_offset = avm_rb_read_uvlc(reader);
    config->conf_win_top_offset = avm_rb_read_uvlc(reader);
    config->conf_win_bottom_offset = avm_rb_read_uvlc(reader);
  } else {
    config->conf_win_enabled_flag = 0;
    config->conf_win_left_offset = 0;
    config->conf_win_right_offset = 0;
    config->conf_win_top_offset = 0;
    config->conf_win_bottom_offset = 0;
  }

#if CONFIG_CWG_F270_CI_OBU
  if (parse_chroma_format_bitdepth(reader, config) != 0) {
    fprintf(stderr, "Chroma format or bitdepth parsing failed.\n");
    return -1;
  }
#else
  if (parse_color_config(reader, config) != 0) {
    fprintf(stderr, "av2c: color_config() parse failed.\n");
    return -1;
  }
#endif  // CONFIG_CWG_F270_CI_OBU

  if (single_picture_header_flag) {
    config->initial_presentation_delay_present = 0;
  } else {
#if CONFIG_CWG_F270_OPS
    AV2C_READ_BIT_OR_RETURN_ERROR(max_display_model_info_present_flag);
    int seq_max_display_model_info_present_flag =
        max_display_model_info_present_flag;
    if (seq_max_display_model_info_present_flag) {
      AV2C_READ_BITS_OR_RETURN_ERROR(max_initial_display_delay_minus_1, 4);
      int seq_max_initial_display_delay_minus_1 =
          max_initial_display_delay_minus_1;
      printf("seq_max_initial_display_delay_minus_1 %d\n",
             seq_max_initial_display_delay_minus_1);
    }
    AV2C_READ_BIT_OR_RETURN_ERROR(decoder_model_present_flag);
    int decoder_model_info_present_flag = decoder_model_present_flag;
    if (decoder_model_info_present_flag) {
      AV2C_READ_BITS_OR_RETURN_ERROR(num_units_in_decoding_tick, 32);
      AV2C_READ_BIT_OR_RETURN_ERROR(max_decoder_model_present_flag);
      int max_decoder_model_info_present_flag = max_decoder_model_present_flag;
      if (max_decoder_model_info_present_flag) {
        int seq_max_decoder_buffer_delay = avm_rb_read_uvlc(reader);
        int seq_max_encoder_buffer_delay = avm_rb_read_uvlc(reader);
        int seq_max_low_delay_mode_flag = avm_rb_read_uvlc(reader);
        if ((seq_max_decoder_buffer_delay < 0) ||
            (seq_max_encoder_buffer_delay < 0) ||
            (seq_max_low_delay_mode_flag < 0)) {
          fprintf(stderr, "Unsupported values.\n");
          return -1;
        }
      }
    }
#else
    int has_decoder_model = 0;
    int buffer_delay_length = 0;

#if !CONFIG_CWG_F270_CI_OBU
    AV2C_READ_BIT_OR_RETURN_ERROR(timing_info_present_flag);
    if (timing_info_present_flag) {
      if (parse_timing_info(reader) != 0) return -1;
#endif  // !CONFIG_CWG_F270_CI_OBU

      AV2C_READ_BIT_OR_RETURN_ERROR(decoder_model_info_present_flag);
      if (decoder_model_info_present_flag &&
          (buffer_delay_length = parse_decoder_model_info(reader)) == -1) {
        return -1;
      }
      has_decoder_model = 1;
#if !CONFIG_CWG_F270_CI_OBU
    }
#endif  //  !CONFIG_CWG_F270_CI_OBU

    AV2C_READ_BIT_OR_RETURN_ERROR(initial_presentation_delay_present);
    config->initial_presentation_delay_present =
        initial_presentation_delay_present;

    AV2C_READ_BITS_OR_RETURN_ERROR(operating_points_cnt_minus_1, 5);
    const int num_operating_points = operating_points_cnt_minus_1 + 1;

    for (int op_index = 0; op_index < num_operating_points; ++op_index) {
      AV2C_READ_BITS_OR_RETURN_ERROR(operating_point_idc, 12);
      if (has_decoder_model) {
        AV2C_READ_BIT_OR_RETURN_ERROR(decoder_model_present_for_op);
        if (decoder_model_present_for_op) {
          if (parse_operating_parameters_info(reader, buffer_delay_length) ==
              -1) {
            return -1;
          }
        }
      }

      if (config->initial_presentation_delay_present) {
        // Skip the initial presentation delay bits if present since this
        // function has no access to the data required to properly set the
        // field.
        AV2C_READ_BIT_OR_RETURN_ERROR(
            initial_presentation_delay_present_for_this_op);
        if (initial_presentation_delay_present_for_this_op) {
          AV2C_READ_BITS_OR_RETURN_ERROR(initial_presentation_delay_minus_1, 4);
        }
      }

      if (op_index == 0) {
        // Av2Config needs only the values from the first operating point.
        config->initial_presentation_delay_present = 0;
        config->initial_presentation_delay_minus_one = 0;
      }
    }
#endif  // CONFIG_CWG_F270_OPS
  }

  return 0;
}

#if CONFIG_CWG_F270_CI_OBU
// Parse Content Interpretation OBU and populare config with CI params
static int parse_content_intrepretation_obu(const uint8_t *const buffer,
                                            size_t length, Av2Config *config) {
  int result = 0;
  struct avm_read_bit_buffer reader_instance = { buffer, buffer + length, 0,
                                                 &result,
                                                 bitreader_error_handler };

  struct avm_read_bit_buffer *reader = &reader_instance;

  // Read CI header fields
  AV2C_READ_BITS_OR_RETURN_ERROR(ci_scan_type_idc, 2);
  config->ci_scan_type_idc = ci_scan_type_idc;

  AV2C_READ_BIT_OR_RETURN_ERROR(ci_color_description_present_flag);
  config->ci_color_description_present_flag = ci_color_description_present_flag;

  AV2C_READ_BIT_OR_RETURN_ERROR(ci_chroma_sample_position_present_flag);
  config->ci_chroma_sample_position_present_flag =
      ci_chroma_sample_position_present_flag;

  AV2C_READ_BIT_OR_RETURN_ERROR(ci_aspect_ratio_info_present_flag);
  config->ci_aspect_ratio_info_present_flag = ci_aspect_ratio_info_present_flag;

  AV2C_READ_BIT_OR_RETURN_ERROR(ci_timing_info_present_flag);
  config->ci_timing_info_present_flag = ci_timing_info_present_flag;

  AV2C_READ_BIT_OR_RETURN_ERROR(ci_extension_present_flag);
  (void)ci_extension_present_flag;  // Not stored in config

  AV2C_READ_BIT_OR_RETURN_ERROR(reserved_bit);
  (void)reserved_bit;  // Reserved bit

  // Parse color information if present
  if (ci_color_description_present_flag) {
    AV2C_READ_BITS_OR_RETURN_ERROR(color_description_idc, 2);
    // Rice-Golomb with k=3, but for simplicity we read as literal
    // In actual implementation, use avm_rb_read_rice_golomb(reader, 2)

    if (color_description_idc == 0) {
      AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(color_primaries);
      config->ci_color_primaries = color_primaries;

      AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(transfer_characteristics);
      config->ci_transfer_characteristics = transfer_characteristics;

      AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(matrix_coefficients);
      config->ci_matrix_coefficients = matrix_coefficients;

    } else {
      config->ci_color_primaries = 2;           // AVM_CICP_CP_UNSPECIFIED
      config->ci_transfer_characteristics = 2;  // AVM_CICP_TC_UNSPECIFIED
      config->ci_matrix_coefficients = 2;       // AVM_CICP_MC_UNSPECIFIED
    }

    AV2C_READ_BIT_OR_RETURN_ERROR(full_range_flag);
    config->ci_full_range_flag = full_range_flag;
  } else {
    config->ci_color_primaries = 2;           // AVM_CICP_CP_UNSPECIFIED
    config->ci_transfer_characteristics = 2;  // AVM_CICP_TC_UNSPECIFIED
    config->ci_matrix_coefficients = 2;       // AVM_CICP_MC_UNSPECIFIED
    config->ci_full_range_flag = 0;
  }

  // Parse chroma sample position if present
  if (ci_chroma_sample_position_present_flag) {
    AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(ci_chroma_sample_position_0);
    config->chroma_sample_position = ci_chroma_sample_position_0;

    if (ci_scan_type_idc != 1) {
      AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(ci_chroma_sample_position_1);
      config->ci_chroma_sample_position_1 = ci_chroma_sample_position_1;
    } else {
      config->ci_chroma_sample_position_1 = ci_chroma_sample_position_0;
    }
  } else {
    config->chroma_sample_position = 0;       // AVM_CSP_UNSPECIFIED
    config->ci_chroma_sample_position_1 = 0;  // AVM_CSP_UNSPECIFIED
  }

  // Parse sample aspect ratio if present
  if (ci_aspect_ratio_info_present_flag) {
    AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(sar_aspect_ratio_idc);
    config->ci_sar_aspect_ratio_idc = sar_aspect_ratio_idc;

    if (sar_aspect_ratio_idc == 255) {  // AVM_SAR_IDC_255
      AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(sar_width);
      config->ci_sar_width = sar_width;

      AV2C_READ_UVLC_BITS_OR_RETURN_ERROR(sar_height);
      config->ci_sar_height = sar_height;
    }
  } else {
    config->ci_sar_aspect_ratio_idc = 0;
    config->ci_sar_width = 0;
    config->ci_sar_height = 0;
  }

  // Skip timing info parsing for now (complex structure)
  // Skip extension parsing (not yet defined)
  config->ci_present = 1;
  return 0;
}
#endif  // CONFIG_CWG_F270_CI_OBU

int get_av2config_from_obu(const uint8_t *buffer, size_t length,
                           Av2Config *config) {
  if (!buffer || length == 0 || !config) {
    return -1;
  }

  ObuHeader obu_header;
  memset(&obu_header, 0, sizeof(obu_header));

  size_t sequence_header_length = 0;
  size_t obu_header_length = 0;
  if (avm_read_obu_header_and_size(buffer, length, &obu_header,
                                   &sequence_header_length,
                                   &obu_header_length) != AVM_CODEC_OK ||
      obu_header.type != OBU_SEQUENCE_HEADER ||
      sequence_header_length + obu_header_length > length) {
    return -1;
  }

  memset(config, 0, sizeof(*config));
  config->marker = 1;
  config->version = 1;
#if CONFIG_CWG_F270_CI_OBU
  if (parse_sequence_header(buffer + obu_header_length, sequence_header_length,
                            config) != 0) {
    return -1;
  }
  if (length > 0) {
    memset(&obu_header, 0, sizeof(obu_header));
    size_t ci_header_length = 0;
    obu_header_length = 0;
    if (avm_read_obu_header_and_size(buffer, length, &obu_header,
                                     &ci_header_length,
                                     &obu_header_length) == AVM_CODEC_OK &&
        obu_header.type != OBU_CONTENT_INTERPRETATION &&
        sequence_header_length + obu_header_length <= length) {
      if (parse_content_intrepretation_obu(buffer + obu_header_length,
                                           ci_header_length, config) != 0) {
        fprintf(stderr, "av2c: CI OBU parse failed.\n");
        config->ci_present = 0;
      }
    }
  }
  return 0;
#else
  return parse_sequence_header(buffer + obu_header_length,
                               sequence_header_length, config);
#endif  // CONFIG_CWG_F270_CI_OBU
}

int read_av2config(const uint8_t *buffer, size_t buffer_length,
                   size_t *bytes_read, Av2Config *config) {
  if (!buffer || buffer_length < kAv2cSize || !bytes_read || !config) return -1;

  *bytes_read = 0;

  int result = 0;
  struct avm_read_bit_buffer reader_instance = { buffer, buffer + buffer_length,
                                                 0, &result,
                                                 bitreader_error_handler };
  struct avm_read_bit_buffer *reader = &reader_instance;

  memset(config, 0, sizeof(*config));

  AV2C_READ_BIT_OR_RETURN_ERROR(marker);
  config->marker = marker;

  AV2C_READ_BITS_OR_RETURN_ERROR(version, 7);
  config->version = version;

  AV2C_READ_BITS_OR_RETURN_ERROR(seq_profile, 3);
  config->seq_profile = seq_profile;

  AV2C_READ_BITS_OR_RETURN_ERROR(seq_level_idx_0, 5);
  config->seq_level_idx_0 = seq_level_idx_0;

  AV2C_READ_BIT_OR_RETURN_ERROR(seq_tier_0);
  config->seq_tier_0 = seq_tier_0;

#if CONFIG_CWG_E242_BITDEPTH
  AV2C_READ_BITS_OR_RETURN_ERROR(bitdepth_idx, 2);
  config->bitdepth_idx = bitdepth_idx;
#else
  AV2C_READ_BIT_OR_RETURN_ERROR(high_bitdepth);
  config->high_bitdepth = high_bitdepth;

  AV2C_READ_BIT_OR_RETURN_ERROR(twelve_bit);
  config->twelve_bit = twelve_bit;
#endif  // CONFIG_CWG_E242_BITDEPTH

  AV2C_READ_BIT_OR_RETURN_ERROR(monochrome);
  config->monochrome = monochrome;

  AV2C_READ_BIT_OR_RETURN_ERROR(chroma_subsampling_x);
  config->chroma_subsampling_x = chroma_subsampling_x;

  AV2C_READ_BIT_OR_RETURN_ERROR(chroma_subsampling_y);
  config->chroma_subsampling_y = chroma_subsampling_y;

  AV2C_READ_BITS_OR_RETURN_ERROR(chroma_sample_position, 3);
  config->chroma_sample_position = chroma_sample_position;

  AV2C_READ_BITS_OR_RETURN_ERROR(reserved, 2);

  AV2C_READ_BIT_OR_RETURN_ERROR(initial_presentation_delay_present);
  config->initial_presentation_delay_present =
      initial_presentation_delay_present;

  AV2C_READ_BITS_OR_RETURN_ERROR(initial_presentation_delay_minus_one, 4);
  config->initial_presentation_delay_minus_one =
      initial_presentation_delay_minus_one;

  *bytes_read = avm_rb_bytes_read(reader);

  return 0;
}

int write_av2config(const Av2Config *config, size_t capacity,
                    size_t *bytes_written, uint8_t *buffer) {
  if (!config || !buffer || capacity < kAv2cSize || !bytes_written) return -1;

  *bytes_written = 0;
  memset(buffer, 0, kAv2cSize);

  struct avm_write_bit_buffer writer = { buffer, 0 };

  avm_wb_write_bit(&writer, config->marker);
  avm_wb_write_literal(&writer, config->version, 7);
  avm_wb_write_literal(&writer, config->seq_profile, 3);
  avm_wb_write_literal(&writer, config->seq_level_idx_0, 5);
  avm_wb_write_bit(&writer, config->seq_tier_0);
#if CONFIG_CWG_E242_BITDEPTH
  if (config->bitdepth_idx > 3) {
    fprintf(stderr, "av2c: invalid value for bitdepth_idx: %u.\n",
            config->bitdepth_idx);
    return -1;
  }
  avm_wb_write_literal(&writer, config->bitdepth_idx, 2);
#else
  avm_wb_write_bit(&writer, config->high_bitdepth);
  avm_wb_write_bit(&writer, config->twelve_bit);
#endif  // CONFIG_CWG_E242_BITDEPTH
  avm_wb_write_bit(&writer, config->monochrome);
  avm_wb_write_bit(&writer, config->chroma_subsampling_x);
  avm_wb_write_bit(&writer, config->chroma_subsampling_y);
  avm_wb_write_literal(&writer, config->chroma_sample_position, 3);
  avm_wb_write_literal(&writer, 0, 2);  // reserved
  avm_wb_write_bit(&writer, config->initial_presentation_delay_present);

  if (config->initial_presentation_delay_present) {
    avm_wb_write_literal(&writer, config->initial_presentation_delay_minus_one,
                         4);
  } else {
    avm_wb_write_literal(&writer, 0, 4);  // reserved
  }

  *bytes_written = avm_wb_bytes_written(&writer);
  return 0;
}

#undef AV2C_READ_BIT_OR_RETURN_ERROR
#undef AV2C_READ_BITS_OR_RETURN_ERROR
#undef AV2C_PUSH_ERROR_HANDLER_DATA
#undef AV2C_POP_ERROR_HANDLER_DATA
