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

/*!\file
 * \brief Declares frame encoding functions.
 */
#ifndef AVM_AV2_ENCODER_ENCODE_STRATEGY_H_
#define AVM_AV2_ENCODER_ENCODE_STRATEGY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "avm/avm_encoder.h"

#include "av2/encoder/encoder.h"
#include "av2/encoder/firstpass.h"

/*!\brief Implement high-level encode strategy
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 * This function will implement high-level encode strategy, choosing frame type,
 * frame placement, etc. It populates an EncodeFrameParams struct with the
 * results of these decisions and then encodes the frame. The caller should use
 * the output parameters *time_stamp and *time_end only when this function
 * returns AVM_CODEC_OK.
 *
 * \param[in]    cpi         Top-level encoder structure
 * \param[in]    size        Bitstream size
 * \param[in]    dest        Bitstream output
 * \param[in]    frame_flags Flags to decide how to encoding the frame
 * \param[out]   time_stamp  Time stamp of the frame
 * \param[out]   time_end    Time end
 * \param[in]    timestamp_ratio Time base
 * \param[in]    flush       Decide to encode one frame or the rest of frames
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AVM_CODEC_OK
 * \retval -1
 * \retval #AVM_CODEC_ERROR
 */
int av2_encode_strategy(AV2_COMP *const cpi, size_t *const size,
                        uint8_t *const dest, unsigned int *frame_flags,
                        int64_t *const time_stamp, int64_t *const time_end,
                        const avm_rational64_t *const timestamp_ratio,
                        int flush);

/*!\cond */
// Set individual buffer update flags based on frame reference type.
// force_refresh_all is used when we have a KEY_FRAME or S_FRAME.  It forces all
// refresh_*_frame flags to be set, because we refresh all buffers in this case.
void av2_configure_buffer_updates(AV2_COMP *const cpi,
                                  const FRAME_UPDATE_TYPE type);
// Encoder-only version for the reference mapping

#if CONFIG_MULTI_LEVEL_SEGMENTATION
void av2_set_seq_seg_info(SequenceHeader *seq_params, struct segmentation *seg);
#endif  // CONFIG_MULTI_LEVEL_SEGMENTATION

void av2_get_ref_frames_enc(AV2_COMP *const cpi, int cur_frame_disp,
                            RefFrameMapPair *ref_frame_map_pairs);

int av2_get_refresh_frame_flags(
    AV2_COMP *const cpi, const EncodeFrameParams *const frame_params,
    FRAME_UPDATE_TYPE frame_update_type, int gf_index, int cur_frame_disp,
    RefFrameMapPair ref_frame_map_pairs[REF_FRAMES]);

int av2_get_refresh_ref_frame_map(AV2_COMMON *cm, int refresh_frame_flags);

int get_forced_keyframe_position(struct lookahead_ctx *lookahead,
                                 const int up_to_index,
                                 const COMPRESSOR_STAGE compressor_stage);

int av2_check_keyframe_arf(int gf_index, GF_GROUP *gf_group,
                           int frame_since_key);
int av2_check_keyframe_overlay(int gf_index, GF_GROUP *gf_group,
                               int frame_since_key);
/*!\endcond */
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_ENCODE_STRATEGY_H_
