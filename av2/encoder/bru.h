/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_ENCODER_BRU_H_
#define AVM_AV2_ENCODER_BRU_H_

#include <stdbool.h>

#include "av2/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!\brief Check if a superblock is pixel-exact across all Y, U, and V planes.
 *
 * Compares the superblock at \c (mi_row, mi_col) between two uncompressed
 * frame buffers \c a and \c b using SIMD sum-of-absolute-differences on
 * full-superblock interior blocks and clipped row comparisons on right/bottom
 * frame boundaries.
 *
 * \param[in]   cpi     Top-level encoder structure
 * \param[in]   a       First frame buffer to compare
 * \param[in]   b       Second frame buffer to compare
 * \param[in]   mi_row  Superblock row coordinate in MI units
 * \param[in]   mi_col  Superblock column coordinate in MI units
 *
 * \return Returns true if all luma and chroma samples in the superblock are
 *         identical between \c a and \c b; false otherwise.
 */
bool av2_sb_planes_identical(const AV2_COMP *cpi, const YV12_BUFFER_CONFIG *a,
                             const YV12_BUFFER_CONFIG *b, int mi_row,
                             int mi_col);

/*!\brief Detect active, support, and inactive BRU superblocks for 1-pass CBR.
 *
 * Classifies each superblock by comparing the current source frame \c cur_src
 * against the BRU reference source frame \c bru_src (at display order \c t-2)
 * and checking whether the reference superblock's certified quantizer in
 * \c cpi->rc.rtc_bru.sb_q[bru_slot] is already within the current CBR target Q
 * plus hysteresis margin. Also holds off whole-frame BRU skips while cyclic
 * refresh (\c aq-mode=3) still has unboosted static superblocks, and clusters
 * active superblocks into at most \c MAX_ACTIVE_REGION bounding boxes with a
 * 1-superblock \c BRU_SUPPORT_SB collar.
 *
 * \param[in,out] cpi       Top-level encoder structure
 * \param[in]     cur_src   Current uncompressed source frame buffer (\c t)
 * \param[in]     bru_src   BRU reference uncompressed source buffer (\c t-2)
 * \param[in]     bru_slot  DPB slot index of the BRU reference frame
 */
void av2_detect_cbr_bru_regions(AV2_COMP *cpi,
                                const YV12_BUFFER_CONFIG *cur_src,
                                const YV12_BUFFER_CONFIG *bru_src,
                                int bru_slot);

/*!\brief Tag the current lookahead source buffer with its display order hint.
 *
 * Records \c cur_frame_disp on \c source->img.display_order_hint so a future
 * frame at \c cur_frame_disp + 2 can locate the matching uncompressed source
 * frame for BRU active-region detection. Resets older lookahead entries on
 * keyframes.
 *
 * \param[in]     cpi             Top-level encoder structure
 * \param[in,out] source          Lookahead entry for the current source frame
 * \param[in]     frame_type      Current frame type
 * \param[in]     cur_frame_disp  Display order hint of the current frame
 */
void av2_update_rt_bru_lookahead_disp_order(const AV2_COMP *cpi,
                                            struct lookahead_entry *source,
                                            FRAME_TYPE frame_type,
                                            int cur_frame_disp);

/*!\brief Find a valid DPB reference slot for 1-pass RTC CBR BRU.
 *
 * Checks whether 1-pass RTC CBR BRU can run on the current frame (at least 3
 * frames after a keyframe, no scene change, matching dimensions, and at least
 * 3 valid past references in the DPB) and returns the DPB slot index whose
 * \c display_order_hint matches \c bru_ref_source (\c cur_frame_disp - 2) with
 * certified per-superblock quantizers.
 *
 * \param[in]   cpi             Top-level encoder structure
 * \param[in]   frame_input     Input frame buffers for the current frame
 * \param[in]   bru_ref_source  Lookahead entry at \c cur_frame_disp - 2
 * \param[in]   cur_frame_disp  Display order hint of the current frame
 *
 * \return Returns the DPB slot index (\c 0..REF_FRAMES-1) if a valid BRU
 *         reference slot is found; \c -1 otherwise.
 */
int av2_find_rtc_cbr_bru_slot(const AV2_COMP *cpi,
                              const EncodeFrameInput *frame_input,
                              const struct lookahead_entry *bru_ref_source,
                              int cur_frame_disp);

/*!\brief Disable segmentation and cyclic refresh on RTC CBR BRU frames.
 *
 * Clears \c cm->seg.enabled and disables cyclic refresh on the current frame
 * when 1-pass RTC CBR BRU is active (\c cm->bru.enabled == 1).
 *
 * \param[in,out] cpi  Top-level encoder structure
 */
void av2_disable_rtc_bru_segmentation(AV2_COMP *cpi);

/*!\brief Check whether 0-lag lookahead uses the RTC CBR BRU ring buffer.
 *
 * \param[in]   cpi  Top-level encoder structure
 *
 * \return Returns true when \c rtc_enable_bru is active in 0-lag 1-pass CBR
 *         with extra lookahead buffers allocated; false otherwise.
 */
bool av2_preserve_zero_lag_cbr_bru_lookahead(const AV2_COMP *cpi);

/*!\brief Rewind the 0-lag lookahead read cursor when CBR drops a frame.
 *
 * Restores \c cpi->lookahead->read_idx and \c sz after
 * \c encode_frame_to_data_rate() drops the current frame so the popped entry
 * does not overwrite the \c t-2 BRU reference source buffer in the 3-slot
 * circular lookahead ring.
 *
 * \param[in,out] cpi  Top-level encoder structure
 */
void av2_rewind_zero_lag_cbr_bru_lookahead_on_drop(AV2_COMP *cpi);

/*!\brief Initialize per-superblock scratch quantizers before encoding a frame.
 *
 * Allocates \c cpi->rc.rtc_bru buffers if needed and records the pre-encode
 * effective quantizer of each superblock in \c cpi->rc.rtc_bru.cur_sb_q
 * (accounting for cyclic refresh segment delta-Q when enabled).
 *
 * \param[in,out] cpi  Top-level encoder structure
 */
void av2_init_cbr_bru_frame_sb_q(AV2_COMP *cpi);

/*!\brief Compute each superblock's certified quantizer after encoding a frame.
 *
 * Updates \c cpi->rc.rtc_bru.cur_sb_q with the effective quantizer achieved by
 * each superblock during \c av2_encode_frame(), propagating certified
 * reference superblock quantizers for zero-MV skip blocks and \c BRU_INACTIVE
 * / \c BRU_SUPPORT superblocks.
 *
 * \param[in,out] cpi  Top-level encoder structure
 */
void av2_compute_cbr_bru_frame_sb_q(AV2_COMP *cpi);

/*!\brief Commit the current frame's per-SB quantizers into refreshed DPB slots.
 *
 * Copies \c cpi->rc.rtc_bru.cur_sb_q into \c cpi->rc.rtc_bru.sb_q[slot] for
 * every DPB slot refreshed by the current frame (and into \c explicit_ref_idx
 * when BRU updates a reference in place), invalidating any uncertified slots.
 *
 * \param[in,out] cpi  Top-level encoder structure
 */
void av2_commit_cbr_bru_dpb_sb_q(AV2_COMP *cpi);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_BRU_H_
