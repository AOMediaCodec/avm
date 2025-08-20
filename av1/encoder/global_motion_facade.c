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

#include "aom_dsp/binary_codes_writer.h"
#include "aom_ports/system_state.h"

#include "aom_dsp/flow_estimation/corner_detect.h"
#include "aom_dsp/flow_estimation/flow_estimation.h"
#include "aom_dsp/pyramid.h"

#include "av1/common/mv.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/global_motion_facade.h"
#include "av1/encoder/rdopt.h"
#include "av1/common/warped_motion.h"

// Range of model types to search
#define FIRST_GLOBAL_TRANS_TYPE ROTZOOM
#define LAST_GLOBAL_TRANS_TYPE ROTZOOM

// Computes the cost for the warp parameters.
static int gm_get_params_cost(const WarpedMotionParams *gm,
                              const WarpedMotionParams *ref_gm,
                              MvSubpelPrecision precision) {
  const int precision_loss = get_gm_precision_loss(precision);
#if CONFIG_IMPROVED_GLOBAL_MOTION
  (void)precision_loss;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
  int params_cost = 0;
#if CONFIG_IMPROVED_GLOBAL_MOTION
  const int trans_bits = GM_ABS_TRANS_BITS;
  const int trans_prec_diff = GM_TRANS_PREC_DIFF;
  const int trans_max = (1 << trans_bits) - 1;
#else
  const int trans_bits = (gm->wmtype == TRANSLATION)
                             ? GM_ABS_TRANS_ONLY_BITS - precision_loss
                             : GM_ABS_TRANS_BITS;
  const int trans_prec_diff = (gm->wmtype == TRANSLATION)
                                  ? GM_TRANS_ONLY_PREC_DIFF + precision_loss
                                  : GM_TRANS_PREC_DIFF;
  const int trans_max = (1 << trans_bits);
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

  switch (gm->wmtype) {
    case AFFINE:
    case ROTZOOM:
      params_cost += aom_count_signed_primitive_refsubexpfin(
          GM_ALPHA_MAX + 1, SUBEXPFIN_K,
          (ref_gm->wmmat[2] >> GM_ALPHA_PREC_DIFF) - (1 << GM_ALPHA_PREC_BITS),
          (gm->wmmat[2] >> GM_ALPHA_PREC_DIFF) - (1 << GM_ALPHA_PREC_BITS));
      params_cost += aom_count_signed_primitive_refsubexpfin(
          GM_ALPHA_MAX + 1, SUBEXPFIN_K,
          (ref_gm->wmmat[3] >> GM_ALPHA_PREC_DIFF),
          (gm->wmmat[3] >> GM_ALPHA_PREC_DIFF));
      if (gm->wmtype >= AFFINE) {
        params_cost += aom_count_signed_primitive_refsubexpfin(
            GM_ALPHA_MAX + 1, SUBEXPFIN_K,
            (ref_gm->wmmat[4] >> GM_ALPHA_PREC_DIFF),
            (gm->wmmat[4] >> GM_ALPHA_PREC_DIFF));
        params_cost += aom_count_signed_primitive_refsubexpfin(
            GM_ALPHA_MAX + 1, SUBEXPFIN_K,
            (ref_gm->wmmat[5] >> GM_ALPHA_PREC_DIFF) -
                (1 << GM_ALPHA_PREC_BITS),
            (gm->wmmat[5] >> GM_ALPHA_PREC_DIFF) - (1 << GM_ALPHA_PREC_BITS));
      }
      params_cost += aom_count_signed_primitive_refsubexpfin(
          trans_max + 1, SUBEXPFIN_K, (ref_gm->wmmat[0] >> trans_prec_diff),
          (gm->wmmat[0] >> trans_prec_diff));
      params_cost += aom_count_signed_primitive_refsubexpfin(
          trans_max + 1, SUBEXPFIN_K, (ref_gm->wmmat[1] >> trans_prec_diff),
          (gm->wmmat[1] >> trans_prec_diff));
      AOM_FALLTHROUGH_INTENDED;
    case IDENTITY: break;
    default: assert(0);
  }
  return (params_cost << AV1_PROB_COST_SHIFT);
}

