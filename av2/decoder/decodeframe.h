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

#ifndef AVM_AV2_DECODER_DECODEFRAME_H_
#define AVM_AV2_DECODER_DECODEFRAME_H_

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

struct AV2Decoder;
struct avm_read_bit_buffer;
struct ThreadData;

void av2_read_conformance_window(struct avm_read_bit_buffer *rb,
                                 struct SequenceHeader *seq_params);

uint32_t av2_read_layer_configuration_record_obu(
    struct AV2Decoder *pbi, int obu_xlayer_id, struct avm_read_bit_buffer *rb);

uint32_t av2_read_operating_point_set_obu(struct AV2Decoder *pbi,
                                          int obu_xlayer_id,
                                          struct avm_read_bit_buffer *rb);

uint32_t av2_read_atlas_segment_info_obu(struct AV2Decoder *pbi,
                                         int obu_xLayer_id,
                                         struct avm_read_bit_buffer *rb);

void copy_fgm_from_list(AV2_COMMON *cm, avm_film_grain_t *pars,
                        const struct film_grain_model *fgm);
// Reads the middle part of the sequence header OBU (from
// frame_width_bits_minus_1 to enable_restoration) into seq_params.
// Reports errors by calling rb->error_handler() or avm_internal_error().
void av2_read_sequence_header(struct avm_read_bit_buffer *rb,
                              SequenceHeader *seq_params);

// Reads the tile information in the sequence header
void read_sequence_tile_info(struct SequenceHeader *seq_params,
                             struct avm_read_bit_buffer *rb);

void alloc_qmatrix(struct quantization_matrix_set *qm_set);
void av2_copy_predefined_qmatrices_to_list(struct AV2Decoder *pbi,
                                           int num_planes);

// Reads multi-frame header
void av2_read_multi_frame_header(AV2_COMMON *cm,
                                 struct avm_read_bit_buffer *rb);

void av2_read_frame_size(struct avm_read_bit_buffer *rb, int num_bits_width,
                         int num_bits_height, int *width, int *height);
BITSTREAM_PROFILE av2_read_profile(struct avm_read_bit_buffer *rb);
int av2_check_byte_alignment(AV2_COMMON *const cm,
                             struct avm_read_bit_buffer *const rb);

// Returns 0 on success. Sets pbi->common.error.error_code and returns -1 on
// failure.
int av2_check_trailing_bits(struct AV2Decoder *pbi,
                            struct avm_read_bit_buffer *rb);
int are_seq_headers_consistent(const SequenceHeader *seq_params_old,
                               const SequenceHeader *seq_params_new);
// On success, returns the tilegroup header size. On failure, calls
// avm_internal_error and does not return.
int32_t av2_read_tilegroup_header(
    struct AV2Decoder *pbi, struct avm_read_bit_buffer *rb, const uint8_t *data,
    const uint8_t **p_data_end, int *first_tile_group_in_frame, int *start_tile,
    int *end_tile, OBU_TYPE obu_type);

void av2_decode_tg_tiles_and_wrapup(struct AV2Decoder *pbi, const uint8_t *data,
                                    const uint8_t *data_end,
                                    const uint8_t **p_data_end, int start_tile,
                                    int end_tile, int initialize_flag);

uint32_t av2_read_content_interpretation_obu(struct AV2Decoder *pbi,
                                             struct avm_read_bit_buffer *rb);

// Reads the chroma format and bitdepth in the sequence header. Reports errors
// by calling rb->error_handler() or avm_internal_error().
void av2_read_chroma_format_bitdepth(
    struct avm_read_bit_buffer *rb, SequenceHeader *seq_params,
    struct avm_internal_error_info *error_info);

// Implements the timing_info() function in the spec. Reports errors by calling
// rb->error_handler() or avm_internal_error().
void av2_read_timing_info_header(avm_timing_info_t *timing_info,
                                 struct avm_internal_error_info *error,
                                 struct avm_read_bit_buffer *rb);

struct avm_read_bit_buffer *av2_init_read_bit_buffer(
    struct AV2Decoder *pbi, struct avm_read_bit_buffer *rb, const uint8_t *data,
    const uint8_t *data_end);

void av2_free_mc_tmp_buf(struct ThreadData *thread_data);
void av2_free_opfl_tmp_bufs(struct ThreadData *thread_data);

void av2_validate_frame_level_conformance(
    const struct SequenceHeader *seq_params, int frame_width, int frame_height,
    struct avm_internal_error_info *error_info);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_DECODER_DECODEFRAME_H_
