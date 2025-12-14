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

#ifndef AVM_AV2_ENCODER_RATECTRL_H_
#define AVM_AV2_ENCODER_RATECTRL_H_

#include "avm/avm_codec.h"
#include "avm/avm_integer.h"

#include "avm_ports/mem.h"

#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!\cond */

// Bits Per MB at different Q (Multiplied by 512)
#define BPER_MB_NORMBITS 9

// Use this macro to turn on/off use of alt-refs in one-pass mode.
#define USE_ALTREF_FOR_ONE_PASS 1

// Threshold used to define if a KF group is static (e.g. a slide show).
// Essentially, this means that no frame in the group has more than 1% of MBs
// that are not marked as coded with 0,0 motion in the first pass.
#define STATIC_KF_GROUP_THRESH 99
#define STATIC_KF_GROUP_FLOAT_THRESH 0.99

// The maximum duration of a GF group that is static (e.g. a slide show).
#define MAX_STATIC_GF_GROUP_LENGTH 250

#define MIN_GF_INTERVAL 4
#define MAX_GF_INTERVAL 32
#define MAX_GF_LENGTH_LAP MAX_GF_INTERVAL

#define MAX_NUM_GF_INTERVALS 15

#define MAX_ARF_LAYERS 6

enum {
  INTER_NORMAL,
  GF_ARF_LOW,
  GF_ARF_STD,
  KF_STD,
  RATE_FACTOR_LEVELS
} UENUM1BYTE(RATE_FACTOR_LEVEL);

enum {
  KF_UPDATE,
  LF_UPDATE,
  GF_UPDATE,
  ARF_UPDATE,
  OVERLAY_UPDATE,
  INTNL_OVERLAY_UPDATE,  // Internal Overlay Frame
  INTNL_ARF_UPDATE,      // Internal Altref Frame
  KFFLT_UPDATE,          // Filtered KF w/ overlay update
  KFFLT_OVERLAY_UPDATE,  // Overlay frame for filtered keyframe
  FRAME_UPDATE_TYPES
} UENUM1BYTE(FRAME_UPDATE_TYPE);

typedef enum { ORIG = 0, THREE_QUARTER = 1, ONE_HALF = 2 } RESIZE_STATE;

/*!\endcond */
/*!
 * \brief  Rate Control parameters and status
 */
