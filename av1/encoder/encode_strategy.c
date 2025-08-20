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

#include <stdint.h>

#include "config/aom_config.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_codec.h"
#include "aom/aom_encoder.h"

#include "aom_ports/system_state.h"

#if CONFIG_MISMATCH_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_MISMATCH_DEBUG

#include "av1/common/av1_common_int.h"
#if CONFIG_BRU
#include "av1/common/bru.h"
#endif  // CONFIG_BRU
#include "av1/common/reconinter.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/encode_strategy.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/firstpass.h"
#include "av1/encoder/gop_structure.h"
#include "av1/encoder/pass2_strategy.h"
#include "av1/encoder/temporal_filter.h"
#include "av1/encoder/tpl_model.h"
#include "av1/encoder/encoder_utils.h"

#if CONFIG_TUNE_VMAF
#include "av1/encoder/tune_vmaf.h"
#endif

#define TEMPORAL_FILTER_KEY_FRAME 1

// Get the subgop config corresponding to the current frame within the
// gf group
const SubGOPStepCfg *get_subgop_step(const GF_GROUP *const gf_group,
                                     int index) {
  const SubGOPCfg *subgop_cfg = gf_group->subgop_cfg;
  if (subgop_cfg == NULL) return NULL;
  const int is_first_gop = (gf_group->update_type[0] == KF_UPDATE);
  const int offset =
      gf_group->has_overlay_for_key_frame ? 2 : (is_first_gop ? 1 : 0);
  return &subgop_cfg->step[index - offset];
}

void av1_get_ref_frames_enc(AV1_COMMON *cm, int cur_frame_disp,
                            RefFrameMapPair *ref_frame_map_pairs) {
  assert(cm->seq_params.explicit_ref_frame_map);
  // With explicit_ref_frame_map on, an encoder-only ranking scheme can be
  // implemented here. For now, av1_get_ref_frames is used as a placeholder.
  av1_get_ref_frames(cm, cur_frame_disp, ref_frame_map_pairs);

#if CONFIG_BRU
  // if BRU ref frame is not in the top n_refs list, swap bru ref to the last of
  // top_n
  enc_bru_swap_ref(cm);
#endif  // CONFIG_BRU
}

void av1_configure_buffer_updates(AV1_COMP *const cpi,
                                  const FRAME_UPDATE_TYPE type) {
  // NOTE(weitinglin): Should we define another function to take care of
  // cpi->rc.is_$Source_Type to make this function as it is in the comment?

  cpi->rc.is_src_frame_alt_ref = 0;

  switch (type) {
    case OVERLAY_UPDATE:
    case KFFLT_OVERLAY_UPDATE:
    case INTNL_OVERLAY_UPDATE: cpi->rc.is_src_frame_alt_ref = 1; break;

    case KF_UPDATE:
    case LF_UPDATE:
    case GF_UPDATE:
    case ARF_UPDATE:
    case KFFLT_UPDATE:
    case INTNL_ARF_UPDATE: break;

    default: assert(0); break;
  }
}

static void set_additional_frame_flags(const AV1_COMMON *const cm,
                                       unsigned int *const frame_flags) {
  if (frame_is_intra_only(cm)) {
    *frame_flags |= FRAMEFLAGS_INTRAONLY;
  }
  if (frame_is_sframe(cm)) {
    *frame_flags |= FRAMEFLAGS_SWITCH;
  }
  if (cm->features.error_resilient_mode) {
    *frame_flags |= FRAMEFLAGS_ERROR_RESILIENT;
  }
  if (cm->film_grain_params.apply_grain) {
    *frame_flags |= FRAMEFLAGS_HAS_FILM_GRAIN_PARAMS;
  }
}

static INLINE void update_keyframe_counters(AV1_COMP *cpi) {
  if (cpi->common.show_frame) {
    cpi->rc.frames_since_key++;
    cpi->rc.frames_to_key--;
  }
}

static INLINE int is_frame_droppable(
    const ExtRefreshFrameFlagsInfo *const ext_refresh_frame_flags) {
  // Droppable frame is only used by external refresh flags. VoD setting won't
  // trigger its use case.
  if (ext_refresh_frame_flags->update_pending)
    return ext_refresh_frame_flags->all_ref_frames == 0;
  else
    return 0;
}

static INLINE void update_frames_till_gf_update(AV1_COMP *cpi) {
  // TODO(weitinglin): Updating this counter for is_frame_droppable
  // is a work-around to handle the condition when a frame is drop.
  // We should fix the cpi->common.show_frame flag
  // instead of checking the other condition to update the counter properly.
  if (cpi->common.show_frame ||
      is_frame_droppable(&cpi->ext_flags.refresh_frame)) {
    // Decrement count down till next gf
    if (cpi->rc.frames_till_gf_update_due > 0)
      cpi->rc.frames_till_gf_update_due--;
  }
}

static INLINE void update_gf_group_index(AV1_COMP *cpi) {
  // Increment the gf group index ready for the next frame. If this is
  // a show_existing_frame with a source other than altref, or if it is not
  // a displayed forward keyframe, the index was incremented when it was
  // originally encoded.
  if (!cpi->common.show_existing_frame || cpi->rc.is_src_frame_alt_ref ||
      cpi->common.current_frame.frame_type == KEY_FRAME) {
    ++cpi->gf_group.index;
  }
}

// Update show_existing_frame flag for frames of type OVERLAY_UPDATE in the
// current GF interval
static INLINE void set_show_existing_alt_ref(GF_GROUP *const gf_group,
                                             int apply_filtering,
                                             int enable_overlay,
                                             int show_existing_alt_ref) {
  if (get_frame_update_type(gf_group) != ARF_UPDATE &&
      get_frame_update_type(gf_group) != KFFLT_UPDATE)
    return;

  if (get_frame_update_type(gf_group) == KFFLT_UPDATE) {
    // Key overlay is always used to ensure good visual quality.
    gf_group->show_existing_alt_ref = 0;
    return;
  }

  if (!enable_overlay)
    gf_group->show_existing_alt_ref = 1;
  else
    gf_group->show_existing_alt_ref =
        apply_filtering ? show_existing_alt_ref : 1;
}

static void update_rc_counts(AV1_COMP *cpi) {
  update_keyframe_counters(cpi);
  update_frames_till_gf_update(cpi);
  update_gf_group_index(cpi);
}

static void set_ext_overrides(AV1_COMMON *const cm,
                              EncodeFrameParams *const frame_params,
                              ExternalFlags *const ext_flags) {
  // Overrides the defaults with the externally supplied values with
  // av1_update_reference() and av1_update_entropy() calls
  // Note: The overrides are valid only for the next frame passed
  // to av1_encode_lowlevel()

  if (ext_flags->use_s_frame) {
    frame_params->frame_type = S_FRAME;
  }

  if (ext_flags->refresh_frame_context_pending) {
    cm->features.refresh_frame_context = ext_flags->refresh_frame_context;
    ext_flags->refresh_frame_context_pending = 0;
  }
  cm->features.allow_ref_frame_mvs = ext_flags->use_ref_frame_mvs;

  frame_params->error_resilient_mode = ext_flags->use_error_resilient;
  // A keyframe is already error resilient and keyframes with
  // error_resilient_mode interferes with the use of show_existing_frame
  // when forward reference keyframes are enabled.
  frame_params->error_resilient_mode &= frame_params->frame_type != KEY_FRAME;
  // For bitstream conformance, s-frames must be error-resilient
  frame_params->error_resilient_mode |= frame_params->frame_type == S_FRAME;
}

