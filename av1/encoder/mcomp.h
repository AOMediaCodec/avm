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

#ifndef AOM_AV1_ENCODER_MCOMP_H_
#define AOM_AV1_ENCODER_MCOMP_H_

#include "av1/common/mv.h"
#include "av1/encoder/block.h"
#include "av1/common/reconinter.h"

#include "aom_dsp/variance.h"

#ifdef __cplusplus
extern "C" {
#endif

// The maximum number of steps in a step search given the largest
// allowed initial step
#define MAX_MVSEARCH_STEPS 11
// Max full pel mv specified in the unit of full pixel
#if CONFIG_MV_SEARCH_RANGE
// Enable the use of motion vector in range [-2047, 2047].
#define MAX_FULL_PEL_VAL ((1 << MV_CLASSES) - 1)
#else
// Enable the use of motion vector in range [-1023, 1023].
#define MAX_FULL_PEL_VAL ((1 << (MAX_MVSEARCH_STEPS - 1)) - 1)
#endif  // CONFIG_MV_SEARCH_RANGE
// Maximum size of the first step in full pel units
#define MAX_FIRST_STEP (1 << (MAX_MVSEARCH_STEPS - 1))
// Maximum number of neighbors to scan per iteration during
// WARPED_CAUSAL refinement
// Note: The elements of warp_search_config.neighbor_mask must be at least
// MAX_WARP_SEARCH_NEIGHBORS many bits wide. So the type may need to be
// widened if this value is increased.
#define MAX_WARP_SEARCH_NEIGHBORS 8

#define SEARCH_RANGE_8P 3
#define SEARCH_GRID_STRIDE_8P (2 * SEARCH_RANGE_8P + 1)
#define SEARCH_GRID_CENTER_8P \
  (SEARCH_RANGE_8P * SEARCH_GRID_STRIDE_8P + SEARCH_RANGE_8P)

// motion search site
typedef struct search_site {
  FULLPEL_MV mv;
  int offset;
} search_site;

typedef struct search_site_config {
  search_site site[MAX_MVSEARCH_STEPS * 2][16 + 1];
  // Number of search steps.
  int num_search_steps;
  int searches_per_step[MAX_MVSEARCH_STEPS * 2];
  int radius[MAX_MVSEARCH_STEPS * 2];
  int stride;
} search_site_config;

typedef struct {
  FULLPEL_MV coord;
  int coord_offset;
} search_neighbors;

struct AV1_COMP;
struct SPEED_FEATURES;

typedef struct {
  // The reference mv used to compute the mv cost
  const MV *ref_mv;
  FULLPEL_MV full_ref_mv;
  MV_COST_TYPE mv_cost_type;
  // Stores the entropy table needed to signal an mv. Includes the joint-mv cost
  // and the diff cost.
  const MvCosts *mv_costs;
  MvSubpelPrecision pb_mv_precision;
  int is_adaptive_mvd;
#if CONFIG_IBC_BV_IMPROVEMENT
  int is_ibc_cost;
#endif
} MV_COST_PARAMS;
#if CONFIG_DERIVED_MVD_SIGN
int av1_mv_sign_cost(const int sign, const int comp, const MvCosts *mv_costs,
                     int weight, int round_bit, const int is_adaptive_mvd);
#endif  // CONFIG_DERIVED_MVD_SIGN
int av1_mv_bit_cost(const MV *mv, const MV *ref_mv,
                    const MvSubpelPrecision pb_mv_precision,
                    const MvCosts *mv_costs, int weight,
                    const int is_adaptive_mvd);

int av1_intrabc_mv_bit_cost(const MV *mv, const MV *ref_mv,
                            const IntraBCMvCosts *mv_costs, int weight
#if CONFIG_IBC_SUBPEL_PRECISION
                            ,
                            MvSubpelPrecision precision
#endif  // CONFIG_IBC_SUBPEL_PRECISION
);

int av1_get_mvpred_sse(const MV_COST_PARAMS *mv_cost_params,
                       const FULLPEL_MV best_mv,
                       const aom_variance_fn_ptr_t *vfp,
                       const struct buf_2d *src, const struct buf_2d *pre);
int av1_get_mvpred_compound_var(
    const MV_COST_PARAMS *ms_params, const FULLPEL_MV best_mv,
    const uint16_t *second_pred, const uint8_t *mask, int mask_stride,
    int invert_mask, const aom_variance_fn_ptr_t *vfp, const struct buf_2d *src,
    const struct buf_2d *pre);

// =============================================================================
//  Motion Search
// =============================================================================
typedef struct {
  // The reference buffer
  const struct buf_2d *ref;

  // The source and predictors/mask used by translational search
  const struct buf_2d *src;
  const uint16_t *second_pred;
  const uint8_t *mask;
  int mask_stride;
  int inv_mask;

  // The weighted source and mask used by OBMC
  const int32_t *wsrc;
  const int32_t *obmc_mask;
} MSBuffers;

static INLINE void av1_set_ms_compound_refs(MSBuffers *ms_buffers,
                                            const uint16_t *second_pred,
                                            const uint8_t *mask,
                                            int mask_stride, int invert_mask) {
  ms_buffers->second_pred = second_pred;
  ms_buffers->mask = mask;
  ms_buffers->mask_stride = mask_stride;
  ms_buffers->inv_mask = invert_mask;
}

// =============================================================================
//  Fullpixel Motion Search
// =============================================================================
enum {
  // Search 8-points in the radius grid around center, up to 11 search stages.
  DIAMOND = 0,
  // Search 12-points in the radius/tan_radius grid around center,
  // up to 15 search stages.
  NSTEP = 1,
  // Search maximum 8-points in the radius grid around center,
  // up to 11 search stages. First stage consists of 8 search points
  // and the rest with 6 search points each in hex shape.
  HEX = 2,
  // Search maximum 8-points in the radius grid around center,
  // up to 11 search stages. First stage consists of 4 search
  // points and the rest with 8 search points each.
  BIGDIA = 3,
  // Search 8-points in the square grid around center, up to 11 search stages.
  SQUARE = 4,
  // HEX search with up to 2 stages.
  FAST_HEX = 5,
  // BIGDIA search with up to 2 stages.
  FAST_DIAMOND = 6,
  // BIGDIA search with up to 3 stages.
  FAST_BIGDIA = 7,
  // Total number of search methods.
  NUM_SEARCH_METHODS,
  // Number of distinct search methods.
  NUM_DISTINCT_SEARCH_METHODS = SQUARE + 1,
} UENUM1BYTE(SEARCH_METHODS);

typedef struct warp_search_config {
  int num_neighbors;
  MV neighbors[MAX_WARP_SEARCH_NEIGHBORS];
  // Bitmask which is used to prune the search neighbors at one iteration
  // based on which direction we chose in the previous iteration.
  // See comments in av1_refine_warped_mv for details.
  uint8_t neighbor_mask[MAX_WARP_SEARCH_NEIGHBORS];
} warp_search_config;

// Methods for refining WARPED_CAUSAL motion vectors
enum {
  // Search 4 adjacent points in a diamond shape at each iteration
  WARP_SEARCH_DIAMOND,
  // Search 8 adjacent points in a square at each iteration
  WARP_SEARCH_SQUARE,
  WARP_SEARCH_METHODS
} UENUM1BYTE(WARP_SEARCH_METHOD);

// This struct holds fullpixel motion search parameters that should be constant
// during the search
typedef struct {
  BLOCK_SIZE bsize;
  // A function pointer to the simd function for fast computation
  const aom_variance_fn_ptr_t *vfp;

#if CONFIG_IBC_SR_EXT
  const MACROBLOCKD *xd;
  int mib_size_log2;
  const AV1_COMMON *cm;
  int mi_row;
  int mi_col;
#endif  // CONFIG_IBC_SR_EXT
#if CONFIG_IBC_BV_IMPROVEMENT
  MACROBLOCK *x;
  int ref_bv_cnt;
#endif  // CONFIG_IBC_BV_IMPROVEMENT

  MSBuffers ms_buffers;

  // WARNING: search_method should be regarded as a private variable and should
  // not be modified directly so it is in sync with search_sites. To modify it,
  // use av1_set_mv_search_method.
  SEARCH_METHODS search_method;
  const search_site_config *search_sites;
  FullMvLimits mv_limits;

  int run_mesh_search;    // Sets mesh search unless it got pruned by
                          // prune_mesh_search.
  int prune_mesh_search;  // Disables mesh search if the best_mv after a normal
                          // search if close to the start_mv.
  int force_mesh_thresh;  // Forces mesh search if the residue variance is
                          // higher than the threshold.
  const struct MESH_PATTERN *mesh_patterns[2];

  // Use maximum search interval of 4 if true. This helps motion search to find
  // the best motion vector for screen content types.
  int fine_search_interval;

  int is_intra_mode;

  int fast_obmc_search;

  // For calculating mv cost
  MV_COST_PARAMS mv_cost_params;

  // Stores the function used to compute the sad. This can be different from the
  // sdf in vfp (e.g. downsampled sad and not sad) to allow speed up.
  aom_sad_fn_t sdf;
  aom_sad_multi_d_fn_t sdx4df;
} FULLPEL_MOTION_SEARCH_PARAMS;

void av1_make_default_fullpel_ms_params(
    FULLPEL_MOTION_SEARCH_PARAMS *ms_params, const struct AV1_COMP *cpi,
    const MACROBLOCK *x, BLOCK_SIZE bsize, const MV *ref_mv,
    const MvSubpelPrecision pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
    const int is_ibc_cost,
#endif

    const search_site_config search_sites[NUM_DISTINCT_SEARCH_METHODS],
    int fine_search_interval);

// Sets up configs for fullpixel diamond search method.
void av1_init_dsmotion_compensation(search_site_config *cfg, int stride);
// Sets up configs for firstpass motion search.
void av1_init_motion_fpf(search_site_config *cfg, int stride);
// Sets up configs for all other types of motion search method.
void av1_init_motion_compensation_nstep(search_site_config *cfg, int stride);
// Sets up configs for BIGDIA / FAST_DIAMOND / FAST_BIGDIA
// motion search method.
void av1_init_motion_compensation_bigdia(search_site_config *cfg, int stride);
// Sets up configs for HEX or FAST_HEX motion search method.
void av1_init_motion_compensation_hex(search_site_config *cfg, int stride);
// Sets up configs for SQUARE motion search method.
void av1_init_motion_compensation_square(search_site_config *cfg, int stride);

// Mv beyond the range do not produce new/different prediction block.
static INLINE void av1_set_mv_search_method(
    FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
    const search_site_config search_sites[NUM_DISTINCT_SEARCH_METHODS],
    SEARCH_METHODS search_method) {
  // Array to inform which all search methods are having
  // same candidates and different in number of search steps.
  static const SEARCH_METHODS search_method_lookup[NUM_SEARCH_METHODS] = {
    DIAMOND,  // DIAMOND
    NSTEP,    // NSTEP
    HEX,      // HEX
    BIGDIA,   // BIGDIA
    SQUARE,   // SQUARE
    HEX,      // FAST_HEX
    BIGDIA,   // FAST_DIAMOND
    BIGDIA    // FAST_BIGDIA
  };

  ms_params->search_method = search_method;
  if (search_sites && search_method < NUM_SEARCH_METHODS) {
    ms_params->search_sites =
        &search_sites[search_method_lookup[ms_params->search_method]];
  }
}

// Set up limit values for MV components.
// Mv beyond the range do not produce new/different prediction block.
static INLINE void av1_set_mv_row_limits(
    const CommonModeInfoParams *const mi_params, FullMvLimits *mv_limits,
    int mi_row, int mi_height, int border) {
  const int min1 = -(mi_row * MI_SIZE + border - 2 * AOM_INTERP_EXTEND);
  const int min2 = -(((mi_row + mi_height) * MI_SIZE) + 2 * AOM_INTERP_EXTEND);
  mv_limits->row_min = AOMMAX(min1, min2);
  const int max1 = (mi_params->mi_rows - mi_row - mi_height) * MI_SIZE +
                   border - 2 * AOM_INTERP_EXTEND;
  const int max2 =
      (mi_params->mi_rows - mi_row) * MI_SIZE + 2 * AOM_INTERP_EXTEND;
  mv_limits->row_max = AOMMIN(max1, max2);
}

static INLINE void av1_set_mv_col_limits(
    const CommonModeInfoParams *const mi_params, FullMvLimits *mv_limits,
    int mi_col, int mi_width, int border) {
  const int min1 = -(mi_col * MI_SIZE + border - 2 * AOM_INTERP_EXTEND);
  const int min2 = -(((mi_col + mi_width) * MI_SIZE) + 2 * AOM_INTERP_EXTEND);
  mv_limits->col_min = AOMMAX(min1, min2);
  const int max1 = (mi_params->mi_cols - mi_col - mi_width) * MI_SIZE + border -
                   2 * AOM_INTERP_EXTEND;
  const int max2 =
      (mi_params->mi_cols - mi_col) * MI_SIZE + 2 * AOM_INTERP_EXTEND;
  mv_limits->col_max = AOMMIN(max1, max2);
}

static INLINE void av1_set_mv_limits(
    const CommonModeInfoParams *const mi_params, FullMvLimits *mv_limits,
    int mi_row, int mi_col, int mi_height, int mi_width, int border) {
  av1_set_mv_row_limits(mi_params, mv_limits, mi_row, mi_height, border);
  av1_set_mv_col_limits(mi_params, mv_limits, mi_col, mi_width, border);
}

void av1_set_mv_search_range(FullMvLimits *mv_limits, const MV *mv,
                             MvSubpelPrecision pb_mv_precision

);

#if CONFIG_OPFL_MV_SEARCH
#define OMVS_AVG_POOLING 1
#define OMVS_RANGE_THR 2
#define OMVS_BIG_STEP 4
#define OMVS_EARLY_TERM 1
#define OMVS_SAD_THR 8

// Obtain the bits of upshift for the MVD derived by optical flow based MV
// search. The purpose for upscaling the MVD is to increase the search range and
// obtain a new search point not covered by the traditional local search.
static INLINE int get_opfl_mv_upshift_bits(const MB_MODE_INFO *mbmi) {
  if (mbmi->mode == NEWMV ||
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
      mbmi->mode == WARP_NEWMV ||
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
      mbmi->mode == WARPMV)
    return 3;
  return 0;
}

int get_opfl_mv_iterations(const struct AV1_COMP *cpi,
                           const MB_MODE_INFO *mbmi);
#endif  // CONFIG_OPFL_MV_SEARCH

void av1_set_tip_mv_search_range(FullMvLimits *mv_limits);

int av1_init_search_range(int size);

int av1_refining_search_8p_c(const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                             const FULLPEL_MV start_mv, FULLPEL_MV *best_mv);
int av1_refining_search_8p_c_low_precision(
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, const FULLPEL_MV start_mv,
    FULLPEL_MV *best_mv, int fast_mv_refinement);

int av1_full_pixel_search(const FULLPEL_MV start_mv,
                          const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                          const int step_param, int *cost_list,
                          FULLPEL_MV *best_mv, FULLPEL_MV *second_best_mv);

int av1_intrabc_hash_search(const struct AV1_COMP *cpi, const MACROBLOCKD *xd,
                            const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                            IntraBCHashInfo *intrabc_hash_info,
                            FULLPEL_MV *best_mv);

int av1_obmc_full_pixel_search(const FULLPEL_MV start_mv,
                               const FULLPEL_MOTION_SEARCH_PARAMS *ms_params,
                               const int step_param, FULLPEL_MV *best_mv);

static INLINE int av1_is_fullmv_in_range(const FullMvLimits *mv_limits,
                                         FULLPEL_MV mv,
                                         MvSubpelPrecision pb_mv_precision

) {
  if (pb_mv_precision < MV_PRECISION_ONE_PEL) {
    if (mv.col & ((1 << (MV_PRECISION_ONE_PEL - pb_mv_precision)) - 1))
      return 0;
    if (mv.row & ((1 << (MV_PRECISION_ONE_PEL - pb_mv_precision)) - 1))
      return 0;
  }

  return (mv.col >= mv_limits->col_min) && (mv.col <= mv_limits->col_max) &&
         (mv.row >= mv_limits->row_min) && (mv.row <= mv_limits->row_max);
}
// =============================================================================
//  Subpixel Motion Search
// =============================================================================
enum {
  EIGHTH_PEL,
  QUARTER_PEL,
  HALF_PEL,
  FULL_PEL
} UENUM1BYTE(SUBPEL_FORCE_STOP);

typedef struct {
  const aom_variance_fn_ptr_t *vfp;
  SUBPEL_SEARCH_TYPE subpel_search_type;
  // Source and reference buffers
  MSBuffers ms_buffers;
  int w, h;
} SUBPEL_SEARCH_VAR_PARAMS;

// This struct holds subpixel motion search parameters that should be constant
// during the search
typedef struct {
  // High level motion search settings
  const int *cost_list;
  SUBPEL_FORCE_STOP forced_stop;
  int iters_per_step;
  SubpelMvLimits mv_limits;

  // For calculating mv cost
  MV_COST_PARAMS mv_cost_params;

  // Distortion calculation params
  SUBPEL_SEARCH_VAR_PARAMS var_params;
} SUBPEL_MOTION_SEARCH_PARAMS;

#if CONFIG_SKIP_ME_FOR_OPFL_MODES
#define MAX_COMP_MV_STATS 128

typedef struct {
  int8_t ref_frame_type;
  int ref_mv_idx_type;
  MvSubpelPrecision mv_precision;
  int_mv mv[2];
} NEW_NEWMV_STATS;

typedef struct {
  int8_t ref_frame_type;
  int_mv start_mv;
  int_mv ref_mv;
  int_mv mv[2];
} NEAR_NEWMV_STATS;

typedef struct {
  int8_t ref_frame_type;
  int ref_mv_idx_type;
  int_mv mv;
} NEW_NEARMV_STATS;

typedef struct {
  int8_t ref_frame_type;
  int ref_mv_idx_type;
  MvSubpelPrecision mv_precision;
  int joint_newmv_scale_idx;
  int8_t cwp_idx;
  int_mv mv[2];
} JOINT_NEWMV_STATS;

typedef struct {
  int8_t ref_frame_type;
  int ref_mv_idx_type;
  int joint_amvd_scale_idx;
  int8_t cwp_idx;
  int_mv mv[2];
} JOINT_AMVDNEWMV_STATS;
#endif  // CONFIG_SKIP_ME_FOR_OPFL_MODES

#if CONFIG_OPFL_MV_SEARCH
int opfl_refine_fullpel_mv_one_sided(
    const AV1_COMMON *cm, MACROBLOCKD *xd,
    const FULLPEL_MOTION_SEARCH_PARAMS *ms_params, MB_MODE_INFO *mbmi,
    const FULLPEL_MV *const smv, int_mv *mv_refined, BLOCK_SIZE bsize);
#endif  // CONFIG_OPFL_MV_SEARCH

// motion search for joint MVD coding
int joint_mvd_search(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                     SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV ref_mv,
                     MV *start_mv, MV *bestmv, int *distortion,
                     unsigned int *sse1, int ref_idx, MV *other_mv,
                     MV *best_other_mv, uint16_t *second_pred,
                     InterPredParams *inter_pred_params,
                     int_mv *last_mv_search_list);

// motion search for 2/4/8 pel precision for joint MVD coding
int low_precision_joint_mvd_search(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                                   SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                   MV ref_mv, MV *start_mv, MV *bestmv,
                                   int *distortion, unsigned int *sse1,
                                   int ref_idx, MV *other_mv, MV *best_other_mv,
                                   uint16_t *second_pred,
                                   InterPredParams *inter_pred_params);

// motion search for near_new and new_near mode when adaptive MVD resolution is
// applied
int adaptive_mvd_search(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                        SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv,
                        MV *bestmv, int *distortion, unsigned int *sse1);

int av1_joint_amvd_motion_search(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                                 SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                 const MV *start_mv, MV *bestmv,
                                 int *distortion, unsigned int *sse1,
                                 int ref_idx, MV *other_mv, MV *best_other_mv,
                                 uint16_t *second_pred,
                                 InterPredParams *inter_pred_params);

void av1_make_default_subpel_ms_params(SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                       const struct AV1_COMP *cpi,
                                       const MACROBLOCK *x, BLOCK_SIZE bsize,
                                       const MV *ref_mv,
                                       const MvSubpelPrecision pb_mv_precision,
#if CONFIG_IBC_SUBPEL_PRECISION
                                       const int is_ibc_cost,
#endif  // CONFIG_IBC_SUBPEL_PRECISION
                                       const int *cost_list);

typedef int(fractional_mv_step_fp)(MACROBLOCKD *xd, const AV1_COMMON *const cm,
                                   const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                   MV start_mv, MV *bestmv, int *distortion,
                                   unsigned int *sse1,
                                   int_mv *last_mv_search_list);

extern fractional_mv_step_fp av1_find_best_sub_pixel_tree;
extern fractional_mv_step_fp av1_find_best_sub_pixel_tree_pruned;
extern fractional_mv_step_fp av1_find_best_sub_pixel_tree_pruned_more;
extern fractional_mv_step_fp av1_find_best_sub_pixel_tree_pruned_evenmore;
extern fractional_mv_step_fp av1_return_max_sub_pixel_mv;
extern fractional_mv_step_fp av1_return_min_sub_pixel_mv;
extern fractional_mv_step_fp av1_find_best_obmc_sub_pixel_tree_up;

#if CONFIG_IBC_SUBPEL_PRECISION
int upsampled_pref_error(MACROBLOCKD *xd, const AV1_COMMON *cm,
                         const MV *this_mv,
                         const SUBPEL_SEARCH_VAR_PARAMS *var_params,
                         unsigned int *sse);
int av1_find_best_sub_pixel_intraBC_dv(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
    int *distortion, unsigned int *sse1, FullMvLimits *full_pel_mv_limits,
    BLOCK_SIZE bsize);
// Refinement of MVs
int av1_refine_low_precision_intraBC_dv(
    MACROBLOCKD *xd, const AV1_COMMON *const cm,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params, MV start_mv, MV *bestmv,
    int *distortion, unsigned int *sse1, FullMvLimits *full_pel_mv_limits,
    BLOCK_SIZE bsize);
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#if CONFIG_WARP_PRECISION

// Struct to store coding info for fast warp search
typedef struct {
  WarpedMotionParams prev_wm_params;
  int is_valid;
  int step_size;
  MB_MODE_INFO mbmi_stats;
} warp_mode_info;

typedef struct {
  warp_mode_info warp_param_info[WARP_STATS_BUFFER_SIZE];
  int model_count;
  int model_start_idx;
} warp_mode_info_array;

void reset_warp_stats_buffer(warp_mode_info_array *warp_stats);
void update_warp_stats_buffer(const warp_mode_info *const this_warp_stats,
                              warp_mode_info_array *warp_stats);

#endif  // CONFIG_WARP_PRECISION
unsigned int av1_refine_warped_mv(MACROBLOCKD *xd, const AV1_COMMON *const cm,
                                  const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                  BLOCK_SIZE bsize, const int *pts0,
                                  const int *pts_inref0, int total_samples,
#if CONFIG_COMPOUND_WARP_CAUSAL
                                  int8_t ref,
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
                                  WARP_SEARCH_METHOD search_method,
                                  int num_iterations);
#if CONFIG_DERIVED_MVD_SIGN
uint8_t need_mv_adjustment(MACROBLOCKD *xd, const AV1_COMMON *const cm,
                           MACROBLOCK *const x, MB_MODE_INFO *mbmi,
                           BLOCK_SIZE bsize, MV *mv_diffs, MV *ref_mvs,
                           MvSubpelPrecision pb_mv_precision,
                           int *num_signaled_mvd, int *start_signaled_mvd_idx,
                           int *num_nonzero_mvd);
#endif  // CONFIG_DERIVED_MVD_SIGN

// Returns 1 if able to select a good model, 0 if not
int av1_pick_warp_delta(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                        MB_MODE_INFO *mbmi,
                        const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                        const ModeCosts *mode_costs,
#if CONFIG_WARP_PRECISION
                        warp_mode_info_array *prev_best_models,
#endif  // CONFIG_WARP_PRECISION
                        WARP_CANDIDATE *warp_param_stack);

int av1_refine_mv_for_base_param_warp_model(
    const AV1_COMMON *const cm, MACROBLOCKD *xd, MB_MODE_INFO *mbmi,
    const MB_MODE_INFO_EXT *mbmi_ext,
    const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
    WARP_SEARCH_METHOD search_method, int num_iterations);

void av1_refine_mv_for_warp_extend(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                   const SUBPEL_MOTION_SEARCH_PARAMS *ms_params,
                                   bool neighbor_is_above, BLOCK_SIZE bsize,
                                   const WarpedMotionParams *neighbor_params,
                                   WARP_SEARCH_METHOD search_method,
                                   int num_iterations);

static INLINE void av1_set_fractional_mv(int_mv *fractional_best_mv) {
  for (int z = 0; z < 3; z++) {
    fractional_best_mv[z].as_int = INVALID_MV;
  }
}

#if CONFIG_DERIVED_MVD_SIGN
static INLINE int is_valid_sign_mvd_single(const MV mvd,
                                           MvSubpelPrecision precision,
                                           const int is_adaptive_mvd,
                                           int th_for_num_nonzero) {
  (void)is_adaptive_mvd;
  int num_nonzero_mvd_comp = (mvd.row != 0) + (mvd.col != 0);
  if (num_nonzero_mvd_comp < th_for_num_nonzero) return 1;
  int precision_shift = MV_PRECISION_ONE_EIGHTH_PEL - precision;
  int last_sign = -1;
  int sum_mvd = 0;
  for (int comp = 0; comp < 2; comp++) {
    int this_mvd_comp = comp == 0 ? mvd.row : mvd.col;
    if (abs(this_mvd_comp) > MV_MAX) return 0;
    if (this_mvd_comp) {
      last_sign = (this_mvd_comp < 0);
      sum_mvd += (abs(this_mvd_comp) >> precision_shift);
    }
  }

  return (last_sign == (sum_mvd & 0x1));
}
#endif  // CONFIG_DERIVED_MVD_SIGN
// This function convert the mv value to the target precision
static INLINE int av1_lower_mv_limit(const int mv, const int shift) {
  int out = ((abs(mv) >> shift) << shift);
  return out * (mv < 0 ? -1 : 1);
}

static INLINE void av1_set_subpel_mv_search_range(
    SubpelMvLimits *subpel_limits, const FullMvLimits *mv_limits,
    const MV *ref_mv

    ,
    MvSubpelPrecision pb_mv_precision

) {
  //  We have to make sure the generated mv_limits
  //  are compatible with target precision.
  MV low_prec_ref_mv = *ref_mv;
#if CONFIG_C071_SUBBLK_WARPMV
  if (pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
    lower_mv_precision(&low_prec_ref_mv, pb_mv_precision);
  // sub_pel_prec_shift is the number of LSBs need to be 0 to make the
  // mv/mv_limit compatible
  const int sub_pel_prec_shift =
      (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision);

  const int max_mv =
      av1_lower_mv_limit(GET_MV_SUBPEL(MAX_FULL_PEL_VAL), sub_pel_prec_shift);

  int col_min =
      av1_lower_mv_limit(GET_MV_SUBPEL(mv_limits->col_min), sub_pel_prec_shift);
  int col_max =
      av1_lower_mv_limit(GET_MV_SUBPEL(mv_limits->col_max), sub_pel_prec_shift);
  int row_min =
      av1_lower_mv_limit(GET_MV_SUBPEL(mv_limits->row_min), sub_pel_prec_shift);
  int row_max =
      av1_lower_mv_limit(GET_MV_SUBPEL(mv_limits->row_max), sub_pel_prec_shift);

  int minc = AOMMAX(col_min, low_prec_ref_mv.col - max_mv);
  int maxc = AOMMIN(col_max, low_prec_ref_mv.col + max_mv);
  int minr = AOMMAX(row_min, low_prec_ref_mv.row - max_mv);
  int maxr = AOMMIN(row_max, low_prec_ref_mv.row + max_mv);

  maxc = AOMMAX(minc, maxc);
  maxr = AOMMAX(minr, maxr);

  subpel_limits->col_min = AOMMAX(MV_LOW + (1 << sub_pel_prec_shift), minc);
  subpel_limits->col_max = AOMMIN(MV_UPP - (1 << sub_pel_prec_shift), maxc);
  subpel_limits->row_min = AOMMAX(MV_LOW + (1 << sub_pel_prec_shift), minr);
  subpel_limits->row_max = AOMMIN(MV_UPP - (1 << sub_pel_prec_shift), maxr);
}

static INLINE void av1_set_tip_subpel_mv_search_range(
    SubpelMvLimits *subpel_limits, const FullMvLimits *mv_limits) {
  const int tmvp_mv = GET_MV_SUBPEL(TIP_MV_SEARCH_RANGE << TMVP_MI_SZ_LOG2);

  subpel_limits->col_min = AOMMAX(GET_MV_SUBPEL(mv_limits->col_min), -tmvp_mv);
  subpel_limits->col_max = AOMMIN(GET_MV_SUBPEL(mv_limits->col_max), tmvp_mv);
  subpel_limits->row_min = AOMMAX(GET_MV_SUBPEL(mv_limits->row_min), -tmvp_mv);
  subpel_limits->row_max = AOMMIN(GET_MV_SUBPEL(mv_limits->row_max), tmvp_mv);
}

static INLINE int av1_is_subpelmv_in_range(const SubpelMvLimits *mv_limits,
                                           MV mv) {
  return (mv.col >= mv_limits->col_min) && (mv.col <= mv_limits->col_max) &&
         (mv.row >= mv_limits->row_min) && (mv.row <= mv_limits->row_max);
}

#if CONFIG_IBC_SUBPEL_PRECISION
void get_default_ref_bv(int_mv *cur_ref_bv,
                        const FULLPEL_MOTION_SEARCH_PARAMS *fullms_params);
static INLINE void init_mv_cost_params(MV_COST_PARAMS *mv_cost_params,
                                       const MvCosts *mv_costs,
                                       int is_adaptive_mvd, const MV *ref_mv,
                                       MvSubpelPrecision pb_mv_precision
#if CONFIG_IBC_BV_IMPROVEMENT
                                       ,
                                       const int is_ibc_cost
#endif

) {
  mv_cost_params->ref_mv = ref_mv;
  mv_cost_params->full_ref_mv = get_fullmv_from_mv(ref_mv);
  mv_cost_params->mv_cost_type = MV_COST_ENTROPY;

  mv_cost_params->mv_costs = mv_costs;
  mv_cost_params->pb_mv_precision = pb_mv_precision;

  mv_cost_params->is_adaptive_mvd = is_adaptive_mvd;

#if CONFIG_IBC_BV_IMPROVEMENT
  mv_cost_params->is_ibc_cost = is_ibc_cost;
#endif
}

// Check if the MV is valid for IBC mode
static INLINE int is_sub_pel_bv_valid(const MV dv, const AV1_COMMON *cm,
                                      const MACROBLOCKD *xd, int mi_row,
                                      int mi_col, BLOCK_SIZE bsize,
                                      const SubpelMvLimits *sub_pel_mv_limits,
                                      const FullMvLimits *full_pel_mv_limits,
                                      MvSubpelPrecision pb_mv_precision) {
  return av1_is_fullmv_in_range(full_pel_mv_limits, get_fullmv_from_mv(&dv),
                                pb_mv_precision) &&
         av1_is_subpelmv_in_range(sub_pel_mv_limits, dv) &&
         av1_is_dv_valid(dv, cm, xd, mi_row, mi_col, bsize, cm->mib_size_log2);
}
#endif  // CONFIG_IBC_SUBPEL_PRECISION

// Returns the cost for signaling the index of compound weighted prediction
int av1_get_cwp_idx_cost(int8_t cwp_idx, const AV1_COMMON *const cm,
                         const MACROBLOCK *x);

#if CONFIG_IBC_BV_IMPROVEMENT
// Returns the cost of using the current mv during the motion search
int av1_get_mv_err_cost(const MV *mv, const MV_COST_PARAMS *mv_cost_params);

// Set the reference MV for the motion search
void av1_init_ref_mv(MV_COST_PARAMS *mv_cost_params, const MV *ref_mv);

// Compute the cost for signalling the intrabc DRL index
int av1_get_intrabc_drl_idx_cost(int max_ref_bv_num, int intrabc_drl_idx,
                                 const MACROBLOCK *x);

// Compute the cost for signalling the intrabc mode and intrabc DRL index. This
// is only used during the motion search
int av1_get_ref_bv_rate_cost(int intrabc_mode, int intrabc_drl_idx,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                             int max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                             MACROBLOCK *x, int errorperbit, int ref_bv_cnt);

// Pick the best reference BV for the current BV
int av1_pick_ref_bv(FULLPEL_MV *best_full_mv,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                    int max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                    const FULLPEL_MOTION_SEARCH_PARAMS *fullms_params);

// Compute the estimated RD cost for the reference BV
int av1_get_ref_mvpred_var_cost(const struct AV1_COMP *cpi,
                                const MACROBLOCKD *xd,
                                const FULLPEL_MOTION_SEARCH_PARAMS *ms_params);
#endif  // CONFIG_IBC_BV_IMPROVEMENT

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_MCOMP_H_