typedef struct {
  // Rate targetting variables

  /*!
   * Baseline target rate for frame before adjustment for previous under or
   * over shoot.
   */
  int base_frame_target;
  /*!
   * Target rate for frame after adjustment for previous under or over shoot.
   */
  int this_frame_target;  // Actual frame target after rc adjustment.

  /*!
   * Target bit budget for the current GF / ARF group of frame.
   */
  int64_t gf_group_bits;

  /*!
   * Projected size for current frame
   */
  int projected_frame_size;

  /*!
   * Super block rate target used with some adaptive quantization strategies.
   */
  int sb64_target_rate;

  /*!
   * Q used on last encoded frame of the given type.
   */
  int last_q[FRAME_TYPES];

  /*!
   * Q used for last boosted (non leaf) frame (GF/KF/ARF)
   */
  int last_boosted_qindex;

  /*!
   * Q used for last boosted (non leaf) frame
   */
  int last_kf_qindex;

  /*!
   * Boost factor used to calculate the extra bits allocated to ARFs and GFs
   */
  int gfu_boost;
  /*!
   * Boost factor used to calculate the extra bits allocated to the key frame
   */
  int kf_boost;

  /*!
   * Correction factors used to adjust the q estimate for a given target rate
   * in the encode loop.
   */
  double rate_correction_factors[RATE_FACTOR_LEVELS];

  /*!
   * Number of frames since the last ARF / GF.
   */
  int frames_since_golden;

  /*!
   * Number of frames till the next ARF / GF is due.
   */
  int frames_till_gf_update_due;

  /*!
   * Number of determined gf groups left
   */
  int intervals_till_gf_calculate_due;

  /*!
   * Stores the determined gf group lengths for a set of gf groups
   */
  int gf_intervals[MAX_NUM_GF_INTERVALS];

  /*!
   * The current group's index into gf_intervals[]
   */
  int cur_gf_index;

  /*!\cond */
  int min_gf_interval;
  int max_gf_interval;
  int static_scene_max_gf_interval;
  int baseline_gf_interval;
  int constrained_gf_group;
  /*!\endcond */
  /*!
   * Frames before the next key frame
   */
  int frames_to_key;
  /*!\cond */
  int frames_since_key;
  int this_key_frame_forced;
  int next_key_frame_forced;
  int is_src_frame_alt_ref;
  int sframe_due;

  int high_source_sad;
  uint64_t avg_source_sad;

  int avg_frame_bandwidth;  // Average frame size target for clip
  int min_frame_bandwidth;  // Minimum allocation used for any frame
  int max_frame_bandwidth;  // Maximum burst rate allowed for a frame.
  int prev_avg_frame_bandwidth;

  int ni_av_qi;
  int ni_tot_qi;
  int ni_frames;
  int avg_frame_qindex[FRAME_TYPES];
  double tot_q;
  double avg_q;

  int64_t buffer_level;
  int64_t bits_off_target;
  int64_t vbr_bits_off_target;
  int64_t vbr_bits_off_target_fast;

  int decimation_factor;
  int decimation_count;

  int rolling_target_bits;
  int rolling_actual_bits;

  int long_rolling_target_bits;
  int long_rolling_actual_bits;

  int rate_error_estimate;

  int64_t total_actual_bits;
  int64_t total_target_bits;
  int64_t total_target_vs_actual;

  /*!\endcond */
  /*!
   * User specified maximum Q allowed for current frame
   */
  int worst_quality;
  /*!
   * User specified minimum Q allowed for current frame
   */
  int best_quality;

  /*!
   * Initial buffuer level in ms for CBR / low delay encoding
   */
  int64_t starting_buffer_level;
  /*!
   * Optimum / target buffuer level in ms for CBR / low delay encoding
   */
  int64_t optimal_buffer_level;
  /*!
   * Maximum target buffuer level in ms for CBR / low delay encoding
   */
  int64_t maximum_buffer_size;
  /*!\cond */

  // rate control history for last frame(1) and the frame before(2).
  // -1: undershot
  //  1: overshoot
  //  0: not initialized.
  int rc_1_frame;
  int rc_2_frame;
  int q_1_frame;
  int q_2_frame;

  float_t arf_boost_factor;

  /*!\endcond */
  /*!
   * Q index used for frame(s) at pyramid level 1, such as an ALTREF frame.
   */
  int level1_qp;
  /*!
   * Proposed maximum alloed Q for current frame
   */
  int active_worst_quality;
  /*!
   * Proposed minimum allowed Q different layers in a coding pyramid
   */
  int active_best_quality[MAX_ARF_LAYERS + 1];

  /*!\cond */
  int base_layer_qp;

  // Total number of stats used only for kf_boost calculation.
  int num_stats_used_for_kf_boost;
  // Total number of stats used only for gfu_boost calculation.
  int num_stats_used_for_gfu_boost;
  // Total number of stats required by gfu_boost calculation.
  int num_stats_required_for_gfu_boost;
  int next_is_fwd_key;
  int enable_scenecut_detection;
  int use_arf_in_this_kf_group;
  // Track amount of low motion in scene
  int avg_frame_low_motion;

  // For dynamic resize, 1 pass cbr.
  RESIZE_STATE resize_state;
  int resize_avg_qp;
  int resize_buffer_underflow;
  int resize_count;
  /*!\endcond */
} RATE_CONTROL;

/*!\cond */

struct AV2_COMP;
struct AV2EncoderConfig;

void av2_rc_init(const struct AV2EncoderConfig *oxcf, int pass,
                 RATE_CONTROL *rc);