// Map the subgop cfg reference list to actual reference buffers. Disable
// any reference frames that are not listed in the sub gop.
static void get_gop_cfg_enabled_refs(AV1_COMP *const cpi, int *ref_frame_flags,
                                     int order_offset) {
  GF_GROUP gf_group = cpi->gf_group;
  // The current display index stored has not yet been updated. We must add
  // The order offset to get the correct value here.
  const int cur_frame_disp =
      cpi->common.current_frame.frame_number + order_offset;

  const SubGOPStepCfg *step_gop_cfg =
      get_subgop_step(&gf_group, gf_group.index);
  assert(step_gop_cfg != NULL);
  // No references specified
  if (step_gop_cfg->num_references < 0) return;

  // Mask to indicate whether or not each ref is allowed by the GOP config
  int ref_frame_used[INTER_REFS_PER_FRAME] = { 0 };
  // Structures to hash each reference frame based on its pyramid level. This
  // will allow us to match the pyramid levels specified in the cfg to the best
  // reference frame index.
  int n_references[MAX_ARF_LAYERS + 1] = { 0 };
  int references[MAX_ARF_LAYERS + 1][INTER_REFS_PER_FRAME] = { { 0 } };
  int disp_orders[MAX_ARF_LAYERS + 1][INTER_REFS_PER_FRAME] = { { 0 } };

  int frame_level = -1;
  // Loop over each reference frame and hash it based on its pyramid level
  for (int frame = 0; frame < INTER_REFS_PER_FRAME; frame++) {
    // Get reference frame buffer
    const RefCntBuffer *const buf = get_ref_frame_buf(&cpi->common, frame);
    if (buf == NULL) continue;
    const int frame_order = (int)buf->display_order_hint;
    frame_level = buf->pyramid_level;

    // Sometimes a frame index is in multiple reference buffers.
    // Do not add a frame to the pyramid list multiple times.
    int found = 0;
    for (int r = 0; r < n_references[frame_level]; r++) {
      if (frame_order == disp_orders[frame_level][r]) {
        found = 1;
        break;
      }
    }
    // If this is an unseen frame, map its display order and ref buffer
    // index to its level in the pyramid
    if (!found) {
      int n_refs = n_references[frame_level]++;
      disp_orders[frame_level][n_refs] = frame_order;
      references[frame_level][n_refs] = frame;
    }
  }

  // For each reference specified in the step_gop_cfg, map it to a reference
  // buffer based on pyramid level if possible.
  for (int i = 0; i < step_gop_cfg->num_references; i++) {
    const int level = step_gop_cfg->references[i];
    const int abs_level = abs(level);
    int best_frame = -1;
    int best_frame_index = -1;
    int best_disp_order = INT_MAX;
    for (int ref = 0; ref < n_references[abs_level]; ref++) {
      const int disp_order = disp_orders[abs_level][ref];
      const int cur_order_diff = cur_frame_disp - disp_order;
      // This frame has already been used
      if (disp_order < 0) continue;
      // This frame is in the wrong direction
      if ((cur_order_diff < 0) != (level < 0)) continue;
      // Store this frame if it is the closest in display order to the current
      // frame so far
      if (abs(cur_order_diff) < abs(best_disp_order - cur_frame_disp)) {
        best_frame = references[abs_level][ref];
        best_frame_index = ref;
        best_disp_order = disp_order;
      }
    }
    update_subgop_ref_stats(&cpi->subgop_stats,
                            cpi->oxcf.unit_test_cfg.enable_subgop_stats, i,
                            (best_frame < 0) ? 0 : 1, level, best_disp_order,
                            (int)step_gop_cfg->num_references);
    if (best_frame == -1) {
      if (cpi->print_per_frame_stats) {
        fprintf(stderr,
                "Warning [Subgop cfg]: "
                "Level %d ref for frame %d not found\n",
                level, step_gop_cfg->disp_frame_idx);
      }
    } else {
      ref_frame_used[best_frame] = 1;
      disp_orders[abs_level][best_frame_index] = -1;
    }
  }

  // Avoid using references that were not specified by the cfg
  for (int frame = 0; frame < INTER_REFS_PER_FRAME; frame++)
    if (!ref_frame_used[frame]) *ref_frame_flags &= ~(1 << (frame));
}

#if CONFIG_BRU
static void bru_lookahead_update(AV1_COMP *const cpi,
                                 const int bru_ref_buf_offset,
                                 struct lookahead_entry **bru_ref_source) {
  if (cpi->common.seq_params.enable_bru) {
    AV1_COMMON *const cm = &cpi->common;
    const int n_refs = cm->ref_frames_info.num_total_refs;
    if (n_refs >= BRU_ENC_LOOKAHEAD_DIST_MINUS_1 + BRU_ENC_REF_DELAY)
      *bru_ref_source = av1_lookahead_peek(cpi->lookahead, bru_ref_buf_offset,
                                           cpi->compressor_stage);
  }
}

static void init_bru_frame(AV1_COMMON *const cm) {
  // bru skip mode update ref
  if (cm->bru.enabled) {
    int bru_ref_idx = cm->bru.update_ref_idx;
    RefCntBuffer *ref_buf = get_ref_frame_buf(cm, bru_ref_idx);
    if (!ref_buf) {
      cm->bru.enabled = 0;  // disable if no valid ref
    }
    if (!cm->bru.enabled) {
      cm->bru.update_ref_idx = -1;
      cm->bru.explicit_ref_idx = -1;
    }
  }
}
#endif  // CONFIG_BRU

static void adjust_frame_rate(AV1_COMP *cpi, int64_t ts_start, int64_t ts_end) {
  TimeStamps *time_stamps = &cpi->time_stamps;
  int64_t this_duration;
  int step = 0;

  // Clear down mmx registers
  aom_clear_system_state();

  if (ts_start == time_stamps->first_ever) {
    this_duration = ts_end - ts_start;
    step = 1;
  } else {
    int64_t last_duration =
        time_stamps->prev_end_seen - time_stamps->prev_start_seen;

    this_duration = ts_end - time_stamps->prev_end_seen;

    // do a step update if the duration changes by 10%
    if (last_duration)
      step = (int)((this_duration - last_duration) * 10 / last_duration);
  }

  if (this_duration) {
    if (step) {
      av1_new_framerate(cpi, 10000000.0 / this_duration);
    } else {
      // Average this frame's rate into the last second's average
      // frame rate. If we haven't seen 1 second yet, then average
      // over the whole interval seen.
      const double interval =
          AOMMIN((double)(ts_end - time_stamps->first_ever), 10000000.0);
      double avg_duration = 10000000.0 / cpi->framerate;
      avg_duration *= (interval - avg_duration + this_duration);
      avg_duration /= interval;

      av1_new_framerate(cpi, 10000000.0 / avg_duration);
    }
  }
  time_stamps->prev_start_seen = ts_start;
  time_stamps->prev_end_seen = ts_end;
}

// Determine whether there is a forced keyframe pending in the lookahead buffer
int get_forced_keyframe_position(struct lookahead_ctx *lookahead,
                                 const int up_to_index,
                                 const COMPRESSOR_STAGE compressor_stage) {
  /* If the forced kf is not available or if the current frame is
   * forced kf, then return -1. Else return the position of the
   * forced kf.
   */
  for (int i = 0; i <= up_to_index; i++) {
    const struct lookahead_entry *e =
        av1_lookahead_peek(lookahead, i, compressor_stage);
    if (e == NULL) {
      // We have reached the end of the lookahead buffer and not early-returned
      // so there isn't a forced key-frame pending.
      return -1;
    } else if (e->flags == AOM_EFLAG_FORCE_KF) {
      return (i > 0) ? i : -1;
    } else {
      continue;
    }
  }
  return -1;  // Never reached
}

