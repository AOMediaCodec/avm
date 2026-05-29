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

#include "common/xlayer_config_parse.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avm/avmcx.h"
#include "third_party/cJSON/cJSON.h"

// Map layer type string to enum value
static int parse_layer_type(const char *str) {
  if (!str) return TEXTURE_LAYER;
  if (strcmp(str, "texture") == 0) return TEXTURE_LAYER;
  if (strcmp(str, "auxiliary") == 0) return AUX_LAYER;
  if (strcmp(str, "stereo") == 0) return STEREO_LAYER;
  if (strcmp(str, "dependent") == 0) return DEPENDENT_LAYER;
  fprintf(stderr, "Warning: unknown layer_type \"%s\", defaulting to texture\n",
          str);
  return TEXTURE_LAYER;
}

// Map auxiliary type string to enum value
static int parse_auxiliary_type(const char *str) {
  if (!str) return LCR_ALPHA_AUX;
  if (strcmp(str, "alpha") == 0) return LCR_ALPHA_AUX;
  if (strcmp(str, "depth") == 0) return LCR_DEPTH_AUX;
  if (strcmp(str, "segmentation") == 0) return LCR_SEGMENTATION_AUX;
  if (strcmp(str, "gain_map") == 0) return LCR_GAIN_MAP_AUX;
  fprintf(stderr,
          "Warning: unknown auxiliary_type \"%s\", defaulting to alpha\n", str);
  return LCR_ALPHA_AUX;
}

// Map view type string to enum value
static int parse_view_type(const char *str) {
  if (!str) return VIEW_UNSPECIFIED;
  if (strcmp(str, "unspecified") == 0) return VIEW_UNSPECIFIED;
  if (strcmp(str, "center") == 0) return VIEW_CENTER;
  if (strcmp(str, "left") == 0) return VIEW_LEFT;
  if (strcmp(str, "right") == 0) return VIEW_RIGHT;
  if (strcmp(str, "explicit") == 0) return VIEW_EXPLICIT;
  fprintf(stderr,
          "Warning: unknown view_type \"%s\", defaulting to "
          "unspecified\n",
          str);
  return VIEW_UNSPECIFIED;
}

// Map scaling mode string to enum value, returns -1 on error
static int parse_scaling_mode(const char *str) {
  if (!str) return -1;
  if (strcmp(str, "1:1") == 0 || strcmp(str, "normal") == 0) return AVME_NORMAL;
  if (strcmp(str, "4/5") == 0) return AVME_FOURFIVE;
  if (strcmp(str, "3/5") == 0) return AVME_THREEFIVE;
  if (strcmp(str, "3/4") == 0) return AVME_THREEFOUR;
  if (strcmp(str, "1/4") == 0) return AVME_ONEFOUR;
  if (strcmp(str, "1/8") == 0) return AVME_ONEEIGHT;
  if (strcmp(str, "1/2") == 0) return AVME_ONETWO;
  fprintf(stderr, "Warning: unknown scaling_mode \"%s\"\n", str);
  return -1;
}

// Map GOP mode string to integer value
static int parse_gop_mode(const char *str) {
  if (!str) return 0;
  if (strcmp(str, "closed") == 0) return 0;
  if (strcmp(str, "open_leading") == 0) return 1;
  if (strcmp(str, "open_sef") == 0) return 2;
  fprintf(stderr, "Warning: unknown gop_mode \"%s\", defaulting to closed\n",
          str);
  return 0;
}

// Map chroma format string to integer (420, 422, 444). Returns 0 on error.
static int parse_chroma_format(const char *str) {
  if (!str) return 0;
  if (strcmp(str, "yuv420") == 0 || strcmp(str, "420") == 0) return 420;
  if (strcmp(str, "yuv422") == 0 || strcmp(str, "422") == 0) return 422;
  if (strcmp(str, "yuv444") == 0 || strcmp(str, "444") == 0) return 444;
  fprintf(stderr, "Warning: unknown format \"%s\"\n", str);
  return 0;
}

// Helper: warn about unknown keys in a JSON object
static void warn_unknown_keys(const cJSON *obj, const char *const known[],
                              int num_known, const char *section) {
  const cJSON *item = NULL;
  cJSON_ArrayForEach(item, obj) {
    if (strcmp(item->string, "comment") == 0) continue;
    int found = 0;
    for (int i = 0; i < num_known; i++) {
      if (strcmp(item->string, known[i]) == 0) {
        found = 1;
        break;
      }
    }
    if (!found)
      fprintf(stderr, "Warning: unknown key \"%s\" in %s (ignored)\n",
              item->string, section);
  }
}

// Helper: get integer from JSON, or default
static int json_get_int(const cJSON *obj, const char *key, int default_val) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsNumber(item)) return item->valueint;
  return default_val;
}

// Helper: get boolean from JSON, or default
static int json_get_bool(const cJSON *obj, const char *key, int default_val) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsTrue(item)) return 1;
  if (cJSON_IsFalse(item)) return 0;
  if (cJSON_IsNumber(item)) return item->valueint != 0;
  return default_val;
}

// Helper: get string from JSON, or default
static const char *json_get_string(const cJSON *obj, const char *key,
                                   const char *default_val) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (cJSON_IsString(item)) return item->valuestring;
  return default_val;
}

// Helper: parse frame_rate from JSON into num/den rational.
// Accepts: number (e.g., 30 -> 30/1), string "N/D" (e.g., "30000/1001").
// Returns 0 on success (or no field present), -1 on error.
static int json_parse_frame_rate(const cJSON *obj, const char *key, int *num,
                                 int *den) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!item) return 0;
  if (cJSON_IsNumber(item)) {
    // Integer or float -> convert to rational
    double v = item->valuedouble;
    if (v <= 0.0) return 0;
    // Check if it's an integer
    if (v == (double)(int)v) {
      *num = (int)v;
      *den = 1;
    } else {
      // Common fractional rates: multiply by 1001 to check for NTSC
      double v1001 = v * 1001.0;
      if (fabs(v1001 - round(v1001)) < 0.01) {
        *num = (int)round(v1001);
        *den = 1001;
      } else {
        // General: use 1000x scale
        *num = (int)round(v * 1000.0);
        *den = 1000;
      }
    }
    return 0;
  }
  if (cJSON_IsString(item)) {
    int n = 0, d = 0;
    if (sscanf(item->valuestring, "%d/%d", &n, &d) == 2 && d > 0) {
      *num = n;
      *den = d;
      return 0;
    }
    // Try plain integer string
    if (sscanf(item->valuestring, "%d", &n) == 1 && n > 0) {
      *num = n;
      *den = 1;
      return 0;
    }
    fprintf(stderr, "Error: invalid frame_rate \"%s\"\n", item->valuestring);
    return -1;
  }
  return 0;
}

