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

#include "av1/common/av1_common_int.h"

#include "av1/encoder/encoder.h"
#include "av1/encoder/firstpass.h"
#include "av1/encoder/gop_structure.h"
#include "av1/encoder/subgop.h"

// Set parameters for frames between 'start' and 'end' (excluding both).
static void set_multi_layer_params(const TWO_PASS *twopass,
                                   GF_GROUP *const gf_group, RATE_CONTROL *rc,
                                   FRAME_INFO *frame_info, int start, int end,
                                   int *cur_frame_idx, int *frame_ind,
                                   int layer_depth) {
  const int num_frames_to_process = end - start;

  // Either we are at the last level of the pyramid, or we don't have enough
  // frames between 'l' and 'r' to create one more level.
  if (layer_depth > gf_group->max_layer_depth_allowed ||
      num_frames_to_process < 3) {
    // Leaf nodes.
    while (start < end) {
      gf_group->update_type[*frame_ind] = LF_UPDATE;
      gf_group->arf_src_offset[*frame_ind] = 0;
      gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
      gf_group->layer_depth[*frame_ind] = MAX_ARF_LAYERS;
      gf_group->arf_boost[*frame_ind] = av1_calc_arf_boost(
          twopass, rc, frame_info, start, end - start, 0, NULL, NULL);
      gf_group->max_layer_depth =
          AOMMAX(gf_group->max_layer_depth, layer_depth);
      ++(*frame_ind);
      ++(*cur_frame_idx);
      ++start;
    }
  } else {
    const int m = (start + end - 1) / 2;

    // Internal ARF.
    gf_group->update_type[*frame_ind] = INTNL_ARF_UPDATE;
    gf_group->arf_src_offset[*frame_ind] = m - start;
    gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
    gf_group->layer_depth[*frame_ind] = layer_depth;

    // Get the boost factor for intermediate ARF frames.
    gf_group->arf_boost[*frame_ind] = av1_calc_arf_boost(
        twopass, rc, frame_info, m, end - m, m - start, NULL, NULL);
    ++(*frame_ind);

    // Frames displayed before this internal ARF.
    set_multi_layer_params(twopass, gf_group, rc, frame_info, start, m,
                           cur_frame_idx, frame_ind, layer_depth + 1);

    // Overlay for internal ARF.
    gf_group->update_type[*frame_ind] = INTNL_OVERLAY_UPDATE;
    gf_group->arf_src_offset[*frame_ind] = 0;
    gf_group->cur_frame_idx[*frame_ind] = *cur_frame_idx;
    gf_group->arf_boost[*frame_ind] = 0;
    gf_group->layer_depth[*frame_ind] = layer_depth;
    ++(*frame_ind);
    ++(*cur_frame_idx);

    // Frames displayed after this internal ARF.
    set_multi_layer_params(twopass, gf_group, rc, frame_info, m + 1, end,
                           cur_frame_idx, frame_ind, layer_depth + 1);
  }
}

static int find_backward_alt_ref(const SubGOPCfg *subgop_cfg, int cur_idx) {
  int cur_disp_idx = subgop_cfg->step[cur_idx].disp_frame_idx;
  int cur_pyr_level = subgop_cfg->step[cur_idx].pyr_level;
  int bwd_alt_ref_disp_idx = 0;

  for (int i = 0; i < cur_idx; ++i) {
    int disp_idx = subgop_cfg->step[i].disp_frame_idx;
    int pyr_level = subgop_cfg->step[i].pyr_level;
    if (pyr_level < cur_pyr_level && disp_idx < cur_disp_idx) {
      if (disp_idx > bwd_alt_ref_disp_idx) bwd_alt_ref_disp_idx = disp_idx;
    }
  }

  return bwd_alt_ref_disp_idx;
}

static int find_forward_alt_ref(const SubGOPCfg *subgop_cfg, int cur_idx) {
  int cur_disp_idx = subgop_cfg->step[cur_idx].disp_frame_idx;
  int cur_pyr_level = subgop_cfg->step[cur_idx].pyr_level;
  int fwd_alt_ref_disp_idx = subgop_cfg->num_frames;

  for (int i = 0; i < cur_idx; ++i) {
    int disp_idx = subgop_cfg->step[i].disp_frame_idx;
    int pyr_level = subgop_cfg->step[i].pyr_level;
    if (pyr_level < cur_pyr_level && disp_idx > cur_disp_idx) {
      if (disp_idx < fwd_alt_ref_disp_idx) fwd_alt_ref_disp_idx = disp_idx;
    }
  }

  return fwd_alt_ref_disp_idx;
}