// Check if we should encode an ARF or internal ARF.  If not, try a LAST
// Do some setup associated with the chosen source
// temporal_filtered, flush, and frame_update_type are outputs.
// Return the frame source, or NULL if we couldn't find one
static struct lookahead_entry *choose_frame_source(
    AV1_COMP *const cpi, int *const flush, struct lookahead_entry **last_source,
#if CONFIG_BRU
    int bru_ref_buf_offset, struct lookahead_entry **bru_ref_source,
#endif  // CONFIG_BRU
    EncodeFrameParams *const frame_params) {
  AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->gf_group;
  struct lookahead_entry *source = NULL;

  // Source index in lookahead buffer.
  int src_index = gf_group->arf_src_offset[gf_group->index];

  // TODO(Aasaipriya): Forced key frames need to be fixed when rc_mode != AOM_Q
  if (src_index &&
      (get_forced_keyframe_position(cpi->lookahead, src_index,
                                    cpi->compressor_stage) != -1) &&
      cpi->oxcf.rc_cfg.mode != AOM_Q) {
    src_index = 0;
    *flush = 1;
  }

  // If the current frame is arf, then we should not pop from the lookahead
  // buffer. If the current frame is not arf, then pop it. This assumes the
  // first frame in the GF group is not arf. May need to change if it is not
  // true.
  int pop_lookahead = (src_index == 0);
  // If this is a key frame and keyframe filtering is enabled with overlay,
  // then do not pop.
  if (pop_lookahead && cpi->oxcf.kf_cfg.enable_keyframe_filtering > 1 &&
      gf_group->update_type[gf_group->index] == KFFLT_UPDATE &&
      !is_stat_generation_stage(cpi) && cpi->lookahead) {
    if (cpi->lookahead->read_ctxs[cpi->compressor_stage].sz &&
        (*flush ||
         cpi->lookahead->read_ctxs[cpi->compressor_stage].sz ==
             cpi->lookahead->read_ctxs[cpi->compressor_stage].pop_sz)) {
      pop_lookahead = 0;
    }
  }
  frame_params->show_frame = pop_lookahead;
  if (pop_lookahead) {
    // show frame, pop from buffer
    // Get last frame source.
    if (cm->current_frame.frame_number > 0) {
      *last_source =
          av1_lookahead_peek(cpi->lookahead, -1, cpi->compressor_stage);
    }
#if CONFIG_BRU
    if (cpi->common.seq_params.enable_bru) {
      bru_lookahead_update(cpi, bru_ref_buf_offset, bru_ref_source);
    }
#endif  // CONFIG_BRU
    // Read in the source frame.
    source = av1_lookahead_pop(cpi->lookahead, *flush, cpi->compressor_stage);
  } else {
    // no show frames are arf frames
    source =
        av1_lookahead_peek(cpi->lookahead, src_index, cpi->compressor_stage);
    if (source != NULL) {
      cm->showable_frame = 1;
      if (gf_group->update_type[gf_group->index] == KFFLT_UPDATE)
        cm->showable_frame = 0;
    }
  }
  return source;
}

// Don't allow a show_existing_frame to coincide with an error resilient or
// S-Frame. An exception can be made in the case of a keyframe, since it does
// not depend on any previous frames.
static int allow_show_existing(const AV1_COMP *const cpi,
                               unsigned int frame_flags) {
  if (cpi->common.current_frame.frame_number == 0) return 0;

  const struct lookahead_entry *lookahead_src =
      av1_lookahead_peek(cpi->lookahead, 0, cpi->compressor_stage);
  if (lookahead_src == NULL) return 1;

  const int is_error_resilient =
      cpi->oxcf.tool_cfg.error_resilient_mode ||
      (lookahead_src->flags & AOM_EFLAG_ERROR_RESILIENT);
  const int is_s_frame = cpi->oxcf.kf_cfg.enable_sframe ||
                         (lookahead_src->flags & AOM_EFLAG_SET_S_FRAME);
  const int is_key_frame =
      (cpi->rc.frames_to_key == 0) || (frame_flags & FRAMEFLAGS_KEY);
  return !(is_error_resilient || is_s_frame) || is_key_frame;
}

// Update frame_flags to tell the encoder's caller what sort of frame was
// encoded.
static void update_frame_flags(const AV1_COMMON *const cm,
                               unsigned int *frame_flags) {
  if (encode_show_existing_frame(cm)) {
    *frame_flags &= ~FRAMEFLAGS_KEY;
    return;
  }

  if (cm->current_frame.frame_type == KEY_FRAME) {
    *frame_flags |= FRAMEFLAGS_KEY;
  } else {
    *frame_flags &= ~FRAMEFLAGS_KEY;
  }
}

#define DUMP_REF_FRAME_IMAGES 0

#if DUMP_REF_FRAME_IMAGES == 1
static int dump_one_image(AV1_COMMON *cm,
                          const YV12_BUFFER_CONFIG *const ref_buf,
                          char *file_name) {
  int h;
  FILE *f_ref = NULL;

  if (ref_buf == NULL) {
    printf("Frame data buffer is NULL.\n");
    return AOM_CODEC_MEM_ERROR;
  }

  if ((f_ref = fopen(file_name, "wb")) == NULL) {
    printf("Unable to open file %s to write.\n", file_name);
    return AOM_CODEC_MEM_ERROR;
  }

  // --- Y ---
  for (h = 0; h < cm->height; ++h) {
    fwrite(&ref_buf->y_buffer[h * ref_buf->y_stride], 1, cm->width, f_ref);
  }
  // --- U ---
  for (h = 0; h < (cm->height >> 1); ++h) {
    fwrite(&ref_buf->u_buffer[h * ref_buf->uv_stride], 1, (cm->width >> 1),
           f_ref);
  }
  // --- V ---
  for (h = 0; h < (cm->height >> 1); ++h) {
    fwrite(&ref_buf->v_buffer[h * ref_buf->uv_stride], 1, (cm->width >> 1),
           f_ref);
  }

  fclose(f_ref);

  return AOM_CODEC_OK;
}

static void dump_ref_frame_images(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MV_REFERENCE_FRAME ref_frame;

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    char file_name[256] = "";
    snprintf(file_name, sizeof(file_name), "/tmp/enc_F%d_ref_%d.yuv",
             cm->current_frame.frame_number, ref_frame);
    dump_one_image(cm, get_ref_frame_yv12_buf(cpi, ref_frame), file_name);
  }
}
#endif  // DUMP_REF_FRAME_IMAGES == 1

int av1_get_refresh_ref_frame_map(AV1_COMMON *cm, int refresh_frame_flags) {
  int ref_map_index = INVALID_IDX;
  for (ref_map_index = 0; ref_map_index < cm->seq_params.ref_frames;
       ++ref_map_index) {
    if ((refresh_frame_flags >> ref_map_index) & 1) break;
  }

  return ref_map_index;
}

