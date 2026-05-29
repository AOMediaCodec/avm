/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_COMMON_XLAYER_CONFIG_H_
#define AVM_COMMON_XLAYER_CONFIG_H_

#include <limits.h>
#include <stdint.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "av2/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_INPUT_SOURCES 8
#define MAX_SOURCE_NAME_LEN 64
#define MAX_CODEC_CONTROLS 32

// Named input source for multi-source encoding
typedef struct InputSourceConfig {
  char name[MAX_SOURCE_NAME_LEN];
  char filename[PATH_MAX];
  unsigned int width;
  unsigned int height;
  int format;          // pixel format: 0=auto, 420, 422, 444
  int bit_depth;       // 0=auto (detect from file or default 8)
  int frame_rate_num;  // 0/0=auto (detect from Y4M or use global timebase)
  int frame_rate_den;
  int frame_skip;  // resolved: max_fps/this_fps (1=every TU, 2=every other)
} InputSourceConfig;

// Per-embedded-layer source and dependency configuration
typedef struct MLayerSourceConfig {
  char input_source_name[MAX_SOURCE_NAME_LEN];  // "" = inherit from xlayer
  int input_source_idx;  // resolved: -1 = inherit from xlayer
  int atlas_pos_x;       // crop origin X (-1 = inherit from xlayer)
  int atlas_pos_y;       // crop origin Y (-1 = inherit from xlayer)
  unsigned int width;    // crop width (0 = inherit from xlayer)
  unsigned int height;   // crop height (0 = inherit from xlayer)
  int dependency_mask;   // bitmask of lower mlayers this depends on
                         // (-1 = default linear chain)
  // Content Interpretation overrides (-1 = inherit from xlayer)
  int color_primaries;
  int transfer_characteristics;
  int matrix_coefficients;
  int full_range_flag;
} MLayerSourceConfig;

// Default mlayer dependency mask: linear chain where each mlayer depends on
// all lower mlayers.  mlayer 0 has mask 0 (no dependencies).
#define DEFAULT_MLAYER_DEP_MASK(m) ((m) > 0 ? (1 << (m)) - 1 : 0)

// Resolve a per-mlayer dependency mask, replacing the sentinel -1 with the
// default linear chain.
static inline int resolve_mlayer_dep_mask(const MLayerSourceConfig *ms, int m) {
  return (ms->dependency_mask >= 0) ? ms->dependency_mask
                                    : DEFAULT_MLAYER_DEP_MASK(m);
}

// Per extended-layer encoder configuration
typedef struct XLayerEncConfig {
  int xlayer_id;  // 0-30
  char input_filename[PATH_MAX];
  unsigned int width;
  unsigned int height;
  unsigned int profile;
  unsigned int tier;
  unsigned int level;
  int layer_type;      // TEXTURE_LAYER, AUX_LAYER, STEREO_LAYER, etc.
  int auxiliary_type;  // LCR_ALPHA_AUX, LCR_DEPTH_AUX, etc. (if AUX_LAYER)
  int view_type;       // VIEW_UNSPECIFIED, VIEW_LEFT, VIEW_RIGHT, etc.
  int num_temporal_layers;
  int num_embedded_layers;
  // Color info
  int color_primaries;
  int transfer_characteristics;
  int matrix_coefficients;
  int full_range_flag;
  // Encoder overrides (-1 = use global default)
  int qp;
  int bitrate;
  int cpu_used;
  int lag_in_frames;
  int sframe_dist;  // S-Frame interval (-1 = disabled/default)
  int sframe_mode;  // S-Frame insertion mode (-1 = default)
  int sframe_type;  // S-Frame type: 0=regular, 1=RAS (-1 = default)
  // Coding structure (-1 or empty = use global default)
  int kf_max_dist;                    // keyframe interval (-1 = default)
  char subgop_config_path[PATH_MAX];  // sub-GOP config file (empty = default)
  // GOP mode: 0=closed(CLK), 1=open_leading(OLK), 2=open_sef
  int gop_mode;
  int fwd_kf_enabled;             // override: -1=derive from gop_mode
  int enable_keyframe_filtering;  // override: -1=derive from gop_mode
  int add_sef_for_hidden_frames;  // override: -1=derive from gop_mode
  // Atlas layout position in composite canvas (-1 = auto)
  int atlas_pos_x;
  int atlas_pos_y;
  // Input source reference (for multi-source encoding)
  char input_source_name[MAX_SOURCE_NAME_LEN];  // references InputSourceConfig
  int input_source_idx;  // resolved index into input_sources[] (-1 = own file)
  // Scaling for embedded layers
  int scaling_mode[MAX_NUM_MLAYERS];
  // Per-embedded-layer source and dependency configuration
  MLayerSourceConfig mlayer_sources[MAX_NUM_MLAYERS];
  int has_per_mlayer_sources;   // 1 if any mlayer has its own source/crop
  int has_mlayer_dependencies;  // 1 if any mlayer has explicit dependency_mask
  // Generic post-init codec controls from JSON "codec_controls" array
  int num_codec_controls;
  struct {
    char name[64];
    int value;
  } codec_controls[MAX_CODEC_CONTROLS];
} XLayerEncConfig;

// Per operating-point configuration within an OPS set
typedef struct OperatingPointConfig {
  int intent;           // OPS intent (display, monitoring, etc.)
  uint32_t xlayer_map;  // bitmask of xlayers included in this OP
  // Per-xlayer within this OP
  int mlayer_count[MAX_NUM_XLAYERS];  // embedded layers per xlayer (0=all)
  int tlayer_count[MAX_NUM_XLAYERS];  // temporal layers per xlayer (0=all)
  // PTL overrides for this OP
  int aggregate_level_idx;  // -1 = derive from constituent layers
  int max_tier_flag;        // -1 = derive
  // Per-xlayer embedded OPS references
  int embedded_ops_id[MAX_NUM_XLAYERS];    // -1 = not set
  int embedded_op_index[MAX_NUM_XLAYERS];  // -1 = not set
} OperatingPointConfig;