static void set_multi_layer_params_from_subgop_cfg(
    const TWO_PASS *twopass, GF_GROUP *const gf_group,
    const SubGOPCfg *subgop_cfg, RATE_CONTROL *rc, FRAME_INFO *frame_info,
    int *cur_frame_idx, int *frame_index, int is_ld_map_first_gop) {
  int last_shown_frame = 0;
  int min_pyr_level = MAX_ARF_LAYERS;

  for (int idx = 0; idx < subgop_cfg->num_steps; ++idx) {
    const SubGOPStepCfg *frame = &subgop_cfg->step[idx];

    if (frame->pyr_level < min_pyr_level) min_pyr_level = frame->pyr_level;
  }

  for (int idx = 0; idx < subgop_cfg->num_steps; ++idx) {
    const SubGOPStepCfg *frame = &subgop_cfg->step[idx];
    FRAME_TYPE_CODE type = frame->type_code;
    int pyr_level = frame->pyr_level;
    int disp_idx = frame->disp_frame_idx;
    gf_group->cur_frame_idx[*frame_index] = *cur_frame_idx;

    if (type == FRAME_TYPE_OOO_FILTERED ||
        type == FRAME_TYPE_OOO_UNFILTERED) {  // ARF
      gf_group->update_type[*frame_index] =
          pyr_level == min_pyr_level ? ARF_UPDATE : INTNL_ARF_UPDATE;
      if (pyr_level == min_pyr_level) {
        gf_group->arf_index = *frame_index;
        gf_group->arf_boost[*frame_index] = rc->gfu_boost;
      } else {
        int fwd_arf_disp_idx = find_forward_alt_ref(subgop_cfg, idx);
        int bwd_arf_disp_idx = find_backward_alt_ref(subgop_cfg, idx);
        gf_group->arf_boost[*frame_index] = av1_calc_arf_boost(
            twopass, rc, frame_info, disp_idx, fwd_arf_disp_idx - disp_idx,
            disp_idx - bwd_arf_disp_idx, NULL, NULL);
      }
      gf_group->arf_src_offset[*frame_index] = disp_idx - last_shown_frame - 1;
    } else if (type == FRAME_TYPE_INO_VISIBLE) {  // Leaf
      gf_group->update_type[*frame_index] =
          (pyr_level == min_pyr_level && !is_ld_map_first_gop) ? GF_UPDATE
                                                               : LF_UPDATE;

      int fwd_arf_disp_idx = find_forward_alt_ref(subgop_cfg, idx);
      gf_group->arf_boost[*frame_index] =
          av1_calc_arf_boost(twopass, rc, frame_info, disp_idx,
                             fwd_arf_disp_idx - disp_idx, 0, NULL, NULL);
      gf_group->arf_src_offset[*frame_index] = 0;
      last_shown_frame = disp_idx;

      (*cur_frame_idx)++;
    } else if (type == FRAME_TYPE_INO_REPEAT ||
               type == FRAME_TYPE_INO_SHOWEXISTING) {  // Overlay
      gf_group->update_type[*frame_index] =
          pyr_level == min_pyr_level ? OVERLAY_UPDATE : INTNL_OVERLAY_UPDATE;
      gf_group->arf_boost[*frame_index] = 0;
      gf_group->arf_src_offset[*frame_index] = 0;
      last_shown_frame = disp_idx;
      (*cur_frame_idx)++;
    }
    gf_group->is_filtered[*frame_index] = (type == FRAME_TYPE_OOO_FILTERED);
    gf_group->layer_depth[*frame_index] = pyr_level;
    gf_group->max_layer_depth = AOMMAX(gf_group->max_layer_depth, pyr_level);

    if (idx != (subgop_cfg->num_steps - 1)) (*frame_index)++;
  }
  for (int idx = 0; idx <= *frame_index; idx++) {
    if (gf_group->layer_depth[idx] == gf_group->max_layer_depth)
      gf_group->layer_depth[idx] = MAX_ARF_LAYERS;
  }
}