int use_subgop_cfg(const GF_GROUP *const gf_group, int gf_index) {
  if (gf_index < 0) return 0;
  if (gf_group->subgop_cfg == NULL) return 0;
  if (gf_index == 1) return !gf_group->has_overlay_for_key_frame;
  return 1;
}
static int get_free_ref_map_index(RefFrameMapPair ref_map_pairs[REF_FRAMES],
                                  const int ref_frames) {
  for (int idx = 0; idx < ref_frames; ++idx)
    if (ref_map_pairs[idx].ref_frame_for_inference == -1) return idx;
  return INVALID_IDX;
}

static int get_refresh_idx(int update_arf, int refresh_level,
                           int cur_frame_disp,
                           RefFrameMapPair ref_frame_map_pairs[REF_FRAMES],
                           const int ref_frames) {
  int arf_count = 0;
  int oldest_arf_order = INT32_MAX;
  int oldest_arf_idx = -1;

  int oldest_frame_order = INT32_MAX;
  int oldest_idx = -1;

  int oldest_ref_level_order = INT32_MAX;
  int oldest_ref_level_idx = -1;
  for (int map_idx = 0; map_idx < ref_frames; map_idx++) {
    RefFrameMapPair ref_pair = ref_frame_map_pairs[map_idx];
    if (ref_pair.ref_frame_for_inference == -1) continue;
    const int frame_order = ref_pair.disp_order;
    const int reference_frame_level = ref_pair.pyr_level;
    // Keep future frames and three closest previous frames in output order
    if (frame_order > cur_frame_disp - 3) continue;

    // Keep track of the oldest reference frame matching the specified
    // refresh level from the subgop cfg
    if (refresh_level > 0 && refresh_level == reference_frame_level) {
      if (frame_order < oldest_ref_level_order) {
        oldest_ref_level_order = frame_order;
        oldest_ref_level_idx = map_idx;
      }
    }

    // Keep track of the oldest level 1 frame if the current frame is level also
    // 1
    if (reference_frame_level == 1) {
      // If there are more than 2 level 1 frames in the reference list,
      // discard the oldest
      if (frame_order < oldest_arf_order) {
        oldest_arf_order = frame_order;
        oldest_arf_idx = map_idx;
      }
      arf_count++;
      continue;
    }

    // Update the overall oldest reference frame
    if (frame_order < oldest_frame_order) {
      oldest_frame_order = frame_order;
      oldest_idx = map_idx;
    }
  }
  if (oldest_ref_level_idx > -1) return oldest_ref_level_idx;
  if (update_arf && arf_count > 2) return oldest_arf_idx;
  if (oldest_idx >= 0) return oldest_idx;
  if (oldest_arf_idx >= 0) return oldest_arf_idx;
  assert(0 && "No valid refresh index found");
  return -1;
}

static int get_refresh_frame_flags_subgop_cfg(
    const AV1_COMP *const cpi, int gf_index, int cur_disp_order,
    RefFrameMapPair ref_frame_map_pairs[REF_FRAMES], int refresh_mask,
    int free_fb_index) {
  const SubGOPStepCfg *step_gop_cfg = get_subgop_step(&cpi->gf_group, gf_index);
  assert(step_gop_cfg != NULL);
  const int pyr_level = step_gop_cfg->pyr_level;
  const FRAME_TYPE_CODE type_code = step_gop_cfg->type_code;
  const int refresh_level = step_gop_cfg->refresh;
  if (refresh_level == 0) return 0;

  // No refresh necessary for these frame types
  if (type_code == FRAME_TYPE_INO_REPEAT ||
      type_code == FRAME_TYPE_INO_SHOWEXISTING)
    return refresh_mask;
  // If there is an open slot, refresh that one instead of replacing a reference
  if (free_fb_index != INVALID_IDX) {
    refresh_mask = 1 << free_fb_index;
    return refresh_mask;
  }

  const int update_arf = type_code == FRAME_TYPE_OOO_FILTERED && pyr_level == 1;
  const int refresh_idx =
      get_refresh_idx(update_arf, refresh_level, cur_disp_order,
                      ref_frame_map_pairs, cpi->common.seq_params.ref_frames);
  return 1 << refresh_idx;
}

int av1_get_refresh_frame_flags(
    const AV1_COMP *const cpi, const EncodeFrameParams *const frame_params,
    FRAME_UPDATE_TYPE frame_update_type, int gf_index, int cur_disp_order,
    RefFrameMapPair ref_frame_map_pairs[REF_FRAMES]) {
  // Switch frames and shown key-frames overwrite all reference slots
  if ((frame_params->frame_type == KEY_FRAME && !cpi->no_show_fwd_kf) ||
      frame_params->frame_type == S_FRAME) {
    return (1 << cpi->common.seq_params.ref_frames) - 1;
  }

  // show_existing_frames don't actually send refresh_frame_flags so set the
  // flags to 0 to keep things consistent.
  if (frame_params->show_existing_frame &&
      (!frame_params->error_resilient_mode ||
       frame_params->frame_type == KEY_FRAME)) {
    return 0;
  }

  int refresh_mask = 0;
  const ExtRefreshFrameFlagsInfo *const ext_refresh_frame_flags =
      &cpi->ext_flags.refresh_frame;

  if (is_frame_droppable(ext_refresh_frame_flags)) return 0;

  if (ext_refresh_frame_flags->update_pending) {
    return refresh_mask;
  }

#if CONFIG_BRU
  // BRU frame, refresh flag is set to refresh BRU ref frame
  int free_fb_index = INVALID_IDX;
  if (cpi->common.bru.enabled) {
    const int bru_ref_order = cpi->common.bru.ref_order;
    assert(bru_ref_order >= 0);
    for (int idx = 0; idx < cpi->common.seq_params.ref_frames; ++idx) {
      if (ref_frame_map_pairs[idx].disp_order == bru_ref_order) {
        free_fb_index = idx;  // get the first one
        break;
      }
    }
  } else {
    free_fb_index = get_free_ref_map_index(ref_frame_map_pairs,
                                           cpi->common.seq_params.ref_frames);
  }
#else
  // Search for the open slot to store the current frame.
  int free_fb_index = get_free_ref_map_index(ref_frame_map_pairs,
                                             cpi->common.seq_params.ref_frames);
#endif  // CONFIG_BRU
  if (use_subgop_cfg(&cpi->gf_group, gf_index)) {
    const int mask = get_refresh_frame_flags_subgop_cfg(
        cpi, gf_index, cur_disp_order, ref_frame_map_pairs, refresh_mask,
        free_fb_index);
    return mask;
  }

  // No refresh necessary for these frame types
  if (frame_update_type == OVERLAY_UPDATE ||
      frame_update_type == KFFLT_OVERLAY_UPDATE ||
      frame_update_type == INTNL_OVERLAY_UPDATE) {
    return refresh_mask;
  }

  // If there is an open slot, refresh that one instead of replacing a reference
  if (free_fb_index != INVALID_IDX) {
    return 1 << free_fb_index;
  }

  const int update_arf = frame_update_type == ARF_UPDATE;
  const int refresh_idx =
      get_refresh_idx(update_arf, -1, cur_disp_order, ref_frame_map_pairs,
                      cpi->common.seq_params.ref_frames);
  return 1 << refresh_idx;
}

void setup_mi(AV1_COMP *const cpi, YV12_BUFFER_CONFIG *src) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;

  av1_setup_src_planes(x, src, 0, 0, num_planes, NULL);

  av1_setup_block_planes(xd, cm->seq_params.subsampling_x,
                         cm->seq_params.subsampling_y, num_planes);

  set_mi_offsets(&cm->mi_params, xd, 0, 0, 0, 0);
}