// Read entire file into a malloc'd string
static char *read_file_contents(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "Error: cannot open config file \"%s\"\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0) {
    fclose(f);
    fprintf(stderr, "Error: config file \"%s\" is empty\n", path);
    return NULL;
  }
  char *buf = (char *)malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t read_len = fread(buf, 1, (size_t)len, f);
  fclose(f);
  buf[read_len] = '\0';
  return buf;
}

// Parse a single xlayer entry from JSON into XLayerEncConfig
static int parse_xlayer_entry(const cJSON *entry, XLayerEncConfig *xlcfg) {
  xlcfg->xlayer_id = json_get_int(entry, "xlayer_id", -1);
  if (xlcfg->xlayer_id < 0 || xlcfg->xlayer_id > 30) {
    fprintf(stderr, "Error: xlayer_id must be 0-30, got %d\n",
            xlcfg->xlayer_id);
    return -1;
  }

  const char *input = json_get_string(entry, "input", NULL);
  if (input) {
    snprintf(xlcfg->input_filename, PATH_MAX, "%s", input);
  }

  // Input source reference (for multi-source mode)
  const char *isrc = json_get_string(entry, "input_source", NULL);
  if (isrc) {
    snprintf(xlcfg->input_source_name, MAX_SOURCE_NAME_LEN, "%s", isrc);
  }

  xlcfg->width = (unsigned int)json_get_int(entry, "width", 0);
  xlcfg->height = (unsigned int)json_get_int(entry, "height", 0);
  xlcfg->profile =
      (unsigned int)json_get_int(entry, "profile", MAIN_420_10_IP1);
  xlcfg->tier = (unsigned int)json_get_int(entry, "tier", 0);
  xlcfg->level = (unsigned int)json_get_int(entry, "level", SEQ_LEVEL_4_0);

  const char *lt = json_get_string(entry, "layer_type", "texture");
  xlcfg->layer_type = parse_layer_type(lt);

  if (xlcfg->layer_type == AUX_LAYER) {
    const char *at = json_get_string(entry, "auxiliary_type", "alpha");
    xlcfg->auxiliary_type = parse_auxiliary_type(at);
  }

  const char *vt = json_get_string(entry, "view_type", NULL);
  if (vt) xlcfg->view_type = parse_view_type(vt);

  xlcfg->num_temporal_layers = json_get_int(entry, "num_temporal_layers", 1);
  xlcfg->num_embedded_layers = json_get_int(entry, "num_embedded_layers", 1);

  // Color info
  xlcfg->color_primaries = json_get_int(entry, "color_primaries", -1);
  xlcfg->transfer_characteristics =
      json_get_int(entry, "transfer_characteristics", -1);
  xlcfg->matrix_coefficients = json_get_int(entry, "matrix_coefficients", -1);
  xlcfg->full_range_flag = json_get_int(entry, "full_range_flag", -1);

  // Encoder overrides
  xlcfg->qp = json_get_int(entry, "qp", -1);
  xlcfg->bitrate = json_get_int(entry, "bitrate", -1);
  xlcfg->cpu_used = json_get_int(entry, "cpu_used", -1);
  xlcfg->lag_in_frames = json_get_int(entry, "lag_in_frames", -1);

  // S-Frame parameters
  xlcfg->sframe_dist = json_get_int(entry, "sframe_dist", -1);
  xlcfg->sframe_mode = json_get_int(entry, "sframe_mode", -1);
  xlcfg->sframe_type = json_get_int(entry, "sframe_type", -1);

  // Coding structure overrides
  xlcfg->kf_max_dist = json_get_int(entry, "kf_max_dist", -1);
  const char *subgop = json_get_string(entry, "subgop_config", NULL);
  if (subgop) {
    snprintf(xlcfg->subgop_config_path, PATH_MAX, "%s", subgop);
  }

  // GOP mode and overrides
  const char *gop = json_get_string(entry, "gop_mode", NULL);
  if (gop) xlcfg->gop_mode = parse_gop_mode(gop);
  xlcfg->fwd_kf_enabled = json_get_int(entry, "fwd_kf_enabled", -1);
  xlcfg->enable_keyframe_filtering =
      json_get_int(entry, "enable_keyframe_filtering", -1);
  xlcfg->add_sef_for_hidden_frames =
      json_get_int(entry, "add_sef_for_hidden_frames", -1);

  // Atlas layout position
  xlcfg->atlas_pos_x = json_get_int(entry, "atlas_pos_x", -1);
  xlcfg->atlas_pos_y = json_get_int(entry, "atlas_pos_y", -1);

  // Scaling modes for embedded layers (flat array format)
  const cJSON *scaling =
      cJSON_GetObjectItemCaseSensitive(entry, "scaling_mode");
  const cJSON *el_arr =
      cJSON_GetObjectItemCaseSensitive(entry, "embedded_layers");

  if (cJSON_IsArray(scaling) && cJSON_IsArray(el_arr)) {
    fprintf(stderr,
            "Error: xlayer %d has both \"scaling_mode\" array and "
            "\"embedded_layers\" — these are mutually exclusive\n",
            xlcfg->xlayer_id);
    return -1;
  }

  int scaling_modes_explicit = 0;

  if (cJSON_IsArray(scaling)) {
    scaling_modes_explicit = 1;
    int n = cJSON_GetArraySize(scaling);
    if (n > MAX_NUM_MLAYERS) n = MAX_NUM_MLAYERS;
    for (int i = 0; i < n; i++) {
      const cJSON *s = cJSON_GetArrayItem(scaling, i);
      if (cJSON_IsNumber(s)) {
        xlcfg->scaling_mode[i] = s->valueint;
      } else if (cJSON_IsString(s)) {
        int mode = parse_scaling_mode(s->valuestring);
        if (mode < 0) {
          fprintf(stderr, "Error: invalid scaling_mode \"%s\" for xlayer %d\n",
                  s->valuestring, xlcfg->xlayer_id);
          return -1;
        }
        xlcfg->scaling_mode[i] = mode;
      }
    }
  }

  // Per-embedded-layer configuration (new format)
  if (cJSON_IsArray(el_arr)) {
    int n = cJSON_GetArraySize(el_arr);
    if (n != xlcfg->num_embedded_layers) {
      fprintf(stderr,
              "Error: xlayer %d \"embedded_layers\" array length %d does not "
              "match num_embedded_layers %d\n",
              xlcfg->xlayer_id, n, xlcfg->num_embedded_layers);
      return -1;
    }
    for (int m = 0; m < n; m++) {
      const cJSON *el = cJSON_GetArrayItem(el_arr, m);
      MLayerSourceConfig *ms = &xlcfg->mlayer_sources[m];

      // scaling_mode per embedded layer
      const cJSON *sm_item =
          cJSON_GetObjectItemCaseSensitive(el, "scaling_mode");
      if (sm_item != NULL) scaling_modes_explicit = 1;
      if (cJSON_IsNumber(sm_item)) {
        xlcfg->scaling_mode[m] = sm_item->valueint;
      } else if (cJSON_IsString(sm_item)) {
        int mode = parse_scaling_mode(sm_item->valuestring);
        if (mode < 0) {
          fprintf(
              stderr,
              "Error: invalid scaling_mode \"%s\" for xlayer %d mlayer %d\n",
              sm_item->valuestring, xlcfg->xlayer_id, m);
          return -1;
        }
        xlcfg->scaling_mode[m] = mode;
      }

      // input_source
      const char *ml_isrc = json_get_string(el, "input_source", NULL);
      if (ml_isrc)
        snprintf(ms->input_source_name, MAX_SOURCE_NAME_LEN, "%s", ml_isrc);

      // crop coordinates
      ms->atlas_pos_x = json_get_int(el, "atlas_pos_x", -1);
      ms->atlas_pos_y = json_get_int(el, "atlas_pos_y", -1);
      ms->width = (unsigned int)json_get_int(el, "width", 0);
      ms->height = (unsigned int)json_get_int(el, "height", 0);

      // depends_on → dependency_mask
      const cJSON *deps = cJSON_GetObjectItemCaseSensitive(el, "depends_on");
      if (cJSON_IsArray(deps)) {
        ms->dependency_mask = 0;
        const cJSON *dep = NULL;
        cJSON_ArrayForEach(dep, deps) {
          if (cJSON_IsNumber(dep) && dep->valueint >= 0 && dep->valueint < m) {
            ms->dependency_mask |= (1 << dep->valueint);
          } else if (cJSON_IsNumber(dep) && dep->valueint >= m) {
            fprintf(stderr,
                    "Error: xlayer %d mlayer %d depends_on[%d] >= self\n",
                    xlcfg->xlayer_id, m, dep->valueint);
            return -1;
          }
        }
        xlcfg->has_mlayer_dependencies = 1;
      }

      // Content Interpretation overrides (inherit from xlayer if omitted)
      ms->color_primaries = json_get_int(el, "color_primaries", -1);
      ms->transfer_characteristics =
          json_get_int(el, "transfer_characteristics", -1);
      ms->matrix_coefficients = json_get_int(el, "matrix_coefficients", -1);
      ms->full_range_flag = json_get_int(el, "full_range_flag", -1);

      // Warn about unknown keys in this embedded layer entry
      {
        static const char *const el_known[] = {
          "scaling_mode",        "input_source",    "atlas_pos_x",
          "atlas_pos_y",         "width",           "height",
          "depends_on",          "color_primaries", "transfer_characteristics",
          "matrix_coefficients", "full_range_flag",
        };
        char el_section[64];
        snprintf(el_section, sizeof(el_section),
                 "xlayer %d embedded_layers[%d]", xlcfg->xlayer_id, m);
        warn_unknown_keys(el, el_known, sizeof(el_known) / sizeof(el_known[0]),
                          el_section);
      }

      if (ms->input_source_name[0] || ms->atlas_pos_x >= 0 || ms->width > 0)
        xlcfg->has_per_mlayer_sources = 1;
    }
  }

  // Parse generic codec controls array: [["name", value], ...]
  const cJSON *cc_arr =
      cJSON_GetObjectItemCaseSensitive(entry, "codec_controls");
  if (cJSON_IsArray(cc_arr)) {
    int n = cJSON_GetArraySize(cc_arr);
    if (n > MAX_CODEC_CONTROLS) {
      fprintf(stderr, "Error: xlayer %d has %d codec_controls (max %d)\n",
              xlcfg->xlayer_id, n, MAX_CODEC_CONTROLS);
      return -1;
    }
    xlcfg->num_codec_controls = n;
    for (int c = 0; c < n; c++) {
      const cJSON *pair = cJSON_GetArrayItem(cc_arr, c);
      if (!cJSON_IsArray(pair) || cJSON_GetArraySize(pair) != 2) {
        fprintf(stderr,
                "Error: xlayer %d codec_controls[%d] must be [\"name\", "
                "value]\n",
                xlcfg->xlayer_id, c);
        return -1;
      }
      const cJSON *name_item = cJSON_GetArrayItem(pair, 0);
      const cJSON *val_item = cJSON_GetArrayItem(pair, 1);
      if (!cJSON_IsString(name_item) || !cJSON_IsNumber(val_item)) {
        fprintf(stderr,
                "Error: xlayer %d codec_controls[%d] must be [string, "
                "number]\n",
                xlcfg->xlayer_id, c);
        return -1;
      }
      snprintf(xlcfg->codec_controls[c].name, 64, "%s", name_item->valuestring);
      xlcfg->codec_controls[c].value = val_item->valueint;
    }
  }

  // Derive default scaling modes when num_embedded_layers > 1 and none
  // specified (all zeros)
  if (!scaling_modes_explicit && xlcfg->num_embedded_layers > 1) {
    int all_zero = 1;
    for (int i = 0; i < xlcfg->num_embedded_layers; i++) {
      if (xlcfg->scaling_mode[i] != 0) {
        all_zero = 0;
        break;
      }
    }
    if (all_zero) {
      // Default: smallest to full-res. Last layer is always AVME_NORMAL (0).
      if (xlcfg->num_embedded_layers == 2) {
        xlcfg->scaling_mode[0] = AVME_ONETWO;
      } else if (xlcfg->num_embedded_layers >= 3) {
        xlcfg->scaling_mode[0] = AVME_ONEFOUR;
        xlcfg->scaling_mode[1] = AVME_ONETWO;
      }
    }
  }

  // Warn about unknown keys
  {
    static const char *const known[] = {
      "xlayer_id",
      "input",
      "input_source",
      "width",
      "height",
      "profile",
      "tier",
      "level",
      "layer_type",
      "auxiliary_type",
      "view_type",
      "num_temporal_layers",
      "num_embedded_layers",
      "color_primaries",
      "transfer_characteristics",
      "matrix_coefficients",
      "full_range_flag",
      "qp",
      "bitrate",
      "cpu_used",
      "lag_in_frames",
      "sframe_dist",
      "sframe_mode",
      "sframe_type",
      "kf_max_dist",
      "subgop_config",
      "gop_mode",
      "fwd_kf_enabled",
      "enable_keyframe_filtering",
      "add_sef_for_hidden_frames",
      "atlas_pos_x",
      "atlas_pos_y",
      "scaling_mode",
      "embedded_layers",
      "codec_controls",
    };
    char section[64];
    snprintf(section, sizeof(section), "xlayer %d", xlcfg->xlayer_id);
    warn_unknown_keys(entry, known, sizeof(known) / sizeof(known[0]), section);
  }

  return 0;
}