// OPS set configuration (one per OPS OBU)
typedef struct OPSConfig {
  int enable;
  int ops_id;    // OPS ID (0-15)
  int priority;  // OPS priority
  int intent_present_flag;
  int ptl_present_flag;
  int color_info_present_flag;
  int mlayer_info_idc;  // 0=no info, 1=same, 2=explicit
  int num_operating_points;
  OperatingPointConfig ops[MAX_OPS_COUNT];
} OPSConfig;

// Top-level multi-xlayer configuration
typedef struct MultiXLayerConfig {
  int num_xlayers;
  XLayerEncConfig xlayers[MAX_NUM_XLAYERS - 1];  // up to 31
  // Global LCR
  int enable_global_lcr;
  int lcr_purpose_id;
  int lcr_dependent_xlayers_flag;
  int lcr_doh_constraint_flag;
  // Local LCR
  int enable_local_lcr;
  int local_lcr_mode;  // 0 = both (Global+Local, identical xlayer_info)
                       // 1 = local_only (Global without payload, Local is
                       //     authoritative)
  // MSDO
  int enable_msdo;
  // Atlas
  int enable_atlas;
  int atlas_mode;
  int atlas_width;            // canvas width (0 = derive from xlayers)
  int atlas_height;           // canvas height (0 = derive)
  int atlas_uniform_spacing;  // 1 = auto-grid, 0 = explicit positions
  // OPS
  int num_ops_sets;
  OPSConfig ops_sets[MAX_NUM_OPS_ID];
  // Shared source (for subpicture encoding from single input)
  char source_filename[PATH_MAX];  // shared source file (empty = disabled)
  unsigned int source_width;       // source resolution (0 = derive from file)
  unsigned int source_height;
  // Named input sources (replaces single source for multi-source encoding)
  int num_input_sources;
  InputSourceConfig input_sources[MAX_INPUT_SOURCES];
  // Bitstream
  int combined_tu;
  int monotonic_output_order;
  double frame_rate;  // 0 = use main encoder timebase (default)
  int limit;          // max frames to encode (0 = unlimited)
  char output_filename[PATH_MAX];
} MultiXLayerConfig;

// Initialize config with defaults
static inline void xlayer_config_init(MultiXLayerConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->enable_global_lcr = 1;
  cfg->lcr_doh_constraint_flag = 1;
  cfg->atlas_uniform_spacing = 1;
  cfg->combined_tu = 1;
  cfg->monotonic_output_order = 1;
  for (int i = 0; i < MAX_NUM_XLAYERS - 1; i++) {
    cfg->xlayers[i].xlayer_id = -1;
    cfg->xlayers[i].qp = -1;
    cfg->xlayers[i].bitrate = -1;
    cfg->xlayers[i].cpu_used = -1;
    cfg->xlayers[i].lag_in_frames = -1;
    cfg->xlayers[i].sframe_dist = -1;
    cfg->xlayers[i].sframe_mode = -1;
    cfg->xlayers[i].sframe_type = -1;
    cfg->xlayers[i].kf_max_dist = -1;
    cfg->xlayers[i].fwd_kf_enabled = -1;
    cfg->xlayers[i].enable_keyframe_filtering = -1;
    cfg->xlayers[i].add_sef_for_hidden_frames = -1;
    cfg->xlayers[i].atlas_pos_x = -1;
    cfg->xlayers[i].atlas_pos_y = -1;
    cfg->xlayers[i].input_source_idx = -1;
    cfg->xlayers[i].profile = MAIN_420_10_IP1;
    cfg->xlayers[i].level = SEQ_LEVEL_4_0;
    cfg->xlayers[i].num_temporal_layers = 1;
    cfg->xlayers[i].num_embedded_layers = 1;
    cfg->xlayers[i].view_type = VIEW_UNSPECIFIED;
    for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
      cfg->xlayers[i].mlayer_sources[j].input_source_idx = -1;
      cfg->xlayers[i].mlayer_sources[j].atlas_pos_x = -1;
      cfg->xlayers[i].mlayer_sources[j].atlas_pos_y = -1;
      cfg->xlayers[i].mlayer_sources[j].width = 0;
      cfg->xlayers[i].mlayer_sources[j].height = 0;
      cfg->xlayers[i].mlayer_sources[j].dependency_mask = -1;
      cfg->xlayers[i].mlayer_sources[j].color_primaries = -1;
      cfg->xlayers[i].mlayer_sources[j].transfer_characteristics = -1;
      cfg->xlayers[i].mlayer_sources[j].matrix_coefficients = -1;
      cfg->xlayers[i].mlayer_sources[j].full_range_flag = -1;
    }
  }
  for (int i = 0; i < MAX_NUM_OPS_ID; i++) {
    for (int j = 0; j < MAX_OPS_COUNT; j++) {
      cfg->ops_sets[i].ops[j].aggregate_level_idx = -1;
      cfg->ops_sets[i].ops[j].max_tier_flag = -1;
      for (int k = 0; k < MAX_NUM_XLAYERS; k++) {
        cfg->ops_sets[i].ops[j].embedded_ops_id[k] = -1;
        cfg->ops_sets[i].ops[j].embedded_op_index[k] = -1;
      }
    }
  }
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_XLAYER_CONFIG_H_