int av2_estimate_bits_at_q(FRAME_TYPE frame_kind, int q, int mbs,
                           double correction_factor, avm_bit_depth_t bit_depth,
                           const int is_screen_content_type);

double av2_convert_qindex_to_q(int qindex, avm_bit_depth_t bit_depth);

void av2_rc_init_minq_luts(void);

int av2_rc_get_default_min_gf_interval(int width, int height, double framerate);
// Note av2_rc_get_default_max_gf_interval() requires the min_gf_interval to
// be passed in to ensure that the max_gf_interval returned is at least as bis
// as that.
int av2_rc_get_default_max_gf_interval(double framerate, int min_gf_interval);

// Generally at the high level, the following flow is expected
// to be enforced for rate control:
// Call encode_frame_to_data_rate() to perform the
// actual encode. This function will in turn call encode_frame()
// one or more times, followed by one of:
//   av2_rc_postencode_update()
//   av2_rc_postencode_update_drop_frame()
//
// The majority of rate control parameters are only expected
// to be set in the av2_rc_get_..._params() functions and
// updated during the av2_rc_postencode_update...() functions.
// The only exceptions are av2_rc_drop_frame() and
// av2_rc_update_rate_correction_factors() functions.

// Functions to set parameters for encoding before the actual
// encode_frame_to_data_rate() function.
struct EncodeFrameParams;

// Post encode update of the rate control parameters based
// on bytes used
void av2_rc_postencode_update(struct AV2_COMP *cpi, uint64_t bytes_used);
// Post encode update of the rate control parameters for dropped frames
void av2_rc_postencode_update_drop_frame(struct AV2_COMP *cpi);

/*!\endcond */
/*!\brief Updates the rate correction factor linking Q to output bits
 *
 * This function updates the Q rate correction factor after an encode
 * cycle depending on whether we overshot or undershot the target rate.
 *
 * \ingroup rate_control
 * \param[in]   cpi                   Top level encoder instance structure
 * \param[in]   width                 Frame width
 * \param[in]   height                Frame height
 *
 * No return value but updates the relevant rate correction factor in cpi->rc
 */
void av2_rc_update_rate_correction_factors(struct AV2_COMP *cpi, int width,
                                           int height);
/*!\cond */

// Decide if we should drop this frame: For 1-pass CBR.
// Changes only the decimation count in the rate control structure
int av2_rc_drop_frame(struct AV2_COMP *cpi);

// Computes frame size bounds.
void av2_rc_compute_frame_size_bounds(const struct AV2_COMP *cpi,
                                      int this_frame_target,
                                      int *frame_under_shoot_limit,
                                      int *frame_over_shoot_limit);

/*!\endcond */

/*!\brief Picks q and q bounds given the rate control parameters in \c cpi->rc.
 *
 * \ingroup rate_control
 * \param[in]       cpi          Top level encoder structure
 * \param[in,out]   rc           Top level rate control structure
 * \param[in]       width        Coded frame width
 * \param[in]       height       Coded frame height
 * \param[in]       gf_index     Index of this frame in the golden frame group
 * \param[out]      bottom_index Bottom bound for q index (best quality)
 * \param[out]      top_index    Top bound for q index (worst quality)
 * \return Returns selected q index to be used for encoding this frame.
 * Also, updates \c rc->level1_qp.
 */
int av2_rc_pick_q_and_bounds(const struct AV2_COMP *cpi, RATE_CONTROL *rc,
                             int width, int height, int gf_index,
                             int *bottom_index, int *top_index);

/*!\brief Estimates q to achieve a target bits per frame
 *
 * \ingroup rate_control
 * \param[in]   cpi                   Top level encoder instance structure
 * \param[in]   target_bits_per_frame Frame rate target
 * \param[in]   active_worst_quality  Max Q allowed
 * \param[in]   active_best_quality   Min Q allowed
 * \param[in]   width                 Frame width
 * \param[in]   height                Frame height
 *
 * \return Returns a q index value
 */
