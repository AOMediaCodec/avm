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

#ifndef AOM_AV1_DECODER_DECODEFRAME_H_
#define AOM_AV1_DECODER_DECODEFRAME_H_

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_THROUGHPUT_ANALYSIS
extern int64_t tot_ctx_syms;
extern int64_t tot_bypass_syms;
extern int64_t max_ctx_syms;
extern int64_t max_bypass_syms;
extern int64_t max_bits;
extern int64_t tot_bits;
extern int64_t tot_frames;
extern int64_t total_context_switch;
extern int64_t total_total_hits;
#endif  // CONFIG_THROUGHPUT_ANALYSIS

struct AV1Decoder;
struct aom_read_bit_buffer;
struct ThreadData;

#if CONFIG_CROP_WIN_CWG_F220
void av1_read_conformance_window(struct aom_read_bit_buffer *rb,
                                 struct SequenceHeader *seq_params);
#endif  // CONFIG_CROP_WIN_CWG_F220

#if CONFIG_MULTILAYER_HLS
uint32_t av1_read_layer_configuration_record_obu(
    struct AV1Decoder *pbi, int obu_xlayer_id, struct aom_read_bit_buffer *rb);

uint32_t av1_read_operating_point_set_obu(struct AV1Decoder *pbi,
                                          int obu_xlayer_id,
                                          struct aom_read_bit_buffer *rb);

uint32_t av1_read_atlas_segment_info_obu(struct AV1Decoder *pbi,
                                         int obu_xLayer_id,
                                         struct aom_read_bit_buffer *rb);
#endif  // CONFIG_MULTILAYER_HLS

#if CONFIG_F153_FGM_OBU
void copy_fgm_from_list(AV1_COMMON *cm, aom_film_grain_t *pars,
                        struct film_grain_model *fgm);
#endif  // CONFIG_F153_FGM_OBU
// Reads the middle part of the sequence header OBU (from
// frame_width_bits_minus_1 to enable_restoration) into seq_params.
// Reports errors by calling rb->error_handler() or aom_internal_error().
void av1_read_sequence_header(struct aom_read_bit_buffer *rb,
                              SequenceHeader *seq_params);

#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
// Reads the tile information in the sequence header
void read_sequence_tile_info(struct SequenceHeader *seq_params,
                             struct aom_read_bit_buffer *rb);
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO

// Reads additional sequence header for coding tools beyond AV1
void av1_read_sequence_header_beyond_av1(
    struct aom_read_bit_buffer *rb, SequenceHeader *seq_params
#if !CONFIG_F255_QMOBU
    ,
    CommonQuantParams *quant_params, struct aom_internal_error_info *error_info
#endif  // !CONFIG_F255_QMOBU
);

#if CONFIG_F255_QMOBU
void av1_copy_predefined_qmatrices_to_list(struct AV1Decoder *pbi,
                                           int num_planes,
                                           bool as_seq_header_init);
#endif  // CONFIG_F255_QMOBU

#if CONFIG_MULTI_FRAME_HEADER
// Reads multi-frame header
void av1_read_multi_frame_header(AV1_COMMON *cm,
                                 struct aom_read_bit_buffer *rb);
#endif  // CONFIG_MULTI_FRAME_HEADER

void av1_read_frame_size(struct aom_read_bit_buffer *rb, int num_bits_width,
                         int num_bits_height, int *width, int *height);
BITSTREAM_PROFILE av1_read_profile(struct aom_read_bit_buffer *rb);

int av1_check_byte_alignment(AV1_COMMON *const cm,
                             struct aom_read_bit_buffer *const rb);

// Returns 0 on success. Sets pbi->common.error.error_code and returns -1 on
// failure.
int av1_check_trailing_bits(struct AV1Decoder *pbi,
                            struct aom_read_bit_buffer *rb);
#if CONFIG_F024_KEYOBU
int are_seq_headers_consistent(const SequenceHeader *seq_params_old,
                               const SequenceHeader *seq_params_new);
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_F106_OBU_TILEGROUP
// On success, returns the tilegroup header size. On failure, calls
// aom_internal_error and does not return.
int32_t av1_read_tilegroup_header(
    struct AV1Decoder *pbi, struct aom_read_bit_buffer *rb, const uint8_t *data,
    const uint8_t **p_data_end, int *first_tile_group_in_frame, int *start_tile,
    int *end_tile, OBU_TYPE obu_type);
#else
// On success, returns the frame header size. On failure, calls
// aom_internal_error and does not return.
// TODO(wtc): Figure out and document the p_data_end parameter.
uint32_t av1_decode_frame_headers_and_setup(struct AV1Decoder *pbi,
                                            struct aom_read_bit_buffer *rb,
                                            const uint8_t *data,
                                            const uint8_t **p_data_end,
                                            int trailing_bits_present);
#endif  // CONFIG_F106_OBU_TILEGROUP
void av1_decode_tg_tiles_and_wrapup(struct AV1Decoder *pbi, const uint8_t *data,
                                    const uint8_t *data_end,
                                    const uint8_t **p_data_end, int start_tile,
                                    int end_tile, int initialize_flag);

// Implements the color_config() function in the spec. Reports errors by
// calling rb->error_handler() or aom_internal_error().
void av1_read_color_config(struct aom_read_bit_buffer *rb,
                           SequenceHeader *seq_params,
                           struct aom_internal_error_info *error_info);

// Implements the timing_info() function in the spec. Reports errors by calling
// rb->error_handler() or aom_internal_error().
void av1_read_timing_info_header(aom_timing_info_t *timing_info,
                                 struct aom_internal_error_info *error,
                                 struct aom_read_bit_buffer *rb);

// Implements the decoder_model_info() function in the spec. Reports errors by
// calling rb->error_handler().
void av1_read_decoder_model_info(aom_dec_model_info_t *decoder_model_info,
                                 struct aom_read_bit_buffer *rb);

// Implements the operating_parameters_info() function in the spec. Reports
// errors by calling rb->error_handler().
void av1_read_op_parameters_info(aom_dec_model_op_parameters_t *op_params,
                                 int buffer_delay_length,
                                 struct aom_read_bit_buffer *rb);

struct aom_read_bit_buffer *av1_init_read_bit_buffer(
    struct AV1Decoder *pbi, struct aom_read_bit_buffer *rb, const uint8_t *data,
    const uint8_t *data_end);

void av1_free_mc_tmp_buf(struct ThreadData *thread_data);
void av1_free_opfl_tmp_bufs(struct ThreadData *thread_data);

#if CONFIG_CROP_WIN_CWG_F220
void av1_validate_frame_level_conformance(
    const struct SequenceHeader *seq_params, int frame_width, int frame_height,
    struct aom_internal_error_info *error_info);
#endif  // CONFIG_CROP_WIN_CWG_F220

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_DECODER_DECODEFRAME_H_