// Parse operating point xlayer_map from JSON array of xlayer IDs to bitmask
static uint32_t parse_xlayer_map_array(const cJSON *arr) {
  uint32_t map = 0;
  if (!cJSON_IsArray(arr)) return 0;
  const cJSON *elem = NULL;
  cJSON_ArrayForEach(elem, arr) {
    if (cJSON_IsNumber(elem) && elem->valueint >= 0 &&
        elem->valueint < (int)MAX_NUM_XLAYERS) {
      map |= (1u << (unsigned int)elem->valueint);
    }
  }
  return map;
}

// Parse a single OPS set from JSON
static int parse_ops_entry(const cJSON *entry, OPSConfig *ops_cfg) {
  ops_cfg->enable = 1;
  ops_cfg->ops_id = json_get_int(entry, "ops_id", 0);
  ops_cfg->priority = json_get_int(entry, "priority", 0);
  ops_cfg->intent_present_flag = json_get_bool(entry, "intent_present", 1);
  ops_cfg->ptl_present_flag = json_get_bool(entry, "ptl_present", 1);
  ops_cfg->color_info_present_flag =
      json_get_bool(entry, "color_info_present", 0);
  ops_cfg->mlayer_info_idc = json_get_int(entry, "mlayer_info_idc", 0);

  const cJSON *op_arr =
      cJSON_GetObjectItemCaseSensitive(entry, "operating_points");
  if (!cJSON_IsArray(op_arr)) {
    fprintf(stderr, "Error: OPS %d missing \"operating_points\" array\n",
            ops_cfg->ops_id);
    return -1;
  }

  ops_cfg->num_operating_points = cJSON_GetArraySize(op_arr);
  if (ops_cfg->num_operating_points > MAX_OPS_COUNT) {
    fprintf(stderr, "Error: OPS %d has %d operating points (max %d)\n",
            ops_cfg->ops_id, ops_cfg->num_operating_points, MAX_OPS_COUNT);
    return -1;
  }

  for (int i = 0; i < ops_cfg->num_operating_points; i++) {
    const cJSON *op_entry = cJSON_GetArrayItem(op_arr, i);
    OperatingPointConfig *op = &ops_cfg->ops[i];

    op->intent = json_get_int(op_entry, "intent", 0);
    const cJSON *xmap =
        cJSON_GetObjectItemCaseSensitive(op_entry, "xlayer_map");
    op->xlayer_map = parse_xlayer_map_array(xmap);

    // Per-xlayer overrides within this OP
    const cJSON *ml =
        cJSON_GetObjectItemCaseSensitive(op_entry, "mlayer_count");
    if (cJSON_IsArray(ml)) {
      int n = cJSON_GetArraySize(ml);
      for (int j = 0; j < n && j < MAX_NUM_XLAYERS; j++) {
        const cJSON *v = cJSON_GetArrayItem(ml, j);
        if (cJSON_IsNumber(v)) op->mlayer_count[j] = v->valueint;
      }
    }

    op->aggregate_level_idx = json_get_int(op_entry, "aggregate_level_idx", -1);
    op->max_tier_flag = json_get_int(op_entry, "max_tier_flag", -1);

    // Warn about unknown keys in this operating point
    {
      static const char *const op_known[] = {
        "intent",        "xlayer_map", "mlayer_count", "aggregate_level_idx",
        "max_tier_flag",
      };
      char op_section[64];
      snprintf(op_section, sizeof(op_section), "ops %d operating_points[%d]",
               ops_cfg->ops_id, i);
      warn_unknown_keys(op_entry, op_known,
                        sizeof(op_known) / sizeof(op_known[0]), op_section);
    }
  }

  // Warn about unknown keys in this OPS entry
  {
    static const char *const known[] = {
      "ops_id",           "priority",           "intent_present",
      "ptl_present",      "color_info_present", "mlayer_info_idc",
      "operating_points",
    };
    char section[64];
    snprintf(section, sizeof(section), "ops %d", ops_cfg->ops_id);
    warn_unknown_keys(entry, known, sizeof(known) / sizeof(known[0]), section);
  }

  return 0;
}

