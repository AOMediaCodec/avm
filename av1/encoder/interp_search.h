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

#ifndef AOM_AV1_ENCODER_INTERP_FILTER_SEARCH_H_
#define AOM_AV1_ENCODER_INTERP_FILTER_SEARCH_H_

#include "av1/encoder/block.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/rdopt_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!\cond */
#define MAX_INTERP_FILTER_STATS 128
#define DUAL_FILTER_SET_SIZE (SWITCHABLE_FILTERS * SWITCHABLE_FILTERS)

typedef struct {
  InterpFilter interp_fltr;
  int_mv mv[2];
  int8_t ref_frames[2];
  COMPOUND_TYPE comp_type;
  int64_t rd;
  unsigned int pred_sse;
} INTERPOLATION_FILTER_STATS;
/*!\endcond */

/*!\brief Miscellaneous arguments for inter mode search.
 */
typedef struct {
  /*!
   * Pointer to the first member in a 2D array which holds
   * single reference mode motion vectors to be used as a starting
   * point in the mv search for compound modes. Each array is length REF_FRAMES,
   * meaning there is a slot for a single reference motion vector for
   * each possible reference frame. The 2D array consists of N of these arrays,
   * where N is the length of the reference mv stack computed for the single
   * reference case for that particular reference frame.
   */
  int_mv (*single_newmv)[MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];
  /*!
   * Pointer to the first array of a 2D array with the same setup as
   * single_newmv array above. This is a 2D array to hold the rate
   * corresponding to each of the single reference mode motion vectors
   * held in single_newmv.
   */
  int (*single_newmv_rate)[MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];
  /*!
   * Pointer to the first array of a 2D array with the same setup as
   * single_newmv array above. This is a 2D array to hold a 0 or 1
   * validity value corresponding to each of the single reference mode motion
   * vectors held in single_newmv.
   */
  int (*single_newmv_valid)[MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];

  /*!
   * Pointer to the first array in a 3D array of predicted rate-distortion.
   * The dimensions of this structure are:
   * (number of possible inter modes) X
   * (number of reference MVs) X
   * (number of reference frames).
   */
  int64_t (*modelled_rd)[MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];

  /*!
   * Holds an estimated entropy cost for picking the current reference frame.
   * This is used to compute an rd estimate.
   */
  int ref_frame_cost;
  /*!
   * Holds an estimated entropy cost for picking single or compound
   * reference. This is used to compute an rd estimate.
   */
  int single_comp_cost;
  /*!
   * Pointer to the first element in a 3D array holding rd's of
   * SIMPLE_TRANSLATION used to prune out the motion mode search in single ref
   * modes used to determine compound ref modes. The full structure is:
   * (number of inter modes) X (length of refmv list) X (number of ref frames)
   */
  int64_t (*simple_rd)[MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];

  /*!
   * An integer value 0 or 1 which indicates whether or not to skip the motion
   * mode search and default to SIMPLE_TRANSLATION as a speed feature.
   */
  int skip_motion_mode;
  /*!
   * A pointer to the first element in an array of INTERINTRA_MODE types. This
   * contains the best inter_intra mode for each reference frame.
   */
  INTERINTRA_MODE *inter_intra_mode;
  /*!
   * Array of saved interpolation filter stats collected to avoid repeating
   * an interpolation filter search when the mv and ref_frame are the same
   * as a previous search.
   */
  INTERPOLATION_FILTER_STATS interp_filter_stats[MAX_INTERP_FILTER_STATS];
  /*!
   * Index of the last set of saved stats in the interp_filter_stats array.
   */
  int interp_filter_stats_idx;
  /*!
   * Saved MV information for opfl off case.
   */
  NEW_NEWMV_STATS new_newmv_stats[MAX_COMP_MV_STATS];
  int new_newmv_stats_idx;

  NEAR_NEWMV_STATS near_newmv_stats[MAX_COMP_MV_STATS];
  int near_newmv_stats_idx;

  NEW_NEARMV_STATS new_nearmv_stats[MAX_COMP_MV_STATS];
  int new_nearmv_stats_idx;

  JOINT_NEWMV_STATS joint_newmv_stats[MAX_COMP_MV_STATS];
  int joint_newmv_stats_idx;

  JOINT_AMVDNEWMV_STATS joint_amvdnewmv_stats[MAX_COMP_MV_STATS];
  int joint_amvdnewmv_stats_idx;

  NEW_NEWMV_AMVD_STATS new_newmv_amvd_stats[MAX_COMP_MV_STATS];
  int new_newmv_amvd_stats_idx;

  NEW_NEARMV_AMVD_STATS new_nearmv_amvd_stats[MAX_COMP_MV_STATS];
  int new_nearmv_amvd_stats_idx;

  NEAR_NEWMV_AMVD_STATS near_newmv_amvd_stats[MAX_COMP_MV_STATS];
  int near_newmv_amvd_stats_idx;
} HandleInterModeArgs;

/*!\cond */

int av1_find_interp_filter_match(
    MB_MODE_INFO *const mbmi, const AV1_COMP *const cpi,
    const InterpFilter assign_filter, const int need_search,
    INTERPOLATION_FILTER_STATS *interp_filter_stats,
#if CONFIG_COMPOUND_4XN
    MACROBLOCKD *xd,
#endif  // CONFIG_COMPOUND_4XN
    int interp_filter_stats_idx);

int64_t av1_interpolation_filter_search(
    MACROBLOCK *const x, const AV1_COMP *const cpi,
    const TileDataEnc *tile_data, BLOCK_SIZE bsize,
    const BUFFER_SET *const tmp_dst, const BUFFER_SET *const orig_dst,
    int64_t *const rd, int *const switchable_rate, int *skip_build_pred,
    HandleInterModeArgs *args, int64_t ref_best_rd);

/*!\endcond */
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_INTERP_FILTER_SEARCH_H_