static const SubGOPCfg *get_subgop_config(SubGOPSetCfg *config_set,
                                          int num_frames, int is_last_subgop,
                                          int is_first_subgop, int use_alt_ref,
                                          int *is_ld_map) {
  const SubGOPCfg *cfg = av1_find_subgop_config(
      config_set, num_frames, is_last_subgop, is_first_subgop);
  if (!cfg) {
    *is_ld_map = 0;
    return NULL;
  }

  // Check if the configuration map is low-delay
  for (int idx = 0; idx < cfg->num_steps; ++idx) {
    const SubGOPStepCfg *frame = &cfg->step[idx];
    FRAME_TYPE_CODE type = frame->type_code;
    if (type == FRAME_TYPE_OOO_FILTERED || type == FRAME_TYPE_OOO_UNFILTERED) {
      *is_ld_map = 0;
      break;
    }
  }

  /*
   * Enable subgop configuration path only when altrefs are
   * enabled for non low-delay configurations and for low-delay
   * configurations.
   */
  if (cfg && (use_alt_ref || *is_ld_map))
    return cfg;
  else
    return NULL;
}

static int construct_multi_layer_gf_structure(
    AV1_COMP *cpi, TWO_PASS *twopass, GF_GROUP *const gf_group,
    RATE_CONTROL *rc, FRAME_INFO *const frame_info, int gf_interval,
    FRAME_UPDATE_TYPE first_frame_update_type) {
  int frame_index = 0;
  int cur_frame_index = 0;

  assert(first_frame_update_type == KF_UPDATE ||
         first_frame_update_type == ARF_UPDATE ||
         first_frame_update_type == GF_UPDATE);
  const int use_altref = gf_group->max_layer_depth_allowed > 0;
  SubGOPSetCfg *subgop_cfg_set = &cpi->subgop_config_set;
  gf_group->subgop_cfg = NULL;
  gf_group->is_user_specified = 0;
  const SubGOPCfg *subgop_cfg;
  int is_ld_map = 1;

  subgop_cfg = get_subgop_config(
      subgop_cfg_set, gf_interval, rc->frames_to_key <= gf_interval + 2,
      first_frame_update_type == KF_UPDATE, use_altref, &is_ld_map);
  int is_ld_map_first_gop = is_ld_map && first_frame_update_type == KF_UPDATE;

#if CONFIG_KEY_OVERLAY
  if (first_frame_update_type == KF_UPDATE &&
      cpi->oxcf.kf_cfg.enable_keyframe_filtering > 1 &&
      has_enough_frames_for_key_filtering(cpi->rc.frames_to_key,
                                          cpi->oxcf.algo_cfg.arnr_max_frames,
                                          cpi->oxcf.gf_cfg.lag_in_frames)) {
#else
  if (first_frame_update_type == KF_UPDATE &&
      cpi->oxcf.kf_cfg.enable_keyframe_filtering > 1) {
#endif  // CONFIG_KEY_OVERLAY

    gf_group->has_overlay_for_key_frame = 1;
    gf_group->update_type[frame_index] = KFFLT_UPDATE;
    gf_group->arf_src_offset[frame_index] = 0;
    gf_group->cur_frame_idx[frame_index] = cur_frame_index;
    gf_group->layer_depth[frame_index] = 0;
    gf_group->max_layer_depth = 0;
    ++frame_index;

    gf_group->update_type[frame_index] = KFFLT_OVERLAY_UPDATE;
    gf_group->arf_src_offset[frame_index] = 0;
    gf_group->cur_frame_idx[frame_index] = cur_frame_index;
#if CONFIG_KEY_OVERLAY
    gf_group->layer_depth[frame_index] = MAX_ARF_LAYERS;
    gf_group->arf_boost[frame_index] = NORMAL_BOOST;
#else
    gf_group->layer_depth[frame_index] = 0;
    gf_group->max_layer_depth = 0;
#endif  // CONFIG_KEY_OVERLAY
    ++frame_index;
    cur_frame_index++;
  } else if ((first_frame_update_type != ARF_UPDATE && !is_ld_map) ||
             is_ld_map_first_gop) {
    gf_group->update_type[frame_index] = first_frame_update_type;
    gf_group->arf_src_offset[frame_index] = 0;
    gf_group->cur_frame_idx[frame_index] = cur_frame_index;
    gf_group->layer_depth[frame_index] = 0;
    gf_group->max_layer_depth = 0;
    ++frame_index;
    ++cur_frame_index;
  }

  if (subgop_cfg) {
    gf_group->subgop_cfg = subgop_cfg;
    gf_group->is_user_specified = 1;
    set_multi_layer_params_from_subgop_cfg(twopass, gf_group, subgop_cfg, rc,
                                           frame_info, &cur_frame_index,
                                           &frame_index, is_ld_map_first_gop);
    frame_index++;
  } else {
    if (first_frame_update_type == KF_UPDATE) gf_interval++;
    // ALTREF.
    if (use_altref) {
      gf_group->update_type[frame_index] = ARF_UPDATE;
      gf_group->arf_src_offset[frame_index] = gf_interval - cur_frame_index - 1;
      gf_group->cur_frame_idx[frame_index] = cur_frame_index;
      gf_group->layer_depth[frame_index] = 1;
      gf_group->arf_boost[frame_index] = cpi->rc.gfu_boost;
      gf_group->max_layer_depth = 1;
      gf_group->arf_index = frame_index;
      ++frame_index;
    } else {
      gf_group->arf_index = -1;
    }
    set_multi_layer_params(twopass, gf_group, rc, frame_info, cur_frame_index,
                           gf_interval - 1, &cur_frame_index, &frame_index,
                           use_altref + 1);
    if (use_altref) {
      gf_group->update_type[frame_index] = OVERLAY_UPDATE;
      gf_group->arf_src_offset[frame_index] = 0;
      gf_group->cur_frame_idx[frame_index] = cur_frame_index;
      gf_group->layer_depth[frame_index] = MAX_ARF_LAYERS;
      gf_group->arf_boost[frame_index] = NORMAL_BOOST;
      ++frame_index;
    } else {
      for (; cur_frame_index < gf_interval; ++cur_frame_index) {
        gf_group->update_type[frame_index] = LF_UPDATE;
        gf_group->arf_src_offset[frame_index] = 0;
        gf_group->cur_frame_idx[frame_index] = cur_frame_index;
        gf_group->layer_depth[frame_index] = MAX_ARF_LAYERS;
        gf_group->arf_boost[frame_index] = NORMAL_BOOST;
        gf_group->max_layer_depth = AOMMAX(gf_group->max_layer_depth, 2);
        ++frame_index;
      }
    }
  }

  return frame_index;
}

#define CHECK_GF_PARAMETER 0
#if CHECK_GF_PARAMETER
void check_frame_params(GF_GROUP *const gf_group, int gf_interval) {
  static const char *update_type_strings[FRAME_UPDATE_TYPES] = {
    "KF_UPDATE",        "LF_UPDATE",      "GF_UPDATE",
    "ARF_UPDATE",       "OVERLAY_UPDATE", "INTNL_OVERLAY_UPDATE",
    "INTNL_ARF_UPDATE", "KFFLT_UPDATE",   "KFFLT_OVERLAY_UPDATE",
  };
  FILE *fid = fopen("GF_PARAMS.txt", "a");

  fprintf(fid, "\ngf_interval = {%d}\n", gf_interval);
  for (int i = 0; i < gf_group->size; ++i) {
    fprintf(fid, "#%2d : %s %d %d %d %d\n", i,
            update_type_strings[gf_group->update_type[i]],
            gf_group->arf_src_offset[i], gf_group->arf_pos_in_gf[i],
            gf_group->arf_update_idx[i], gf_group->pyramid_level[i]);
  }

  fprintf(fid, "number of nodes in each level: \n");
  for (int i = 0; i < gf_group->pyramid_height; ++i) {
    fprintf(fid, "lvl %d: %d ", i, gf_group->pyramid_lvl_nodes[i]);
  }
  fprintf(fid, "\n");
  fclose(fid);
}
#endif  // CHECK_GF_PARAMETER

void av1_gop_setup_structure(AV1_COMP *cpi) {
  RATE_CONTROL *const rc = &cpi->rc;
  GF_GROUP *const gf_group = &cpi->gf_group;
  TWO_PASS *const twopass = &cpi->twopass;
  FRAME_INFO *const frame_info = &cpi->frame_info;
  const int key_frame = rc->frames_since_key == 0;
  const int use_altref = gf_group->max_layer_depth_allowed > 0;
  const FRAME_UPDATE_TYPE update_type = cpi->gf_state.arf_gf_boost_lst ||
                                                use_altref ||
                                                (rc->baseline_gf_interval == 1)
                                            ? ARF_UPDATE
                                            : GF_UPDATE;
  const FRAME_UPDATE_TYPE first_frame_update_type =
      key_frame ? KF_UPDATE : update_type;
  gf_group->is_user_specified = 0;
  gf_group->has_overlay_for_key_frame = 0;
  if (cpi->print_per_frame_stats) {
    printf("baseline_gf_interval = %d\n", rc->baseline_gf_interval);
  }
  gf_group->size = construct_multi_layer_gf_structure(
      cpi, twopass, gf_group, rc, frame_info, rc->baseline_gf_interval,
      first_frame_update_type);
  cpi->rc.level1_qp = -1;  // Set to uninitialized.

#if CHECK_GF_PARAMETER
  check_frame_params(gf_group, rc->baseline_gf_interval);
#endif
}