int parse_multi_xlayer_config(const char *json_path, MultiXLayerConfig *cfg) {
  xlayer_config_init(cfg);

  char *json_str = read_file_contents(json_path);
  if (!json_str) return -1;

  cJSON *root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) {
    fprintf(stderr, "Error: failed to parse JSON in \"%s\"\n", json_path);
    return -1;
  }

  // Parse xlayers array
  const cJSON *xlayers = cJSON_GetObjectItemCaseSensitive(root, "xlayers");
  if (!cJSON_IsArray(xlayers)) {
    fprintf(stderr, "Error: config missing \"xlayers\" array\n");
    cJSON_Delete(root);
    return -1;
  }

  cfg->num_xlayers = cJSON_GetArraySize(xlayers);
  if (cfg->num_xlayers < 1 || cfg->num_xlayers > MAX_NUM_XLAYERS - 1) {
    fprintf(stderr, "Error: num_xlayers %d out of range (1-%d)\n",
            cfg->num_xlayers, MAX_NUM_XLAYERS - 1);
    cJSON_Delete(root);
    return -1;
  }

  for (int i = 0; i < cfg->num_xlayers; i++) {
    const cJSON *entry = cJSON_GetArrayItem(xlayers, i);
    if (parse_xlayer_entry(entry, &cfg->xlayers[i]) != 0) {
      cJSON_Delete(root);
      return -1;
    }
  }

  // Parse global_lcr
  const cJSON *lcr = cJSON_GetObjectItemCaseSensitive(root, "global_lcr");
  if (cJSON_IsObject(lcr)) {
    cfg->enable_global_lcr = json_get_bool(lcr, "enable", 1);
    cfg->lcr_purpose_id = json_get_int(lcr, "purpose_id", 0);
    cfg->lcr_dependent_xlayers_flag =
        json_get_bool(lcr, "dependent_xlayers", 0);
    cfg->lcr_doh_constraint_flag = json_get_bool(lcr, "doh_constraint", 1);
    static const char *const lcr_known[] = {
      "enable",
      "purpose_id",
      "dependent_xlayers",
      "doh_constraint",
    };
    warn_unknown_keys(lcr, lcr_known, sizeof(lcr_known) / sizeof(lcr_known[0]),
                      "global_lcr");
  }

  // Parse local_lcr
  const cJSON *local_lcr = cJSON_GetObjectItemCaseSensitive(root, "local_lcr");
  if (cJSON_IsObject(local_lcr)) {
    cfg->enable_local_lcr = json_get_bool(local_lcr, "enable", 0);
    const char *mode_str = json_get_string(local_lcr, "mode", "both");
    if (strcmp(mode_str, "local_only") == 0)
      cfg->local_lcr_mode = 1;
    else
      cfg->local_lcr_mode = 0;
    static const char *const ll_known[] = { "enable", "mode" };
    warn_unknown_keys(local_lcr, ll_known,
                      sizeof(ll_known) / sizeof(ll_known[0]), "local_lcr");
  }

  // Parse msdo
  const cJSON *msdo = cJSON_GetObjectItemCaseSensitive(root, "msdo");
  if (cJSON_IsObject(msdo)) {
    cfg->enable_msdo = json_get_bool(msdo, "enable", 0);
    static const char *const msdo_known[] = { "enable" };
    warn_unknown_keys(msdo, msdo_known,
                      sizeof(msdo_known) / sizeof(msdo_known[0]), "msdo");
  }

  // Parse ops array
  const cJSON *ops_arr = cJSON_GetObjectItemCaseSensitive(root, "ops");
  if (cJSON_IsArray(ops_arr)) {
    cfg->num_ops_sets = cJSON_GetArraySize(ops_arr);
    if (cfg->num_ops_sets > MAX_NUM_OPS_ID) cfg->num_ops_sets = MAX_NUM_OPS_ID;
    for (int i = 0; i < cfg->num_ops_sets; i++) {
      const cJSON *entry = cJSON_GetArrayItem(ops_arr, i);
      if (parse_ops_entry(entry, &cfg->ops_sets[i]) != 0) {
        cJSON_Delete(root);
        return -1;
      }
    }
  }

  // Parse atlas
  const cJSON *atlas = cJSON_GetObjectItemCaseSensitive(root, "atlas");
  if (cJSON_IsObject(atlas)) {
    cfg->enable_atlas = json_get_bool(atlas, "enable", 0);
    cfg->atlas_mode = json_get_int(atlas, "mode", 0);
    cfg->atlas_width = json_get_int(atlas, "width", 0);
    cfg->atlas_height = json_get_int(atlas, "height", 0);
    cfg->atlas_uniform_spacing = json_get_bool(atlas, "uniform_spacing", 1);
    static const char *const atlas_known[] = {
      "enable", "mode", "width", "height", "uniform_spacing",
    };
    warn_unknown_keys(atlas, atlas_known,
                      sizeof(atlas_known) / sizeof(atlas_known[0]), "atlas");
  }

  // Parse named input sources array (new format)
  const cJSON *inputs = cJSON_GetObjectItemCaseSensitive(root, "inputs");
  if (cJSON_IsArray(inputs)) {
    int n = cJSON_GetArraySize(inputs);
    if (n > MAX_INPUT_SOURCES) {
      fprintf(stderr, "Error: too many input sources (%d > %d)\n", n,
              MAX_INPUT_SOURCES);
      cJSON_Delete(root);
      return -1;
    }
    cfg->num_input_sources = n;
    for (int i = 0; i < n; i++) {
      const cJSON *inp = cJSON_GetArrayItem(inputs, i);
      InputSourceConfig *src = &cfg->input_sources[i];
      const char *name = json_get_string(inp, "name", NULL);
      if (name) snprintf(src->name, MAX_SOURCE_NAME_LEN, "%s", name);
      const char *fn = json_get_string(inp, "filename", NULL);
      if (fn) snprintf(src->filename, PATH_MAX, "%s", fn);
      src->width = (unsigned int)json_get_int(inp, "width", 0);
      src->height = (unsigned int)json_get_int(inp, "height", 0);
      const char *fmt = json_get_string(inp, "format", NULL);
      src->format = parse_chroma_format(fmt);
      src->bit_depth = json_get_int(inp, "bit_depth", 0);
      if (json_parse_frame_rate(inp, "frame_rate", &src->frame_rate_num,
                                &src->frame_rate_den) != 0) {
        cJSON_Delete(root);
        return -1;
      }
      static const char *const inp_known[] = {
        "name",   "filename",  "width",      "height",
        "format", "bit_depth", "frame_rate",
      };
      char inp_section[64];
      snprintf(inp_section, sizeof(inp_section), "inputs[%d]", i);
      warn_unknown_keys(inp, inp_known,
                        sizeof(inp_known) / sizeof(inp_known[0]), inp_section);
    }
  }

  // Parse shared source (legacy single-source format)
  const cJSON *source = cJSON_GetObjectItemCaseSensitive(root, "source");
  if (cJSON_IsObject(source)) {
    if (cfg->num_input_sources > 0) {
      fprintf(stderr, "Error: cannot specify both \"inputs\" and \"source\"\n");
      cJSON_Delete(root);
      return -1;
    }
    const char *src_file = json_get_string(source, "filename", NULL);
    if (src_file) {
      snprintf(cfg->source_filename, PATH_MAX, "%s", src_file);
    }
    cfg->source_width = (unsigned int)json_get_int(source, "width", 0);
    cfg->source_height = (unsigned int)json_get_int(source, "height", 0);
    // Convert to input_sources[0] for unified handling
    cfg->num_input_sources = 1;
    InputSourceConfig *src = &cfg->input_sources[0];
    snprintf(src->name, MAX_SOURCE_NAME_LEN, "default");
    if (src_file) snprintf(src->filename, PATH_MAX, "%s", src_file);
    src->width = cfg->source_width;
    src->height = cfg->source_height;
    static const char *const src_known[] = { "filename", "width", "height" };
    warn_unknown_keys(source, src_known,
                      sizeof(src_known) / sizeof(src_known[0]), "source");
  }

  // Parse bitstream options
  cfg->combined_tu = json_get_bool(root, "combined_tu", 1);
  cfg->monotonic_output_order =
      json_get_bool(root, "monotonic_output_order", 0);

  // Parse frame rate (used for aggregate level derivation)
  {
    const cJSON *fps = cJSON_GetObjectItemCaseSensitive(root, "frame_rate");
    if (cJSON_IsNumber(fps)) cfg->frame_rate = fps->valuedouble;
  }

  // Parse limit (max frames to encode)
  cfg->limit = json_get_int(root, "limit", 0);

  const char *output = json_get_string(root, "output", NULL);
  if (output) {
    snprintf(cfg->output_filename, PATH_MAX, "%s", output);
  }

  // Warn about unknown root-level keys
  {
    static const char *const known[] = {
      "xlayers",     "global_lcr",
      "local_lcr",   "msdo",
      "ops",         "atlas",
      "inputs",      "source",
      "combined_tu", "monotonic_output_order",
      "frame_rate",  "limit",
      "output",
    };
    warn_unknown_keys(root, known, sizeof(known) / sizeof(known[0]), "root");
  }

  cJSON_Delete(root);
  return 0;
}

