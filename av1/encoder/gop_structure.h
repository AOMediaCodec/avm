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

#ifndef AOM_AV1_ENCODER_GOP_STRUCTURE_H_
#define AOM_AV1_ENCODER_GOP_STRUCTURE_H_

#include "av1/common/av1_common_int.h"
#include "av1/encoder/ratectrl.h"
#include "config/aom_config.h"
#ifdef __cplusplus
extern "C" {
#endif
/*!\cond */
struct AV1_COMP;
struct EncodeFrameParams;

#define NORMAL_BOOST 100

/*!\endcond */

/*!\brief Set up the Group-Of-Pictures structure for this GF_GROUP.
 *
 *\ingroup rate_control
 *
 * This function defines the Group-Of-Pictures structure for this GF_GROUP.
 * This involves deciding where to place the various FRAME_UPDATE_TYPEs in
 * the group. It does this primarily by updateing entries in
 * cpi->twopass.gf_group.update_type[].
 *
 * \param[in]    cpi          Top - level encoder instance structure
 *
 * No return value but this function updates group data structures.
 */
void av1_gop_setup_structure(struct AV1_COMP *cpi);

/*!\brief Distributes bits to frames in a group
 *
 *\ingroup rate_control
 *
 * This function decides on the allocation of bits between the different
 * frames and types of frame in a GF/ARF group.
 *
 * \param[in]   cpi           Top - level encoder instance structure
 * \param[in]   rc            Rate control data
 * \param[in]   gf_group      GF/ARF group data structure
 * \param[in]   is_key_frame  Indicates if the first frame in the group is
 *                            also a key frame.
 * \param[in]   use_arf       Are ARF frames enabled or is this a GF only
 *                            uni-directional group.
 * \param[in]   gf_group_bits Bits available to be allocated.
 *
 * No return but updates the rate control and group data structures
 * to reflect the allocation of bits.
 */
void av1_gop_bit_allocation(const AV1_COMP *cpi, RATE_CONTROL *const rc,
                            GF_GROUP *gf_group, int is_key_frame, int use_arf,
                            int64_t gf_group_bits);

/*!\brief Check if there are enough frames for key filtering
 *
 * \param[in]   frames_to_key   Number of frames to the next Key frame
 * \param[in]   arnr_max_frames Maximum number of frames used
 * \param[in]   lag_in_frames   Lagged frames allowed
 *
 * \return 1 if there are enough frames available else 0
 */
static INLINE int has_enough_frames_for_key_filtering(int frames_to_key,
                                                      int arnr_max_frames,
                                                      int lag_in_frames) {
  return (arnr_max_frames > 0 && lag_in_frames > 0 &&
          frames_to_key > arnr_max_frames);
}

/*!\cond */
int av1_calc_arf_boost(const TWO_PASS *twopass, const RATE_CONTROL *rc,
                       FRAME_INFO *frame_info, int offset, int f_frames,
                       int b_frames, int *num_fpstats_used,
                       int *num_fpstats_required);
/*!\endcond */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_GOP_STRUCTURE_H_