// Apply temporal filtering to source frames and encode the filtered frame.
// If the current frame does not require filtering, this function is identical
// to av1_encode() except that tpl is not performed.
static int denoise_and_encode(AV1_COMP *const cpi, uint8_t *const dest,
                              EncodeFrameInput *const frame_input,
                              EncodeFrameParams *const frame_params,
                              EncodeFrameResults *const frame_results) {
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  AV1_COMMON *const cm = &cpi->common;
  const GF_GROUP *const gf_group = &cpi->gf_group;

  // Decide whether to apply temporal filtering to the source frame.
  int apply_filtering = 0;
  int arf_src_index = -1;
  if (frame_params->frame_type == KEY_FRAME) {
    // Decide whether it is allowed to perform key frame filtering
    int allow_kf_filtering =
        oxcf->kf_cfg.enable_keyframe_filtering &&
        !is_stat_generation_stage(cpi) && !frame_params->show_existing_frame &&
        has_enough_frames_for_key_filtering(cpi->rc.frames_to_key,
                                            oxcf->algo_cfg.arnr_max_frames,
                                            oxcf->gf_cfg.lag_in_frames) &&
        (!is_lossless_requested(&oxcf->rc_cfg) ||
         oxcf->kf_cfg.enable_keyframe_filtering > 1);

    if (allow_kf_filtering) {
      const double y_noise_level = av1_estimate_noise_from_single_plane(
          frame_input->source, 0, cm->seq_params.bit_depth);
      apply_filtering = y_noise_level > 0;
    } else {
      apply_filtering = 0;
    }
    // If we are doing kf filtering, set up a few things.
    if (apply_filtering) {
      MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
      av1_init_mi_buffers(&cm->mi_params);
      setup_mi(cpi, frame_input->source);
      av1_init_macroblockd(cm, xd);
      memset(cpi->mbmi_ext_info.frame_base, 0,
             cpi->mbmi_ext_info.alloc_size *
                 sizeof(*cpi->mbmi_ext_info.frame_base));

      av1_set_speed_features_framesize_independent(cpi, oxcf->speed);
      av1_set_speed_features_framesize_dependent(cpi, oxcf->speed);
      av1_set_rd_speed_thresholds(cpi);
      av1_setup_frame_buf_refs(cm);
      av1_setup_frame_sign_bias(cm);
      av1_frame_init_quantizer(cpi);
      av1_setup_past_independence(cm);

      if (!frame_params->show_frame && cpi->no_show_fwd_kf) {
        // fwd kf
        arf_src_index = -1 * gf_group->arf_src_offset[gf_group->index];
      } else if (!frame_params->show_frame) {
        arf_src_index = 0;
      } else {
        arf_src_index = -1;
      }
    }
  } else if (get_frame_update_type(&cpi->gf_group) == ARF_UPDATE ||
             get_frame_update_type(&cpi->gf_group) == KFFLT_UPDATE ||
             get_frame_update_type(&cpi->gf_group) == INTNL_ARF_UPDATE) {
    // ARF
    apply_filtering = oxcf->algo_cfg.arnr_max_frames > 0;
    if (is_lossless_requested(&oxcf->rc_cfg)) {
      // Turn off temporal filtering if overlay is off.
      // Also, turn off temporal filtering for internal ARF if overlay is on,
      // since overlay is not supported for this frame, and without overlay,
      // the frame cannot become lossless after temporal filtering.
      apply_filtering &=
          (oxcf->algo_cfg.enable_overlay &
           (get_frame_update_type(&cpi->gf_group) != INTNL_ARF_UPDATE));
    }
    if (gf_group->is_user_specified) {
      apply_filtering &= gf_group->is_filtered[gf_group->index];
    }
    if (apply_filtering) {
      arf_src_index = gf_group->arf_src_offset[gf_group->index];
    }
  }
  // Save the pointer to the original source image.
  YV12_BUFFER_CONFIG *source_buffer = frame_input->source;
  // apply filtering to frame
  int show_existing_alt_ref = 0;
  if (apply_filtering) {
    // TODO(bohanli): figure out why we need frame_type in cm here.
    cm->current_frame.frame_type = frame_params->frame_type;
    const int code_arf =
        av1_temporal_filter(cpi, arf_src_index, &show_existing_alt_ref);
    if (code_arf) {
      aom_extend_frame_borders(&cpi->alt_ref_buffer, av1_num_planes(cm), 0);
      frame_input->source = &cpi->alt_ref_buffer;
      aom_copy_metadata_to_frame_buffer(frame_input->source,
                                        source_buffer->metadata);
    }
  }
#if !CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
#if CONFIG_F253_REMOVE_OUTPUTFLAG
  // show_existing mechanism is
#else
  // when enable_frame_output_order == 1, show_existing mechanism is
#endif  // CONFIG_F253_REMOVE_OUTPUTFLAG
  // used for alt_ref in encoder side internally, but the OBU with
  // show_existing_frame == 1 is not signaled in the bitstream.
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
  if (cm->seq_params.enable_frame_output_order)
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
    show_existing_alt_ref = 1;
#endif  // !CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
  set_show_existing_alt_ref(&cpi->gf_group, apply_filtering,
                            oxcf->algo_cfg.enable_overlay,
                            show_existing_alt_ref);

  // perform tpl after filtering
  int allow_tpl = oxcf->gf_cfg.lag_in_frames > 1 &&
                  !is_stat_generation_stage(cpi) &&
                  oxcf->algo_cfg.enable_tpl_model;
  if (frame_params->frame_type == KEY_FRAME) {
    // Don't do tpl for fwd key frames
    allow_tpl = allow_tpl && !cpi->sf.tpl_sf.disable_filtered_key_tpl &&
                !cpi->no_show_fwd_kf;
  } else {
    // Do tpl after ARF is filtered, or if no ARF, at the second frame of GF
    // group.
    // TODO(bohanli): if no ARF, just do it at the first frame.
    int gf_index = gf_group->index;
    allow_tpl = allow_tpl && (gf_group->update_type[gf_index] == ARF_UPDATE ||
                              gf_group->update_type[gf_index] == GF_UPDATE);
    if (allow_tpl) {
      // Need to set the size for TPL for ARF
      // TODO(bohanli): Why is this? what part of it is necessary?
      av1_set_frame_size(cpi, cm->width, cm->height);
    }
  }

  if (gf_group->index == 0) av1_init_tpl_stats(&cpi->tpl_data);
  if (allow_tpl) av1_tpl_setup_stats(cpi, 0, frame_params, frame_input);

  if (av1_encode(cpi, dest, frame_input, frame_params, frame_results) !=
      AOM_CODEC_OK) {
    return AOM_CODEC_ERROR;
  }

  // Set frame_input source to true source for psnr calculation.
  if (apply_filtering && is_psnr_calc_enabled(cpi)) {
    cpi->source = av1_scale_if_required(cm, source_buffer, &cpi->scaled_source,
                                        cm->features.interp_filter, 0, false);
    cpi->unscaled_source = source_buffer;
  }

  return AOM_CODEC_OK;
}