// Look up an input source by name. Returns its index, or -1 if not found.
static int find_input_source_by_name(const MultiXLayerConfig *cfg,
                                     const char *name) {
  for (int s = 0; s < cfg->num_input_sources; s++) {
    if (strcmp(name, cfg->input_sources[s].name) == 0) return s;
  }
  return -1;
}

int resolve_input_sources(MultiXLayerConfig *cfg) {
  for (int i = 0; i < cfg->num_xlayers; i++) {
    XLayerEncConfig *xl = &cfg->xlayers[i];
    xl->input_source_idx = -1;  // default: own file

    // Skip xlayers with their own input file
    if (xl->input_filename[0] != '\0') continue;

    if (xl->input_source_name[0] != '\0') {
      // Explicit source reference — look up by name
      xl->input_source_idx =
          find_input_source_by_name(cfg, xl->input_source_name);
      if (xl->input_source_idx < 0) {
        fprintf(stderr,
                "Error: xlayer %d references unknown input_source \"%s\"\n",
                xl->xlayer_id, xl->input_source_name);
        return -1;
      }
    } else if (cfg->num_input_sources == 1) {
      // Single input source — all unassigned xlayers use it
      xl->input_source_idx = 0;
    } else if (cfg->num_input_sources > 1) {
      fprintf(stderr,
              "Error: xlayer %d has no input or input_source, and multiple "
              "input sources are defined\n",
              xl->xlayer_id);
      return -1;
    }
  }

  // Resolve per-mlayer input sources
  for (int i = 0; i < cfg->num_xlayers; i++) {
    XLayerEncConfig *xl = &cfg->xlayers[i];
    if (!xl->has_per_mlayer_sources) continue;
    for (int m = 0; m < xl->num_embedded_layers; m++) {
      MLayerSourceConfig *ms = &xl->mlayer_sources[m];
      if (ms->input_source_name[0] == '\0') {
        // Inherit from xlayer
        ms->input_source_idx = xl->input_source_idx;
        if (ms->atlas_pos_x < 0) ms->atlas_pos_x = xl->atlas_pos_x;
        if (ms->atlas_pos_y < 0) ms->atlas_pos_y = xl->atlas_pos_y;
        if (ms->width == 0) ms->width = xl->width;
        if (ms->height == 0) ms->height = xl->height;
        continue;
      }
      // Look up source by name
      ms->input_source_idx =
          find_input_source_by_name(cfg, ms->input_source_name);
      if (ms->input_source_idx < 0) {
        fprintf(stderr,
                "Error: xlayer %d mlayer %d references unknown input_source "
                "\"%s\"\n",
                xl->xlayer_id, m, ms->input_source_name);
        return -1;
      }
      // Fill in crop defaults if not specified
      if (ms->atlas_pos_x < 0) ms->atlas_pos_x = 0;
      if (ms->atlas_pos_y < 0) ms->atlas_pos_y = 0;
    }
  }

  // Resolve frame_skip for each input source based on frame rates.
  // The master rate is the highest frame rate among all sources.
  // Each source's rate must be an exact integer divisor of the master rate.
  // Frame rates are rational (num/den) for exact arithmetic.
  {
    // Find the maximum frame rate using rational comparison: a/b > c/d iff
    // a*d > c*b
    int max_idx = -1;
    for (int s = 0; s < cfg->num_input_sources; s++) {
      if (cfg->input_sources[s].frame_rate_num <= 0) continue;
      if (max_idx < 0) {
        max_idx = s;
      } else {
        int64_t lhs = (int64_t)cfg->input_sources[s].frame_rate_num *
                      cfg->input_sources[max_idx].frame_rate_den;
        int64_t rhs = (int64_t)cfg->input_sources[max_idx].frame_rate_num *
                      cfg->input_sources[s].frame_rate_den;
        if (lhs > rhs) max_idx = s;
      }
    }

    if (max_idx >= 0) {
      int max_n = cfg->input_sources[max_idx].frame_rate_num;
      int max_d = cfg->input_sources[max_idx].frame_rate_den;

      for (int s = 0; s < cfg->num_input_sources; s++) {
        int src_n = cfg->input_sources[s].frame_rate_num;
        int src_d = cfg->input_sources[s].frame_rate_den;
        if (src_n <= 0) {
          // Unspecified — assume master rate
          cfg->input_sources[s].frame_skip = 1;
          continue;
        }
        // Ratio = (max_n/max_d) / (src_n/src_d) = (max_n * src_d) /
        //         (max_d * src_n)
        int64_t ratio_num = (int64_t)max_n * src_d;
        int64_t ratio_den = (int64_t)max_d * src_n;
        // Must be an exact integer (ratio_den divides ratio_num evenly)
        if (ratio_den == 0 || ratio_num % ratio_den != 0) {
          fprintf(stderr,
                  "Error: input source \"%s\" frame_rate %d/%d is not an "
                  "exact divisor of the max frame_rate %d/%d\n",
                  cfg->input_sources[s].name, src_n, src_d, max_n, max_d);
          return -1;
        }
        int skip = (int)(ratio_num / ratio_den);
        if (skip < 1) {
          fprintf(stderr,
                  "Error: input source \"%s\" frame_rate %d/%d exceeds the "
                  "max frame_rate %d/%d\n",
                  cfg->input_sources[s].name, src_n, src_d, max_n, max_d);
          return -1;
        }
        cfg->input_sources[s].frame_skip = skip;
      }
    } else {
      // No frame rates specified — all sources run at same rate
      for (int s = 0; s < cfg->num_input_sources; s++)
        cfg->input_sources[s].frame_skip = 1;
    }
  }

  return 0;
}