int av2_rc_regulate_q(const struct AV2_COMP *cpi, int target_bits_per_frame,
                      int active_best_quality, int active_worst_quality,
                      int width, int height);

/*!\cond */
// Estimates bits per mb for a given qindex and correction factor.
int av2_rc_bits_per_mb(FRAME_TYPE frame_type, int qindex,
                       double correction_factor, avm_bit_depth_t bit_depth,
                       const int is_screen_content_type);

// Clamping utilities for bitrate targets for iframes and pframes.
int av2_rc_clamp_iframe_target_size(const struct AV2_COMP *const cpi,
                                    int target);
int av2_rc_clamp_pframe_target_size(const struct AV2_COMP *const cpi,
                                    int target, uint8_t frame_update_type);

// Find q_index corresponding to desired_q, within [best_qindex, worst_qindex].
// To be precise, 'q_index' is the smallest integer, for which the corresponding
// q >= desired_q.
// If no such q index is found, returns 'worst_qindex'.
int av2_find_qindex(double desired_q, avm_bit_depth_t bit_depth,
                    int best_qindex, int worst_qindex);

// Computes a q delta (in "q index" terms) to get from a starting q value
// to a target q value
int av2_compute_qdelta(const RATE_CONTROL *rc, double qstart, double qtarget,
                       avm_bit_depth_t bit_depth);

// Computes a q delta (in "q index" terms) to get from a starting q value
// to a value that should equate to the given rate ratio.
int av2_compute_qdelta_by_rate(const RATE_CONTROL *rc, FRAME_TYPE frame_type,
                               int qindex, double rate_target_ratio,
                               const int is_screen_content_type,
                               avm_bit_depth_t bit_depth);

int av2_frame_type_qdelta(const struct AV2_COMP *cpi, int q);

void av2_rc_update_framerate(struct AV2_COMP *cpi, int width, int height);

void av2_rc_set_gf_interval_range(const struct AV2_COMP *const cpi,
                                  RATE_CONTROL *const rc);

void av2_set_target_rate(struct AV2_COMP *cpi, int width, int height);

void av2_rc_set_frame_target(struct AV2_COMP *cpi, int target, int width,
                             int height);

/*!\endcond */
/*!\brief Calculates how many bits to use for a P frame in one pass vbr
 *
 * \ingroup rate_control
 * \callgraph
 * \callergraph
 *
 * \param[in]       cpi                 Top level encoder structure
 * \param[in]       frame_update_type   Type of frame
 *
 * \return	Returns the target number of bits for this frame.
 */
int av2_calc_pframe_target_size_one_pass_vbr(
    const struct AV2_COMP *const cpi, FRAME_UPDATE_TYPE frame_update_type);

/*!\brief Calculates how many bits to use for an i frame in one pass vbr
 *
 * \ingroup rate_control
 * \callgraph
 * \callergraph
 *
 * \param[in]       cpi  Top level encoder structure
 *
 * \return	Returns the target number of bits for this frame.
 */
int av2_calc_iframe_target_size_one_pass_vbr(const struct AV2_COMP *const cpi);

/*!\brief Calculates how many bits to use for a P frame in one pass cbr
 *
 * \ingroup rate_control
 * \callgraph
 * \callergraph
 *
 * \param[in]       cpi                 Top level encoder structure
 * \param[in]       frame_update_type   Type of frame
 *
 * \return  Returns the target number of bits for this frame.
 */
int av2_calc_pframe_target_size_one_pass_cbr(
    const struct AV2_COMP *cpi, FRAME_UPDATE_TYPE frame_update_type);

/*!\brief Calculates how many bits to use for an i frame in one pass cbr
 *
 * \ingroup rate_control
 * \callgraph
 * \callergraph
 *
 * \param[in]       cpi  Top level encoder structure
 *
 * \return  Returns the target number of bits for this frame.
 */
int av2_calc_iframe_target_size_one_pass_cbr(const struct AV2_COMP *cpi);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_RATECTRL_H_