int av1_encode_strategy(AV1_COMP *const cpi, size_t *const size,
                        uint8_t *const dest, unsigned int *frame_flags,
                        int64_t *const time_stamp, int64_t *const time_end,
                        const aom_rational64_t *const timestamp_ratio,
                        int flush) {
  AV1EncoderConfig *const oxcf = &cpi->oxcf;
  AV1_COMMON *const cm = &cpi->common;
  GF_GROUP *gf_group = &cpi->gf_group;
  ExternalFlags *const ext_flags = &cpi->ext_flags;

  EncodeFrameInput frame_input;
  EncodeFrameParams frame_params;
  EncodeFrameResults frame_results;
  memset(&frame_input, 0, sizeof(frame_input));
  memset(&frame_params, 0, sizeof(frame_params));
  memset(&frame_results, 0, sizeof(frame_results));
#if CONFIG_BRU
  cm->bru.update_ref_idx = -1;
  cm->bru.explicit_ref_idx = -1;
  cm->bru.ref_order = -1;
#endif  // CONFIG_BRU

  // Check if we need to stuff more src frames
  if (flush == 0) {
    int srcbuf_size =
        av1_lookahead_depth(cpi->lookahead, cpi->compressor_stage);
    int pop_size = av1_lookahead_pop_sz(cpi->lookahead, cpi->compressor_stage);

    // Continue buffering look ahead buffer.
    if (srcbuf_size < pop_size) return -1;
  }

  if (!av1_lookahead_peek(cpi->lookahead, 0, cpi->compressor_stage)) {
    if (flush && oxcf->pass == 1 && !cpi->twopass.first_pass_done) {
      av1_end_first_pass(cpi); /* get last stats packet */
      cpi->twopass.first_pass_done = 1;
    }
    return -1;
  }

  if (!is_stat_generation_stage(cpi)) {
    // If this is a forward keyframe, mark as a show_existing_frame
    // TODO(bohanli): find a consistent condition for fwd keyframes
    if (oxcf->kf_cfg.fwd_kf_enabled &&
        (gf_group->index == (gf_group->size - 1)) &&
        (gf_group->update_type[gf_group->index] == OVERLAY_UPDATE ||
         gf_group->update_type[gf_group->index] == KFFLT_OVERLAY_UPDATE) &&
        gf_group->arf_index >= 0 && cpi->rc.frames_to_key == 0) {
      frame_params.show_existing_frame = 1;
    } else {
      frame_params.show_existing_frame =
          (gf_group->show_existing_alt_ref &&
           (gf_group->update_type[gf_group->index] == OVERLAY_UPDATE ||
            gf_group->update_type[gf_group->index] == KFFLT_OVERLAY_UPDATE)) ||
          gf_group->update_type[gf_group->index] == INTNL_OVERLAY_UPDATE;
    }
    frame_params.show_existing_frame &= allow_show_existing(cpi, *frame_flags);

    // Reset show_existing_alt_ref decision to 0 after it is used.
    if (gf_group->update_type[gf_group->index] == OVERLAY_UPDATE ||
        gf_group->update_type[gf_group->index] == KFFLT_OVERLAY_UPDATE) {
      gf_group->show_existing_alt_ref = 0;
    }
  } else {
    frame_params.show_existing_frame = 0;
  }

  if (!is_stat_generation_stage(cpi)) {
    av1_get_second_pass_params(cpi, &frame_params);
  }

  struct lookahead_entry *source = NULL;
  struct lookahead_entry *last_source = NULL;
#if CONFIG_BRU
  struct lookahead_entry *bru_ref_source = NULL;
#endif  // CONFIG_BRU
  if (frame_params.show_existing_frame) {
    source = av1_lookahead_pop(cpi->lookahead, flush, cpi->compressor_stage);
    frame_params.show_frame = 1;
  } else {
    source = choose_frame_source(cpi, &flush, &last_source,
#if CONFIG_BRU  // use -2 distance frame as BRU ref frame
                                 -(BRU_ENC_LOOKAHEAD_DIST_MINUS_1 + 1),
                                 &bru_ref_source,
#endif  // CONFIG_BRU
                                 &frame_params);
  }

  if (source == NULL) {  // If no source was found, we can't encode a frame.
    if (flush && oxcf->pass == 1 && !cpi->twopass.first_pass_done) {
      av1_end_first_pass(cpi); /* get last stats packet */
      cpi->twopass.first_pass_done = 1;
    }
    return -1;
  }
  // Source may be changed if temporal filtered later.
  frame_input.source = &source->img;
  frame_input.last_source = last_source != NULL ? &last_source->img : NULL;
#if CONFIG_BRU
  // prepare bru ref source
  frame_input.bru_ref_source =
      bru_ref_source != NULL ? &bru_ref_source->img : NULL;
  if (bru_ref_source) {
    cpi->common.bru.update_ref_idx = bru_ref_source->order_hint;
    cpi->common.bru.ref_order = bru_ref_source->order_hint;
  }
#endif  // CONFIG_BRU
  frame_input.ts_duration = source->ts_end - source->ts_start;
  // Save unfiltered source. It is used in av1_get_second_pass_params().
  cpi->unfiltered_source = frame_input.source;

  *time_stamp = source->ts_start;
  *time_end = source->ts_end;
  if (source->ts_start < cpi->time_stamps.first_ever) {
    cpi->time_stamps.first_ever = source->ts_start;
    cpi->time_stamps.prev_end_seen = source->ts_start;
  }

  if (!is_stat_generation_stage(cpi) &&
      cpi->tpl_data.tpl_stats_pool[0] == NULL) {
    setup_tpl_buffers(cm, &cpi->tpl_data, cpi->oxcf.algo_cfg.enable_tpl_model,
                      oxcf->gf_cfg.lag_in_frames);
  }

  av1_apply_encoding_flags(cpi, source->flags);
  if (!frame_params.show_existing_frame)
    *frame_flags = (source->flags & AOM_EFLAG_FORCE_KF) ? FRAMEFLAGS_KEY : 0;

  // Shown frames and arf-overlay frames need frame-rate considering
  if (frame_params.show_frame)
    adjust_frame_rate(cpi, source->ts_start, source->ts_end);

  if (!frame_params.show_existing_frame) {
    if (cpi->film_grain_table) {
      cm->cur_frame->film_grain_params_present = aom_film_grain_table_lookup(
          cpi->film_grain_table, *time_stamp, *time_end, 0 /* =erase */,
          &cm->film_grain_params);
    } else {
      cm->cur_frame->film_grain_params_present =
          cm->seq_params.film_grain_params_present;
    }
    // only one operating point supported now
    const int64_t pts64 = ticks_to_timebase_units(timestamp_ratio, *time_stamp);
    if (pts64 < 0 || pts64 > UINT32_MAX) return AOM_CODEC_ERROR;
    cm->frame_presentation_time = (uint32_t)pts64;
  }

  FRAME_UPDATE_TYPE frame_update_type = get_frame_update_type(gf_group);

  if (frame_params.show_existing_frame &&
      frame_params.frame_type != KEY_FRAME) {
    // Force show-existing frames to be INTER, except forward keyframes
    frame_params.frame_type = INTER_FRAME;
  }

  // TODO(david.turner@argondesign.com): Move all the encode strategy
  // (largely near av1_get_compressed_data) in here

  // TODO(david.turner@argondesign.com): Change all the encode strategy to
  // modify frame_params instead of cm or cpi.

  // Per-frame encode speed.  In theory this can vary, but things may have
  // been written assuming speed-level will not change within a sequence, so
  // this parameter should be used with caution.
  frame_params.speed = oxcf->speed;

  // Work out some encoding parameters specific to the pass:
  if (has_no_stats_stage(cpi) && oxcf->q_cfg.aq_mode == CYCLIC_REFRESH_AQ) {
    av1_cyclic_refresh_update_parameters(cpi);
  } else if (is_stat_generation_stage(cpi)) {
    cpi->td.mb.e_mbd.lossless[0] = is_lossless_requested(&oxcf->rc_cfg);
    const int kf_requested = (cm->current_frame.frame_number == 0 ||
                              (*frame_flags & FRAMEFLAGS_KEY));
    if (kf_requested && frame_update_type != OVERLAY_UPDATE &&
        frame_update_type != KFFLT_OVERLAY_UPDATE &&
        frame_update_type != INTNL_OVERLAY_UPDATE) {
      frame_params.frame_type = KEY_FRAME;
    } else {
      frame_params.frame_type = INTER_FRAME;
    }
  } else if (is_stat_consumption_stage(cpi)) {
#if CONFIG_MISMATCH_DEBUG
    mismatch_move_frame_idx_w(
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
        !cm->seq_params.enable_frame_output_order ||
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
        !frame_params.show_existing_frame);
#endif  // CONFIG_MISMATCH_DEBUG
#if TXCOEFF_COST_TIMER
    cm->txcoeff_cost_timer = 0;
    cm->txcoeff_cost_count = 0;
#endif
  }
#if CONFIG_BRU
  if (frame_params.frame_type == KEY_FRAME) {
    source->order_hint = 0;
  }
#endif
  if (frame_params.frame_type == KEY_FRAME) cm->showable_frame = 0;

#if CONFIG_MISMATCH_DEBUG
  if (has_no_stats_stage(cpi)) {
    mismatch_move_frame_idx_w(
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
        !cm->seq_params.enable_frame_output_order ||
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
        !frame_params.show_existing_frame);
  }
#endif  // CONFIG_MISMATCH_DEBUG

  if (!is_stat_generation_stage(cpi))
    set_ext_overrides(cm, &frame_params, ext_flags);

  // Shown keyframes and S frames refresh all reference buffers
  const int force_refresh_all =
      ((frame_params.frame_type == KEY_FRAME && frame_params.show_frame) ||
       frame_params.frame_type == S_FRAME) &&
      !frame_params.show_existing_frame;

  (void)force_refresh_all;
  av1_configure_buffer_updates(cpi, frame_update_type);

  const int order_offset = gf_group->arf_src_offset[gf_group->index];
  const int cur_frame_disp =
      cpi->common.current_frame.frame_number + order_offset;

  // Here, if temporal_layer_id is set to a non-zero value (pry_level),
  // temporal_layer_id is signaled in obu extension,
  // and affect reference list construction in both encoder and decoder.
  // Otherwise (if temporal_layer_id is set to 0), temporal_layer_id is
  // not signaled and does not change the reference frame list construction.
  cm->current_frame.order_hint = cur_frame_disp;
  cm->current_frame.display_order_hint = cur_frame_disp;
  cm->current_frame.pyramid_level = get_true_pyr_level(
      cpi->gf_group.layer_depth[cpi->gf_group.index], cur_frame_disp,
      cpi->gf_group.max_layer_depth,
      cpi->gf_group.update_type[cpi->gf_group.index] == KFFLT_OVERLAY_UPDATE);

#if CONFIG_NEW_OBU_HEADER
  cm->tlayer_id = 0;
  cm->current_frame.temporal_layer_id = cm->tlayer_id;
#else
  cm->temporal_layer_id = 0;
  cm->current_frame.temporal_layer_id = cm->temporal_layer_id;
#endif  // CONFIG_NEW_OBU_HEADER

  init_ref_map_pair(&cpi->common, cm->ref_frame_map_pairs,
                    gf_group->update_type[gf_group->index] == KF_UPDATE);

  if (!is_stat_generation_stage(cpi)) {
    cm->current_frame.frame_type = frame_params.frame_type;
    cm->features.error_resilient_mode = frame_params.error_resilient_mode;
#if CONFIG_BRU
    // get last frame idx as bru frame
    cm->bru.enabled = cpi->oxcf.tool_cfg.enable_bru > 0 &&
                      (frame_params.frame_type == INTER_FRAME);
    cm->bru.frame_inactive_flag = 0;
    if (cm->bru.enabled
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
        && cm->seq_params.order_hint_info.enable_order_hint
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
    ) {
      int n_future = 0;
      for (int i = 0; i < REF_FRAMES; i++) {
        const RefCntBuffer *const buf = cm->ref_frame_map[i];
        if (buf) {
          int ref_disp = (int)buf->display_order_hint;
          const int disp_diff = get_relative_dist(
              &cm->seq_params.order_hint_info, cur_frame_disp, ref_disp);
          if (disp_diff < 0) {
            n_future++;
            break;
          }
        }
      }
      if (n_future > 0) {
        cm->bru.enabled = 0;
      }
    }
    if (cm->bru.enabled && frame_input.bru_ref_source != NULL &&
        !frame_is_intra_only(&cpi->common)) {
      active_region_detection(cpi, frame_input.source,
                              frame_input.bru_ref_source);
      //  disable bru if
      //  1. too many active regions
      //  2. active ratio too large > 50%
      const int num_active_region = bru_get_num_of_active_region(&cpi->common);
      if (num_active_region > MAX_ACTIVE_REGION) {
        cm->bru.blocks_skipped = 0;
      } else if (cm->bru.blocks_skipped * 100 / cm->bru.total_units <
                 BRU_OFF_RATIO) {
        cm->bru.blocks_skipped = 0;
      }
      if (cm->bru.blocks_skipped == 0) {
        cm->bru.enabled = 0;
        cm->bru.update_ref_idx = -1;
      }
      cm->bru.frame_inactive_flag =
          (cm->bru.blocks_skipped == cm->bru.total_units);
    } else {
      cm->bru.enabled = 0;
      cm->bru.update_ref_idx = -1;
    }
    // clean up active sb queue if any left over
    // it may happen if encoder decide not using BRU for lots of active sbs
    // exists
    if (cm->bru.enabled == 0 && cm->bru.active_mode_map) {
      memset(cm->bru.active_mode_map, 2, sizeof(uint8_t) * cm->bru.total_units);
      for (uint32_t r = 0; r < cm->bru.num_active_regions; r++) {
        ARD_Queue *q = cpi->enc_act_sb_queue[r];
        if (q == NULL) continue;
        // make sure every queue is dumpped
        while (!ard_is_queue_empty(q)) {
          ard_dequeue(q);
        }
        // after dump, free the ARD_Queue structure
        free(q);
        cpi->enc_act_sb_queue[r] = NULL;
      }
    } else {
      cm->features.tip_frame_mode = TIP_FRAME_DISABLED;
    }
#endif  // CONFIG_BRU
    if (cm->seq_params.explicit_ref_frame_map)
      av1_get_ref_frames_enc(cm, cur_frame_disp, cm->ref_frame_map_pairs);
    else
      av1_get_ref_frames(cm, cur_frame_disp, cm->ref_frame_map_pairs);
#if CONFIG_BRU
    if (!cm->seq_params.explicit_ref_frame_map && cm->bru.enabled) {
      const int num_past_refs = cm->ref_frames_info.num_past_refs;
      if (cm->bru.ref_order >= 0) {
        cm->bru.update_ref_idx = -1;
        cm->bru.explicit_ref_idx = -1;
        for (int i = 0; i < num_past_refs; i++) {
          const int ref_list_order =
              cm->ref_frame_map[cm->remapped_ref_idx[i]]->order_hint;
          if (ref_list_order == cm->bru.ref_order) {
            cm->bru.update_ref_idx = i;
            cm->bru.explicit_ref_idx = cm->remapped_ref_idx[i];
            break;
          }
        }
      }
      if (cm->bru.update_ref_idx < 0) {
        cm->bru.enabled = 0;
        cm->bru.frame_inactive_flag = 0;
      }
    }
    if (cm->bru.frame_inactive_flag) {
      cm->features.refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;
      const RefCntBuffer *bru_ref_buf =
          get_ref_frame_buf(cm, cm->bru.update_ref_idx);
      cm->quant_params.base_qindex = bru_ref_buf->base_qindex;
      if (av1_num_planes(cm) > 1) {
        cm->quant_params.u_ac_delta_q = bru_ref_buf->u_ac_delta_q;
        cm->quant_params.v_ac_delta_q = bru_ref_buf->v_ac_delta_q;
      } else {
        cm->quant_params.v_ac_delta_q = cm->quant_params.u_ac_delta_q = 0;
      }
      cm->cur_frame->base_qindex = cm->quant_params.base_qindex;
      cm->cur_frame->u_ac_delta_q = cm->quant_params.u_ac_delta_q;
      cm->cur_frame->v_ac_delta_q = cm->quant_params.v_ac_delta_q;
    }
#endif  // CONFIG_BRU
    cm->ref_frames_info.num_same_ref_compound =
        AOMMIN(cm->seq_params.num_same_ref_compound,
               cm->ref_frames_info.num_total_refs);
#if CONFIG_IMPROVED_GLOBAL_MOTION
    cm->cur_frame->num_ref_frames = cm->ref_frames_info.num_total_refs;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

    // ref_frame_flags is defined based on the external flag
    // max-reference-frames.
    frame_params.ref_frame_flags =
        (1 << cpi->common.ref_frames_info.num_total_refs) - 1;

    frame_params.order_offset = gf_group->arf_src_offset[gf_group->index];

    if (!is_stat_generation_stage(cpi) &&
        use_subgop_cfg(&cpi->gf_group, cpi->gf_group.index) &&
        (frame_update_type != KF_UPDATE && frame_update_type != KFFLT_UPDATE)) {
      get_gop_cfg_enabled_refs(cpi, &frame_params.ref_frame_flags,
                               frame_params.order_offset);
    }

    frame_params.refresh_frame_flags = av1_get_refresh_frame_flags(
        cpi, &frame_params, frame_update_type, cpi->gf_group.index,
        cur_frame_disp, cm->ref_frame_map_pairs);

    frame_params.existing_fb_idx_to_show = INVALID_IDX;
    // Find the frame buffer to show based on display order
    if (frame_params.show_existing_frame) {
      for (int frame = 0; frame < cm->seq_params.ref_frames; frame++) {
        const RefCntBuffer *const buf = cm->ref_frame_map[frame];
        if (buf == NULL) continue;
        const int frame_order = (int)buf->display_order_hint;
        if (frame_order == cur_frame_disp)
          frame_params.existing_fb_idx_to_show = frame;
      }
    }
  }

  // The way frame_params->remapped_ref_idx is setup is a placeholder.
  // Currently, reference buffer assignment is done by update_ref_frame_map()
  // which is called by high-level strategy AFTER encoding a frame.  It
  // modifies cm->remapped_ref_idx.  If you want to use an alternative method
  // to determine reference buffer assignment, just put your assignments into
  // frame_params->remapped_ref_idx here and they will be used when encoding
  // this frame.  If frame_params->remapped_ref_idx is setup independently of
  // cm->remapped_ref_idx then update_ref_frame_map() will have no effect.
  memcpy(frame_params.remapped_ref_idx, cm->remapped_ref_idx,
         REF_FRAMES * sizeof(*cm->remapped_ref_idx));
#if CONFIG_BRU
  init_bru_frame(cm);
#endif  // CONFIG_BRU

  cpi->td.mb.delta_qindex = 0;

  if (!frame_params.show_existing_frame) {
    cm->quant_params.using_qmatrix = oxcf->q_cfg.using_qm;
    av1_set_lr_tools(cm->seq_params.lr_tools_disable_mask[0], 0, &cm->features);
    av1_set_lr_tools(cm->seq_params.lr_tools_disable_mask[1], 1, &cm->features);
    av1_set_lr_tools(cm->seq_params.lr_tools_disable_mask[1], 2, &cm->features);
  }
  if (denoise_and_encode(cpi, dest, &frame_input, &frame_params,
                         &frame_results) != AOM_CODEC_OK) {
    return AOM_CODEC_ERROR;
  }

  if (!is_stat_generation_stage(cpi)) {
    // First pass doesn't modify reference buffer assignment or produce frame
    // flags
    update_frame_flags(&cpi->common, frame_flags);
  }

  if (!is_stat_generation_stage(cpi)) {
#if TXCOEFF_COST_TIMER
    cm->cum_txcoeff_cost_timer += cm->txcoeff_cost_timer;
    fprintf(stderr,
            "\ntxb coeff cost block number: %ld, frame time: %ld, cum time %ld "
            "in us\n",
            cm->txcoeff_cost_count, cm->txcoeff_cost_timer,
            cm->cum_txcoeff_cost_timer);
#endif
    if (!has_no_stats_stage(cpi)) av1_twopass_postencode_update(cpi);
  }

#if CONFIG_TUNE_VMAF
  if (!is_stat_generation_stage(cpi) &&
      (oxcf->tune_cfg.tuning >= AOM_TUNE_VMAF_WITH_PREPROCESSING &&
       oxcf->tune_cfg.tuning <= AOM_TUNE_VMAF_NEG_MAX_GAIN)) {
    av1_update_vmaf_curve(cpi);
  }
#endif

  if (!is_stat_generation_stage(cpi)) {
    set_additional_frame_flags(cm, frame_flags);
    update_rc_counts(cpi);
  }

  // Unpack frame_results:
  *size = frame_results.size;

  // Leave a signal for a higher level caller about if this frame is droppable
  if (*size > 0) {
    cpi->droppable = is_frame_droppable(&ext_flags->refresh_frame);
  }

  return AOM_CODEC_OK;
}