// For the given reference frame, computes the global motion parameters for
// different motion models and finds the best.
#if CONFIG_IMPROVED_GLOBAL_MOTION
static AOM_INLINE void compute_global_motion_for_ref_frame(
    AV1_COMP *cpi, struct aom_internal_error_info *error_info,
    YV12_BUFFER_CONFIG *ref_buf[INTER_REFS_PER_FRAME], int frame,
    MotionModel *motion_models, uint8_t *segment_map, const int segment_map_w,
    const int segment_map_h) {
#else
static AOM_INLINE void compute_global_motion_for_ref_frame(
    AV1_COMP *cpi, struct aom_internal_error_info *error_info,
    YV12_BUFFER_CONFIG *ref_buf[INTER_REFS_PER_FRAME], int frame,
    MotionModel *motion_models, uint8_t *segment_map, const int segment_map_w,
    const int segment_map_h, const WarpedMotionParams *ref_params) {
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
  AV1_COMMON *const cm = &cpi->common;
#if CONFIG_IMPROVED_GLOBAL_MOTION
  GlobalMotionInfo *const gm_info = &cpi->gm_info;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

  int src_width = cpi->source->y_crop_width;
  int src_height = cpi->source->y_crop_height;
  int src_stride = cpi->source->y_stride;

  assert(ref_buf[frame] != NULL);

  aom_clear_system_state();

  int bit_depth = cpi->common.seq_params.bit_depth;
  GlobalMotionMethod global_motion_method = default_global_motion_method;
  int downsample_level = cpi->sf.gm_sf.downsample_level;
  int num_refinements = cpi->sf.gm_sf.num_refinement_steps;
  bool mem_alloc_failed = false;

  // Select the best model based on fractional error reduction.
  // By initializing this to erroradv_tr, the same logic which is used to
  // select the best model will automatically filter out any model which
  // doesn't meet the required quality threshold
  double best_erroradv = erroradv_tr;
  for (TransformationType model = FIRST_GLOBAL_TRANS_TYPE;
       model <= LAST_GLOBAL_TRANS_TYPE; ++model) {
    if (!aom_compute_global_motion(model, cpi->source, ref_buf[frame],
                                   bit_depth, global_motion_method,
                                   downsample_level, motion_models,
                                   RANSAC_NUM_MOTIONS, &mem_alloc_failed)) {
      if (mem_alloc_failed) {
        aom_internal_error(error_info, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate global motion buffers");
      }
      continue;
    }
#if CONFIG_ACROSS_SCALE_WARP
    cm->global_motion[frame].invalid = 0;
#endif  // CONFIG_ACROSS_SCALE_WARP
    for (int i = 0; i < RANSAC_NUM_MOTIONS; ++i) {
      if (motion_models[i].num_inliers == 0) continue;

      WarpedMotionParams tmp_wm_params;
      memcpy(tmp_wm_params.wmmat, motion_models[i].params,
             MAX_PARAMDIM * sizeof(*tmp_wm_params.wmmat));
      tmp_wm_params.wmtype = get_wmtype(&tmp_wm_params);
      tmp_wm_params.invalid = 0;

      // Check that the generated model is warp-able
      if (!av1_get_shear_params(&tmp_wm_params
#if CONFIG_ACROSS_SCALE_WARP
                                ,
                                get_ref_scale_factors_const(cm, frame)
#endif  // CONFIG_ACROSS_SCALE_WARP
                                    ))
        continue;

      // Skip models that we won't use (IDENTITY or TRANSLATION)
      //
      // For IDENTITY type models, we don't need to evaluate anything because
      // all the following logic is effectively comparing the estimated model
      // to an identity model.
      //
      // For TRANSLATION type global motion models, gm_get_motion_vector() gives
      // the wrong motion vector (see comments in that function for details).
      // As translation-type models do not give much gain, we can avoid this bug
      // by never choosing a TRANSLATION type model
      if (tmp_wm_params.wmtype <= TRANSLATION) continue;

      av1_compute_feature_segmentation_map(
          segment_map, segment_map_w, segment_map_h, motion_models[i].inliers,
          motion_models[i].num_inliers);

      int64_t ref_frame_error = av1_segmented_frame_error(
          bit_depth, ref_buf[frame]->y_buffer, ref_buf[frame]->y_stride,
          cpi->source->y_buffer, src_stride, src_width, src_height, segment_map,
          segment_map_w);

      if (ref_frame_error == 0) continue;

      const int64_t warp_error = av1_refine_integerized_param(
          &tmp_wm_params, tmp_wm_params.wmtype, bit_depth,
          ref_buf[frame]->y_buffer, ref_buf[frame]->y_crop_width,
          ref_buf[frame]->y_crop_height, ref_buf[frame]->y_stride,
          cpi->source->y_buffer, src_width, src_height, src_stride,
          num_refinements, ref_frame_error, segment_map, segment_map_w
#if CONFIG_ACROSS_SCALE_WARP
          ,
          get_ref_scale_factors_const(cm, frame)
#endif  // CONFIG_ACROSS_SCALE_WARP
      );

      // av1_refine_integerized_param() can return a simpler model type than
      // its input, so re-check model type here
      if (tmp_wm_params.wmtype <= TRANSLATION) continue;

      double erroradvantage = (double)warp_error / ref_frame_error;

#if !CONFIG_IMPROVED_GLOBAL_MOTION
      // Check that the model signaling cost is not too high
      if (!av1_is_enough_erroradvantage(
              erroradvantage,
              gm_get_params_cost(&tmp_wm_params, ref_params,
                                 cm->features.fr_mv_precision))) {
        continue;
      }
#endif  // !CONFIG_IMPROVED_GLOBAL_MOTION

      if (erroradvantage < best_erroradv) {
        best_erroradv = erroradvantage;
        // Save the wm_params modified by
        // av1_refine_integerized_param() rather than motion index to
        // avoid rerunning refine() below.
        memcpy(&(cm->global_motion[frame]), &tmp_wm_params,
               sizeof(WarpedMotionParams));
      }
    }
  }
#if CONFIG_IMPROVED_GLOBAL_MOTION
  gm_info->erroradvantage[frame] = best_erroradv;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
  aom_clear_system_state();
}

// Computes global motion for the given reference frame.
void av1_compute_gm_for_valid_ref_frames(
    AV1_COMP *cpi, struct aom_internal_error_info *error_info,
    YV12_BUFFER_CONFIG *ref_buf[INTER_REFS_PER_FRAME], int frame,
    MotionModel *motion_models, uint8_t *segment_map, int segment_map_w,
    int segment_map_h) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
  compute_global_motion_for_ref_frame(cpi, error_info, ref_buf, frame,
                                      motion_models, segment_map, segment_map_w,
                                      segment_map_h);
#else
  AV1_COMMON *const cm = &cpi->common;
  const WarpedMotionParams *ref_params =
      cm->prev_frame ? &cm->prev_frame->global_motion[frame]
                     : &default_warp_params;

  compute_global_motion_for_ref_frame(cpi, error_info, ref_buf, frame,
                                      motion_models, segment_map, segment_map_w,
                                      segment_map_h, ref_params);
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
}

// Loops over valid reference frames and computes global motion estimation.
static AOM_INLINE void compute_global_motion_for_references(
    AV1_COMP *cpi, YV12_BUFFER_CONFIG *ref_buf[INTER_REFS_PER_FRAME],
    FrameDistPair reference_frame[INTER_REFS_PER_FRAME], int num_ref_frames,
    MotionModel *motion_models, uint8_t *segment_map, const int segment_map_w,
    const int segment_map_h) {
  AV1_COMMON *const cm = &cpi->common;
  struct aom_internal_error_info *const error_info =
      cpi->td.mb.e_mbd.error_info;
  // Compute global motion w.r.t. reference frames starting from the nearest ref
  // frame in a given direction.
  for (int frame = 0; frame < num_ref_frames; frame++) {
    int ref_frame = reference_frame[frame].frame;
    av1_compute_gm_for_valid_ref_frames(cpi, error_info, ref_buf, ref_frame,
                                        motion_models, segment_map,
                                        segment_map_w, segment_map_h);
    // If global motion w.r.t. current ref frame is
    // INVALID/TRANSLATION/IDENTITY, skip the evaluation of global motion w.r.t
    // the remaining ref frames in that direction. The below exit is disabled
    // when ref frame distance w.r.t. current frame is zero. E.g.:
    // source_alt_ref_frame w.r.t. ARF frames.
    if (cpi->sf.gm_sf.prune_ref_frame_for_gm_search &&
        reference_frame[frame].distance != 0 &&
        cm->global_motion[ref_frame].wmtype <= TRANSLATION)
      break;
  }
}

// Compares the distance in 'a' and 'b'. Returns 1 if the frame corresponding to
// 'a' is farther, -1 if the frame corresponding to 'b' is farther, 0 otherwise.
static int compare_distance(const void *a, const void *b) {
  const int diff =
      ((FrameDistPair *)a)->distance - ((FrameDistPair *)b)->distance;
  if (diff > 0)
    return 1;
  else if (diff < 0)
    return -1;
  return 0;
}

static int disable_gm_search_based_on_stats(const AV1_COMP *const cpi) {
  const GF_GROUP *gf_group = &cpi->gf_group;
  int is_gm_present = 1;

  // Check number of GM models only in GF groups with ARF frames. GM param
  // estimation is always done in the case of GF groups with no ARF frames (flat
  // gops)
  if (gf_group->arf_index > -1) {
    // valid_gm_model_found is initialized to INT32_MAX in the beginning of
    // every GF group.
    // Therefore, GM param estimation is always done for all frames until
    // at least 1 frame each of ARF_UPDATE, INTNL_ARF_UPDATE and LF_UPDATE are
    // encoded in a GF group For subsequent frames, GM param estimation is
    // disabled, if no valid models have been found in all the three update
    // types.
    is_gm_present = (cpi->valid_gm_model_found[ARF_UPDATE] != 0) ||
                    (cpi->valid_gm_model_found[INTNL_ARF_UPDATE] != 0) ||
                    (cpi->valid_gm_model_found[LF_UPDATE] != 0);
  }
  return !is_gm_present;
}

// Populates valid reference frames in past/future directions in
// 'reference_frames' and their count in 'num_ref_filters'.
static AOM_INLINE void update_valid_ref_frames_for_gm(
    AV1_COMP *cpi, YV12_BUFFER_CONFIG *ref_buf[INTER_REFS_PER_FRAME],
    FrameDistPair reference_frames[MAX_DIRECTIONS][INTER_REFS_PER_FRAME],
    int *num_ref_frames) {
  AV1_COMMON *const cm = &cpi->common;
  int *num_past_ref_frames = &num_ref_frames[0];
  int *num_future_ref_frames = &num_ref_frames[1];
  const GF_GROUP *gf_group = &cpi->gf_group;
  int ref_pruning_enabled = is_frame_eligible_for_ref_pruning(
      gf_group, cpi->sf.inter_sf.selective_ref_frame, 1, gf_group->index);
  int cur_frame_gm_disabled = 0;
  int pyr_lvl = cm->cur_frame->pyramid_level;

  if (cpi->sf.gm_sf.disable_gm_search_based_on_stats) {
    cur_frame_gm_disabled = disable_gm_search_based_on_stats(cpi);
  }

  for (int frame = cm->ref_frames_info.num_total_refs - 1; frame >= 0;
       --frame) {
    const MV_REFERENCE_FRAME ref_frame[2] = { frame, NONE_FRAME };
    assert(frame <= INTER_REFS_PER_FRAME);
    const int ref_disabled = !(cm->ref_frame_flags & (1 << frame));
    ref_buf[frame] = NULL;
    cm->global_motion[frame] = default_warp_params;
    RefCntBuffer *buf = get_ref_frame_buf(cm, frame);
    // Skip global motion estimation for invalid ref frames
    if (buf == NULL ||
        (ref_disabled && cpi->sf.hl_sf.recode_loop != DISALLOW_RECODE)) {
      continue;
    } else {
#if CONFIG_ACROSS_SCALE_WARP
      // Get the scaled buffer
      if (av1_is_scaled(get_ref_scale_factors(cm, frame)))
        ref_buf[frame] = &cpi->scaled_ref_buf[frame]->buf;
      else
        ref_buf[frame] = &buf->buf;
#else
      ref_buf[frame] = &buf->buf;
#endif  // CONFIG_ACROSS_SCALE_WARP
    }

    int prune_ref_frames =
        ref_pruning_enabled &&
        prune_ref_by_selective_ref_frame(cpi, NULL, ref_frame);
    int ref_pyr_lvl = buf->pyramid_level;

    if (ref_buf[frame]->y_crop_width == cpi->source->y_crop_width &&
        ref_buf[frame]->y_crop_height == cpi->source->y_crop_height &&
        frame < cpi->sf.gm_sf.max_ref_frames && !prune_ref_frames &&
        ref_pyr_lvl <= pyr_lvl && !cur_frame_gm_disabled) {
      assert(ref_buf[frame] != NULL);
      const int relative_frame_dist = av1_encoder_get_relative_dist(
          buf->display_order_hint, cm->cur_frame->display_order_hint);
      // Populate past and future ref frames.
      // reference_frames[0][] indicates past direction and
      // reference_frames[1][] indicates future direction.
      if (relative_frame_dist <= 0) {
        reference_frames[0][*num_past_ref_frames].distance =
            abs(relative_frame_dist);
        reference_frames[0][*num_past_ref_frames].frame = frame;
        (*num_past_ref_frames)++;
      } else {
        reference_frames[1][*num_future_ref_frames].distance =
            abs(relative_frame_dist);
        reference_frames[1][*num_future_ref_frames].frame = frame;
        (*num_future_ref_frames)++;
      }
    }
  }
}

// Allocates and initializes memory for segment_map and MotionModel.
static AOM_INLINE void alloc_global_motion_data(MotionModel *motion_models,
                                                uint8_t **segment_map,
                                                const int segment_map_w,
                                                const int segment_map_h) {
  for (int m = 0; m < RANSAC_NUM_MOTIONS; m++) {
    av1_zero(motion_models[m]);
    motion_models[m].inliers =
        aom_malloc(sizeof(*(motion_models[m].inliers)) * 2 * MAX_CORNERS);
  }

  *segment_map = (uint8_t *)aom_malloc(sizeof(*segment_map) * segment_map_w *
                                       segment_map_h);
  av1_zero_array(*segment_map, segment_map_w * segment_map_h);
}

// Deallocates segment_map and inliers.
static AOM_INLINE void dealloc_global_motion_data(MotionModel *motion_models,
                                                  uint8_t *segment_map) {
  aom_free(segment_map);

  for (int m = 0; m < RANSAC_NUM_MOTIONS; m++) {
    aom_free(motion_models[m].inliers);
  }
}

#if CONFIG_IMPROVED_GLOBAL_MOTION
// Select which global motion model to use as a base
static AOM_INLINE void pick_base_gm_params(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  GlobalMotionInfo *const gm_info = &cpi->gm_info;
  int num_total_refs = cm->ref_frames_info.num_total_refs;

  int best_our_ref;
  int best_their_ref;
  const WarpedMotionParams *best_base_model;
  int best_temporal_distance;
  int best_num_models;
  int best_cost;

  // Bitmask of which models we will actually use if we accept the current
  // best base model
  uint8_t best_enable_models;

  // First, evaluate the identity model as a base
  {
    int this_num_models = 0;
    int this_cost =
        aom_count_primitive_quniform(num_total_refs + 1, num_total_refs)
        << AV1_PROB_COST_SHIFT;
    uint8_t this_enable_models = 0;

    for (int frame = 0; frame < num_total_refs; frame++) {
      const WarpedMotionParams *model = &cm->global_motion[frame];
      if (model->wmtype == IDENTITY) continue;
      int model_cost = gm_get_params_cost(model, &default_warp_params,
                                          cm->features.fr_mv_precision);
      bool use_model = av1_is_enough_erroradvantage(
          gm_info->erroradvantage[frame], model_cost);

      if (use_model) {
        this_num_models += 1;
        this_cost += model_cost;
        this_enable_models |= (1 << frame);
      }
    }

    // Set initial values
    best_our_ref = cm->ref_frames_info.num_total_refs;
    best_their_ref = -1;
    best_base_model = &default_warp_params;
    best_temporal_distance = 1;
    best_num_models = this_num_models;
    best_cost = this_cost;
    best_enable_models = this_enable_models;
  }

  // Then try each available reference model in turn
  for (int our_ref = 0; our_ref < num_total_refs; ++our_ref) {
    const int ref_disabled = !(cm->ref_frame_flags & (1 << our_ref));
    RefCntBuffer *buf = get_ref_frame_buf(cm, our_ref);
    // Skip looking at invalid ref frames
    if (buf == NULL ||
        (ref_disabled && cpi->sf.hl_sf.recode_loop != DISALLOW_RECODE)) {
      continue;
    }

    int their_num_refs = buf->num_ref_frames;
    for (int their_ref = 0; their_ref < their_num_refs; ++their_ref) {
      const WarpedMotionParams *base_model = &buf->global_motion[their_ref];
      if (base_model->wmtype == IDENTITY) {
        continue;
      }

      const int our_ref_order_hint = buf->display_order_hint;
      const int their_ref_order_hint = buf->ref_display_order_hint[their_ref];
      int base_temporal_distance =
          get_relative_dist(&seq_params->order_hint_info, our_ref_order_hint,
                            their_ref_order_hint);

      int this_num_models = 0;
      int this_cost =
          (aom_count_primitive_quniform(num_total_refs + 1, our_ref) +
           aom_count_primitive_quniform(their_num_refs, their_ref))
          << AV1_PROB_COST_SHIFT;
      uint8_t this_enable_models = 0;

      for (int frame = 0; frame < num_total_refs; frame++) {
        const WarpedMotionParams *model = &cm->global_motion[frame];
        if (model->wmtype == IDENTITY) continue;

        int temporal_distance;
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
        if (seq_params->order_hint_info.enable_order_hint) {
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
          const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, frame);
          const int ref_order_hint = ref_buf->display_order_hint;
          const int cur_order_hint = cm->cur_frame->display_order_hint;
          temporal_distance = get_relative_dist(&seq_params->order_hint_info,
                                                cur_order_hint, ref_order_hint);
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
        } else {
          temporal_distance = 1;
        }
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT

        if (temporal_distance == 0) {
          // Don't code global motion for frames at the same temporal instant
          assert(model->wmtype == IDENTITY);
          continue;
        }

        WarpedMotionParams ref_params;
        av1_scale_warp_model(base_model, base_temporal_distance, &ref_params,
                             temporal_distance);

        int model_cost = gm_get_params_cost(model, &ref_params,
                                            cm->features.fr_mv_precision);
        bool use_model = av1_is_enough_erroradvantage(
            gm_info->erroradvantage[frame], model_cost);

        if (use_model) {
          this_num_models += 1;
          this_cost += model_cost;
          this_enable_models |= (1 << frame);
        }
      }

      if (this_num_models > best_num_models ||
          (this_num_models == best_num_models && this_cost < best_cost)) {
        best_our_ref = our_ref;
        best_their_ref = their_ref;
        best_base_model = base_model;
        best_temporal_distance = base_temporal_distance;
        best_num_models = this_num_models;
        best_cost = this_cost;
        best_enable_models = this_enable_models;
      }
    }
  }

  gm_info->base_model_our_ref = best_our_ref;
  gm_info->base_model_their_ref = best_their_ref;
  cm->base_global_motion_model = *best_base_model;
  cm->base_global_motion_distance = best_temporal_distance;

  for (int frame = 0; frame < num_total_refs; frame++) {
    if ((best_enable_models & (1 << frame)) == 0) {
      // Disable this model
      cm->global_motion[frame] = default_warp_params;
    }
  }
}
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