// Resolve per-mlayer CI inheritance: if an mlayer's CI field is -1, inherit
// from the parent xlayer's value. Must be called after resolve_input_sources().
void resolve_mlayer_ci(MultiXLayerConfig *cfg) {
  for (int i = 0; i < cfg->num_xlayers; i++) {
    XLayerEncConfig *xl = &cfg->xlayers[i];
    for (int m = 0; m < xl->num_embedded_layers; m++) {
      MLayerSourceConfig *ms = &xl->mlayer_sources[m];
      if (ms->color_primaries == -1) ms->color_primaries = xl->color_primaries;
      if (ms->transfer_characteristics == -1)
        ms->transfer_characteristics = xl->transfer_characteristics;
      if (ms->matrix_coefficients == -1)
        ms->matrix_coefficients = xl->matrix_coefficients;
      if (ms->full_range_flag == -1) ms->full_range_flag = xl->full_range_flag;
    }
  }
}

int validate_multi_xlayer_config(const MultiXLayerConfig *cfg) {
  if (cfg->num_xlayers < 1) {
    fprintf(stderr, "Error: must have at least 1 xlayer\n");
    return -1;
  }

  // Check xlayer_ids are unique and in range
  int seen[MAX_NUM_XLAYERS] = { 0 };
  for (int i = 0; i < cfg->num_xlayers; i++) {
    int id = cfg->xlayers[i].xlayer_id;
    if (id < 0 || id > 30) {
      fprintf(stderr, "Error: xlayer %d has invalid xlayer_id %d\n", i, id);
      return -1;
    }
    if (seen[id]) {
      fprintf(stderr, "Error: duplicate xlayer_id %d\n", id);
      return -1;
    }
    seen[id] = 1;
  }

  // Validate input source names are unique and non-empty
  for (int i = 0; i < cfg->num_input_sources; i++) {
    if (cfg->input_sources[i].name[0] == '\0') {
      fprintf(stderr, "Error: input source %d has no name\n", i);
      return -1;
    }
    for (int j = i + 1; j < cfg->num_input_sources; j++) {
      if (strcmp(cfg->input_sources[i].name, cfg->input_sources[j].name) == 0) {
        fprintf(stderr, "Error: duplicate input source name \"%s\"\n",
                cfg->input_sources[i].name);
        return -1;
      }
    }
  }

  // Validate each xlayer has input (or input source) and dimensions
  int has_shared_source =
      (cfg->source_filename[0] != '\0' || cfg->num_input_sources > 0);
  for (int i = 0; i < cfg->num_xlayers; i++) {
    const XLayerEncConfig *xl = &cfg->xlayers[i];
    if (xl->input_filename[0] == '\0' && xl->input_source_idx < 0 &&
        !has_shared_source) {
      fprintf(stderr,
              "Error: xlayer %d missing input filename and no shared source\n",
              xl->xlayer_id);
      return -1;
    }
    // When using a shared/named input source, atlas positions and dimensions
    // are required
    if (xl->input_source_idx >= 0) {
      if (xl->atlas_pos_x < 0 || xl->atlas_pos_y < 0) {
        fprintf(stderr,
                "Error: xlayer %d requires atlas_pos_x/y when using input "
                "source\n",
                xl->xlayer_id);
        return -1;
      }
      if (xl->width == 0 || xl->height == 0) {
        fprintf(stderr,
                "Error: xlayer %d requires width/height when using input "
                "source\n",
                xl->xlayer_id);
        return -1;
      }
    }
  }

  // Per-source-group chroma validation: xlayers sharing the same input source
  // must have the same chroma format (profile determines chroma)
  for (int s = 0; s < cfg->num_input_sources; s++) {
    int ref_chroma = -1;
    int ref_xlayer_id = -1;
    unsigned int ref_profile = 0;
    for (int i = 0; i < cfg->num_xlayers; i++) {
      if (cfg->xlayers[i].input_source_idx != s) continue;
      int chroma = (cfg->xlayers[i].profile <= MAIN_420_10_IP1)   ? 0
                   : (cfg->xlayers[i].profile == MAIN_422_10_IP1) ? 1
                                                                  : 2;
      if (ref_chroma < 0) {
        ref_chroma = chroma;
        ref_xlayer_id = cfg->xlayers[i].xlayer_id;
        ref_profile = cfg->xlayers[i].profile;
      } else if (chroma != ref_chroma) {
        fprintf(stderr,
                "Error: xlayers sharing input source \"%s\" must use the "
                "same chroma format (xlayer %d profile %u vs xlayer %d "
                "profile %u)\n",
                cfg->input_sources[s].name, ref_xlayer_id, ref_profile,
                cfg->xlayers[i].xlayer_id, cfg->xlayers[i].profile);
        return -1;
      }
    }
  }

  // Validate OPS operating points reference valid xlayer_ids
  for (int s = 0; s < cfg->num_ops_sets; s++) {
    const OPSConfig *ops = &cfg->ops_sets[s];
    if (!ops->enable) continue;
    for (int p = 0; p < ops->num_operating_points; p++) {
      uint32_t xmap = ops->ops[p].xlayer_map;
      for (int bit = 0; bit < 31; bit++) {
        if (xmap & (1u << bit)) {
          if (!seen[bit]) {
            fprintf(stderr,
                    "Error: OPS %d OP %d references xlayer_id %d which is "
                    "not in the config\n",
                    ops->ops_id, p, bit);
            return -1;
          }
        }
      }
    }
  }

  // When monotonic_output_order is disabled, all xlayers must use the same
  // coding structure (temporal layers, lag-in-frames, keyframe interval,
  // sub-GOP config, and GOP mode) so that their output ordering is
  // synchronized.
  if (!cfg->monotonic_output_order && cfg->num_xlayers > 1) {
    const XLayerEncConfig *ref = &cfg->xlayers[0];
    for (int i = 1; i < cfg->num_xlayers; i++) {
      const XLayerEncConfig *xl = &cfg->xlayers[i];
      if (xl->num_temporal_layers != ref->num_temporal_layers) {
        fprintf(stderr,
                "Error: monotonic_output_order=0 requires all xlayers to use "
                "the same num_temporal_layers (xlayer %d has %d, xlayer %d has "
                "%d)\n",
                ref->xlayer_id, ref->num_temporal_layers, xl->xlayer_id,
                xl->num_temporal_layers);
        return -1;
      }
      if (xl->lag_in_frames != ref->lag_in_frames) {
        fprintf(stderr,
                "Error: monotonic_output_order=0 requires all xlayers to use "
                "the same lag_in_frames (xlayer %d has %d, xlayer %d has %d)\n",
                ref->xlayer_id, ref->lag_in_frames, xl->xlayer_id,
                xl->lag_in_frames);
        return -1;
      }
      if (xl->kf_max_dist != ref->kf_max_dist) {
        fprintf(stderr,
                "Error: monotonic_output_order=0 requires all xlayers to use "
                "the same kf_max_dist (xlayer %d has %d, xlayer %d has %d)\n",
                ref->xlayer_id, ref->kf_max_dist, xl->xlayer_id,
                xl->kf_max_dist);
        return -1;
      }
      if (strcmp(xl->subgop_config_path, ref->subgop_config_path) != 0) {
        fprintf(stderr,
                "Error: monotonic_output_order=0 requires all xlayers to use "
                "the same subgop_config (xlayer %d has \"%s\", xlayer %d has "
                "\"%s\")\n",
                ref->xlayer_id, ref->subgop_config_path, xl->xlayer_id,
                xl->subgop_config_path);
        return -1;
      }
      if (xl->gop_mode != ref->gop_mode) {
        fprintf(stderr,
                "Error: monotonic_output_order=0 requires all xlayers to use "
                "the same gop_mode (xlayer %d has %d, xlayer %d has %d)\n",
                ref->xlayer_id, ref->gop_mode, xl->xlayer_id, xl->gop_mode);
        return -1;
      }
    }
  }

  // Validate embedded layer configuration
  for (int i = 0; i < cfg->num_xlayers; i++) {
    const XLayerEncConfig *xl = &cfg->xlayers[i];
    if (xl->num_embedded_layers < 1 ||
        xl->num_embedded_layers > MAX_NUM_MLAYERS) {
      fprintf(stderr,
              "Error: xlayer %d num_embedded_layers %d out of range (1-%d)\n",
              xl->xlayer_id, xl->num_embedded_layers, MAX_NUM_MLAYERS);
      return -1;
    }
    if (xl->num_embedded_layers > 1) {
      // Last layer must be full-resolution (AVME_NORMAL = 0)
      if (xl->scaling_mode[xl->num_embedded_layers - 1] != AVME_NORMAL) {
        fprintf(stderr,
                "Error: xlayer %d scaling_mode[%d] must be 0 (full-res) for "
                "the last embedded layer\n",
                xl->xlayer_id, xl->num_embedded_layers - 1);
        return -1;
      }
      // Validate all scaling mode values are in range
      for (int m = 0; m < xl->num_embedded_layers; m++) {
        if (xl->scaling_mode[m] < AVME_NORMAL ||
            xl->scaling_mode[m] > AVME_ONETWO) {
          fprintf(stderr,
                  "Error: xlayer %d scaling_mode[%d]=%d out of range (0-%d)\n",
                  xl->xlayer_id, m, xl->scaling_mode[m], AVME_ONETWO);
          return -1;
        }
      }
    }
  }

  // Validate per-embedded-layer source configuration
  for (int i = 0; i < cfg->num_xlayers; i++) {
    const XLayerEncConfig *xl = &cfg->xlayers[i];
    if (!xl->has_per_mlayer_sources && !xl->has_mlayer_dependencies) continue;
    for (int m = 0; m < xl->num_embedded_layers; m++) {
      const MLayerSourceConfig *ms = &xl->mlayer_sources[m];
      // Per-mlayer source requires width, height, and crop coordinates
      if (ms->input_source_name[0] != '\0' || ms->input_source_idx >= 0) {
        if (ms->width == 0 || ms->height == 0) {
          fprintf(stderr,
                  "Error: xlayer %d mlayer %d requires width/height when "
                  "using per-mlayer input source\n",
                  xl->xlayer_id, m);
          return -1;
        }
        if (ms->atlas_pos_x < 0 || ms->atlas_pos_y < 0) {
          fprintf(stderr,
                  "Error: xlayer %d mlayer %d requires atlas_pos_x/y when "
                  "using per-mlayer input source\n",
                  xl->xlayer_id, m);
          return -1;
        }
      }
      // dependency_mask validation
      if (ms->dependency_mask >= 0) {
        if (m == 0 && ms->dependency_mask != 0) {
          fprintf(stderr,
                  "Error: xlayer %d mlayer 0 cannot depend on any lower "
                  "mlayer (depends_on must be empty)\n",
                  xl->xlayer_id);
          return -1;
        }
        // Check all set bits reference valid lower mlayer indices
        for (int j = m; j < MAX_NUM_MLAYERS; j++) {
          if (ms->dependency_mask & (1 << j)) {
            fprintf(stderr,
                    "Error: xlayer %d mlayer %d depends_on references "
                    "mlayer %d (must be < %d)\n",
                    xl->xlayer_id, m, j, m);
            return -1;
          }
        }
      }
    }
  }

  // Validate per-mlayer CI values are in valid CICP ranges
  for (int i = 0; i < cfg->num_xlayers; i++) {
    const XLayerEncConfig *xl = &cfg->xlayers[i];
    for (int m = 0; m < xl->num_embedded_layers; m++) {
      const MLayerSourceConfig *ms = &xl->mlayer_sources[m];
      if (ms->color_primaries != -1 &&
          (ms->color_primaries < 0 || ms->color_primaries > 255)) {
        fprintf(stderr,
                "Error: xlayer %d mlayer %d color_primaries %d out of range "
                "(0-255)\n",
                xl->xlayer_id, m, ms->color_primaries);
        return -1;
      }
      if (ms->transfer_characteristics != -1 &&
          (ms->transfer_characteristics < 0 ||
           ms->transfer_characteristics > 255)) {
        fprintf(stderr,
                "Error: xlayer %d mlayer %d transfer_characteristics %d out "
                "of range (0-255)\n",
                xl->xlayer_id, m, ms->transfer_characteristics);
        return -1;
      }
      if (ms->matrix_coefficients != -1 &&
          (ms->matrix_coefficients < 0 || ms->matrix_coefficients > 255)) {
        fprintf(stderr,
                "Error: xlayer %d mlayer %d matrix_coefficients %d out of "
                "range (0-255)\n",
                xl->xlayer_id, m, ms->matrix_coefficients);
        return -1;
      }
      if (ms->full_range_flag != -1 &&
          (ms->full_range_flag < 0 || ms->full_range_flag > 1)) {
        fprintf(stderr,
                "Error: xlayer %d mlayer %d full_range_flag %d must be 0 or "
                "1\n",
                xl->xlayer_id, m, ms->full_range_flag);
        return -1;
      }
    }
  }

  // Validate GOP mode constraints
  if (cfg->monotonic_output_order) {
    for (int i = 0; i < cfg->num_xlayers; i++) {
      if (cfg->xlayers[i].gop_mode == 1) {
        fprintf(stderr,
                "Error: gop_mode \"open_leading\" is not allowed with "
                "monotonic_output_order=1 (xlayer %d). Leading OBUs require "
                "non-monotonic output.\n",
                cfg->xlayers[i].xlayer_id);
        return -1;
      }
    }
  }

  return 0;
}