// Determine whether a frame is a keyframe arf. Will return 0 for fwd kf arf.
// Note it depends on frame_since_key and gf_group, therefore should be called
// after the gf group is defined, or otherwise a keyframe arf may still return
// 0.
int av1_check_keyframe_arf(int gf_index, GF_GROUP *gf_group,
                           int frame_since_key) {
  if (gf_index >= gf_group->size) return 0;
  (void)frame_since_key;
  return gf_group->update_type[gf_index] == KFFLT_UPDATE;
  /*
  return gf_group->update_type[gf_index] == ARF_UPDATE &&
         gf_group->update_type[gf_index + 1] == OVERLAY_UPDATE &&
         frame_since_key == 0;
         */
}

// Determine whether a frame is a keyframe overlay (will also return 0 for fwd
// kf overlays).
int av1_check_keyframe_overlay(int gf_index, GF_GROUP *gf_group,
                               int frame_since_key) {
  if (gf_index < 1) return 0;
  (void)frame_since_key;
  return gf_group->update_type[gf_index] == KFFLT_OVERLAY_UPDATE;
  /*
  return gf_group->update_type[gf_index - 1] == ARF_UPDATE &&
         gf_group->update_type[gf_index] == OVERLAY_UPDATE &&
         frame_since_key == 0;
         */
}