// Initializes parameters used for computing global motion.
static AOM_INLINE void setup_global_motion_info_params(AV1_COMP *cpi) {
  GlobalMotionInfo *const gm_info = &cpi->gm_info;
  YV12_BUFFER_CONFIG *source = cpi->source;

  gm_info->segment_map_w =
      (source->y_crop_width + WARP_ERROR_BLOCK - 1) >> WARP_ERROR_BLOCK_LOG;
  gm_info->segment_map_h =
      (source->y_crop_height + WARP_ERROR_BLOCK - 1) >> WARP_ERROR_BLOCK_LOG;

  memset(gm_info->reference_frames, -1,
         sizeof(gm_info->reference_frames[0][0]) * MAX_DIRECTIONS *
             INTER_REFS_PER_FRAME);
  av1_zero(gm_info->num_ref_frames);

  // Populate ref_buf for valid ref frames in global motion
  update_valid_ref_frames_for_gm(cpi, gm_info->ref_buf,
                                 gm_info->reference_frames,
                                 gm_info->num_ref_frames);

  // Sort the past and future ref frames in the ascending order of their
  // distance from the current frame. reference_frames[0] => past direction
  // and reference_frames[1] => future direction.
  qsort(gm_info->reference_frames[0], gm_info->num_ref_frames[0],
        sizeof(gm_info->reference_frames[0][0]), compare_distance);
  qsort(gm_info->reference_frames[1], gm_info->num_ref_frames[1],
        sizeof(gm_info->reference_frames[1][0]), compare_distance);
}

// Computes global motion w.r.t. valid reference frames.
static AOM_INLINE void global_motion_estimation(AV1_COMP *cpi) {
  GlobalMotionInfo *const gm_info = &cpi->gm_info;

  MotionModel motion_models[RANSAC_NUM_MOTIONS];
  uint8_t *segment_map = NULL;

  alloc_global_motion_data(motion_models, &segment_map, gm_info->segment_map_w,
                           gm_info->segment_map_h);

  // Compute global motion w.r.t. past reference frames and future reference
  // frames
  for (int dir = 0; dir < MAX_DIRECTIONS; dir++) {
    if (gm_info->num_ref_frames[dir] > 0)
      compute_global_motion_for_references(
          cpi, gm_info->ref_buf, gm_info->reference_frames[dir],
          gm_info->num_ref_frames[dir], motion_models, segment_map,
          gm_info->segment_map_w, gm_info->segment_map_h);
  }

  dealloc_global_motion_data(motion_models, segment_map);
}

// Updates frame level stats related to global motion
static AOM_INLINE void update_gm_stats(AV1_COMP *cpi) {
  const GF_GROUP *gf_group = &cpi->gf_group;
  FRAME_UPDATE_TYPE update_type = gf_group->update_type[gf_group->index];
  int i, is_gm_present = 0;

  // Check if the current frame has any valid global motion model across its
  // reference frames
  for (i = 0; i < INTER_REFS_PER_FRAME; i++) {
    if (cpi->common.global_motion[i].wmtype != IDENTITY) {
      is_gm_present = 1;
      break;
    }
  }

  int update_actual_stats = 1;
  if (update_actual_stats) {
    if (cpi->valid_gm_model_found[update_type] == INT32_MAX) {
      cpi->valid_gm_model_found[update_type] = is_gm_present;
    } else {
      cpi->valid_gm_model_found[update_type] |= is_gm_present;
    }
  }
}

// Global motion estimation for the current frame is computed.This computation
// happens once per frame and the winner motion model parameters are stored in
// cm->cur_frame->global_motion.
void av1_compute_global_motion_facade(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *gf_group = &cpi->gf_group;
  GlobalMotionInfo *const gm_info = &cpi->gm_info;

  // Reset `valid_gm_model_found` at the start of each GOP
  if (cpi->oxcf.tool_cfg.enable_global_motion) {
    if (gf_group->index == 0) {
      for (int i = 0; i < FRAME_UPDATE_TYPES; i++) {
        cpi->valid_gm_model_found[i] = INT32_MAX;
      }
    }
  }

  if (cpi->common.current_frame.frame_type == INTER_FRAME && cpi->source &&
      cpi->oxcf.tool_cfg.enable_global_motion &&
      cpi->sf.gm_sf.max_ref_frames > 0 && !gm_info->search_done) {
    setup_global_motion_info_params(cpi);

    if (cpi->mt_info.num_workers > 1)
      av1_global_motion_estimation_mt(cpi);
    else
      global_motion_estimation(cpi);

#if CONFIG_IMPROVED_GLOBAL_MOTION
    // Once we have determined the best motion model for each ref frame,
    // choose the base parameters to minimize the total encoding cost
    pick_base_gm_params(cpi);
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

    update_gm_stats(cpi);

    gm_info->search_done = 1;
  }

  memcpy(cm->cur_frame->global_motion, cm->global_motion,
         sizeof(cm->cur_frame->global_motion));
}
