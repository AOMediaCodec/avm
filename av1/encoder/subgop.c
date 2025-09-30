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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "av1/encoder/subgop.h"

static char *my_strtok_r(char *str, const char *delim, char **saveptr) {
  if (str == NULL) return NULL;
  if (strlen(str) == 0) return NULL;
  char *ptr = str;
  char *x = strstr(str, delim);
  if (x) {
    *x = 0;
    if (saveptr) *saveptr = x + strlen(delim);
  } else {
    if (saveptr) *saveptr = NULL;
    return ptr;
  }
  return ptr;
}

static char *read_token_after(char *str, const char *delim, char **saveptr) {
  if (str == NULL) return NULL;
  if (strlen(str) == 0) return NULL;
  char *ptr = str;
  char *x = strstr(str, delim);
  if (x) {
    ptr = x + strlen(delim);
    while (*x != 0 && !isspace(*x)) x++;
    *x = 0;
    if (saveptr) *saveptr = x + 1;
    return ptr;
  } else {
    if (saveptr) *saveptr = str;
    return NULL;
  }
}

static bool readline(char *buf, int size, FILE *fp) {
  buf[0] = '\0';
  buf[size - 1] = '\0';
  char *tmp;
  while (1) {
    if (fgets(buf, size, fp) == NULL) {
      *buf = '\0';
      return false;
    } else {
      if ((tmp = strrchr(buf, '\n')) != NULL) *tmp = '\0';
      if ((tmp = strchr(buf, '#')) != NULL) *tmp = '\0';
      for (int i = 0; i < (int)strlen(buf); ++i) {
        if (!isspace(buf[i])) return true;
      }
    }
  }
  return true;
}

void av1_init_subgop_config_set(SubGOPSetCfg *config_set) {
  memset(config_set, 0, sizeof(*config_set));
  config_set->num_configs = 0;
  for (int i = 0; i < MAX_SUBGOP_CONFIGS; ++i) {
    memset(&config_set->config[i], 0, sizeof(config_set->config[i]));
  }
}

static void check_duplicate_and_add(SubGOPSetCfg *config_set) {
  int k;
  const int n = config_set->num_configs;
  for (k = 0; k < n; ++k) {
    if (config_set->config[k].num_frames == config_set->config[n].num_frames &&
        config_set->config[k].subgop_in_gop_code ==
            config_set->config[n].subgop_in_gop_code) {
      memcpy(&config_set->config[k], &config_set->config[n],
             sizeof(config_set->config[k]));
      return;
    }
  }
  if (k == n) config_set->num_configs++;
}

static int process_subgop_step(char *str, SubGOPStepCfg *step) {
  char *ptr;
  step->num_references = 0;
  step->disp_frame_idx = (int8_t)strtol(str, &ptr, 10);
  // Check if no numeric disp idx exist or is negative
  if (ptr == str || step->disp_frame_idx < 0) return 0;
  switch (*ptr) {
    case 'V': step->type_code = FRAME_TYPE_INO_VISIBLE; break;
    case 'R': step->type_code = FRAME_TYPE_INO_REPEAT; break;
    case 'S': step->type_code = FRAME_TYPE_INO_SHOWEXISTING; return 1;
    case 'F': step->type_code = FRAME_TYPE_OOO_FILTERED; break;
    case 'U': step->type_code = FRAME_TYPE_OOO_UNFILTERED; break;
    default: return 0;
  }
  str = ++ptr;
  step->pyr_level = (int8_t)strtol(str, &ptr, 10);
  // Check if no numeric disp idx exist
  if (ptr == str) return 0;
  assert(ptr != NULL);

  // Check for character P preceding the references.
  if (*ptr == 'P') {
    str = ++ptr;
    char delim[] = "^";
    char *token, *next = NULL;
    while ((token = my_strtok_r(str, delim, &str)) != NULL) {
      if (strlen(token) == 0) return 0;
      if (step->num_references >= INTER_REFS_PER_FRAME) return 0;
      step->references[step->num_references] = (int8_t)strtol(token, &next, 10);
      if (step->references[step->num_references] == 0)
        return 0;  // 0 is invalid
      step->num_references++;
    }
    ptr = next;
  } else {
    // Unspecified references
    step->num_references = -1;
  }
  assert(ptr != NULL);

  step->refresh = -1;
  if (*ptr == 'X') {
    ptr++;
    step->refresh = (int8_t)strtol(ptr, NULL, 10);
    if (step->refresh < 0) return 0;
  } else if (*ptr == 0) {
    // no refresh data preceded by X provided
    return 1;
  } else {
    return 0;
  }
  return 1;
}

static int process_subgop_steps(char *str, SubGOPCfg *config) {
  char delim[] = "/";
  config->num_steps = 0;
  char *token;
  while ((token = my_strtok_r(str, delim, &str)) != NULL) {
    if (strlen(token) == 0) return 0;
    int res = process_subgop_step(token, &config->step[config->num_steps]);
    if (!res) return 0;
    // Populate pyr level for show existing frame
    if (config->step[config->num_steps].type_code ==
        FRAME_TYPE_INO_SHOWEXISTING) {
      int k;
      for (k = 0; k < config->num_steps; ++k) {
        if (config->step[k].disp_frame_idx ==
            config->step[config->num_steps].disp_frame_idx) {
          config->step[config->num_steps].pyr_level = config->step[k].pyr_level;
          break;
        }
      }
      // showexisting for a frame not coded before is invalid
      if (k == config->num_steps) return 0;
    }
    config->num_steps++;
  }
  return 1;
}

static int process_subgop_config(char *str, SubGOPCfg *config) {
  if (strlen(str) == 0) return 0;
  char delim[] = ":";
  char *token = my_strtok_r(str, delim, &str);
  if (!token) return 0;
  if (strlen(token) == 0) return 0;
  config->num_frames = atoi(token);
  token = my_strtok_r(str, delim, &str);
  if (!token) return 0;
  if (strlen(token) == 0) return 0;
  int subgop_in_gop_code = atoi(token);
  // check for invalid subgop_in_gop_code
  if (subgop_in_gop_code < 0 || subgop_in_gop_code >= SUBGOP_IN_GOP_CODES)
    return 0;
  config->subgop_in_gop_code = (SUBGOP_IN_GOP_CODE)subgop_in_gop_code;
  token = my_strtok_r(str, delim, &str);
  if (!token) return 0;
  if (strlen(token) == 0) return 0;
  return process_subgop_steps(token, config);
}
// frames with show_frame = 1, showable_frame = 1
// and show_existing_frame = 1 are handled as
#if CONFIG_F253_REMOVE_OUTPUTFLAG
// 'visible' frames
#else
// 'visible' frames when enable_frame_output_order = 1
#endif  // CONFIG_F253_REMOVE_OUTPUTFLAG
static int is_visible(FRAME_TYPE_CODE code) {
  switch (code) {
    case FRAME_TYPE_INO_VISIBLE:
    case FRAME_TYPE_INO_REPEAT:
    case FRAME_TYPE_OOO_UNFILTERED:
    case FRAME_TYPE_INO_SHOWEXISTING: return 1;
    case FRAME_TYPE_OOO_FILTERED: return 0;
    default: assert(0 && "Invalid frame type code"); return 0;
  }
}

static int check_subgop_config(SubGOPCfg *config) {
  // check for invalid disp_frame_idx
  for (int s = 0; s < config->num_steps; ++s) {
    if (config->step[s].disp_frame_idx > config->num_frames) return 0;
  }

  // Each disp frame index must be shown exactly once and in ascending order
  int last_visible = 0;
  for (int s = 0; s < config->num_steps; ++s) {
    if (config->step[s].type_code == FRAME_TYPE_INO_VISIBLE ||
        config->step[s].type_code == FRAME_TYPE_INO_REPEAT) {
      int updated_last_visible = config->step[s].disp_frame_idx;
      do {
        last_visible = updated_last_visible;
        for (int k = 0; k < s; ++k) {
          if (is_visible(config->step[k].type_code) &&
              config->step[k].disp_frame_idx == last_visible + 1) {
            updated_last_visible = config->step[k].disp_frame_idx;
          }
        }
      } while (last_visible != updated_last_visible);
    }
  }
  if (last_visible != config->num_frames) return 0;

  // Each disp frame index must have at most one invisible frame
  int invisible[MAX_SUBGOP_LENGTH];
  memset(invisible, 0, config->num_frames * sizeof(*invisible));
  for (int s = 0; s < config->num_steps; ++s) {
    if (!is_visible(config->step[s].type_code))
      invisible[config->step[s].disp_frame_idx - 1]++;
  }
  for (int k = 0; k < config->num_frames; ++k) {
    if (invisible[k] > 1) return 0;
  }

  return 1;
}

int av1_process_subgop_config_set(const char *param, SubGOPSetCfg *config_set) {
  if (!param) return 1;
  if (!strlen(param)) return 1;
  const int bufsize = (int)((strlen(param) + 1) * sizeof(*param));
  char *buf = (char *)aom_malloc(bufsize);
  memcpy(buf, param, bufsize);
  char delim[] = ",";

  char *str = buf;
  char *token;
  while ((token = my_strtok_r(str, delim, &str)) != NULL) {
    int res = process_subgop_config(
        token, &config_set->config[config_set->num_configs]);
    if (res) {
      res = check_subgop_config(&config_set->config[config_set->num_configs]);
      if (res) {
        check_duplicate_and_add(config_set);
      } else {
        printf(
            "Warning: Subgop config validation failed for config #%d, "
            "skipping the rest.\n",
            config_set->num_configs);
        return 0;
      }
    } else {
      printf(
          "Warning: Subgop config parsing failed for config #%d, "
          "skipping the rest.\n",
          config_set->num_configs);
      return 0;
    }
  }
  aom_free(buf);
  return 1;
}

static int process_subgop_config_fromfile(FILE *fp, SubGOPCfg *config) {
  char line[256];
  int linesize = 256;
  int s;
  if (!readline(line, linesize, fp)) return 0;
  char *token;
  char *str = line;
  token = read_token_after(str, "num_frames:", &str);
  if (!token) return 0;
  if (strlen(token) == 0) return 0;
  const int num_frames = atoi(token);
  if (num_frames <= 0) return 0;
  config->num_frames = num_frames;

  token = read_token_after(str, "subgop_in_gop_code:", &str);
  if (!token) return 0;
  if (strlen(token) == 0) return 0;
  const int subgop_in_gop_code = atoi(token);
  // check for invalid subgop_in_gop_code
  if (subgop_in_gop_code < 0 || subgop_in_gop_code >= SUBGOP_IN_GOP_CODES)
    return 0;
  config->subgop_in_gop_code = (SUBGOP_IN_GOP_CODE)subgop_in_gop_code;

  token = read_token_after(str, "num_steps:", &str);
  if (!token) return 0;
  if (strlen(token) == 0) return 0;
  config->num_steps = atoi(token);
  for (s = 0; s < config->num_steps; ++s) {
    SubGOPStepCfg *step = &config->step[s];
    step->num_references = 0;
    if (!readline(line, linesize, fp)) return 0;
    str = line;

    token = read_token_after(str, "disp_frame_idx:", &str);
    if (!token) return 0;
    if (strlen(token) == 0) return 0;
    const int disp_frame_idx = atoi(token);
    if (disp_frame_idx < 0 || disp_frame_idx > config->num_frames) return 0;
    step->disp_frame_idx = disp_frame_idx;
    token = read_token_after(str, "type_code:", &str);
    if (!token) return 0;
    if (strlen(token) != 1) return 0;
    switch (*token) {
      case 'V': step->type_code = FRAME_TYPE_INO_VISIBLE; break;
      case 'R': step->type_code = FRAME_TYPE_INO_REPEAT; break;
      case 'S': step->type_code = FRAME_TYPE_INO_SHOWEXISTING; break;
      case 'F': step->type_code = FRAME_TYPE_OOO_FILTERED; break;
      case 'U': step->type_code = FRAME_TYPE_OOO_UNFILTERED; break;
      default: return 0;
    }
    if (step->type_code == FRAME_TYPE_INO_SHOWEXISTING) {
      int k;
      for (k = 0; k < s; ++k) {
        if (config->step[k].disp_frame_idx == step->disp_frame_idx) {
          step->pyr_level = config->step[k].pyr_level;
          break;
        }
      }
      // showexisting for a frame not coded before is invalid
      if (k == s) return 0;
      continue;
    }
    token = read_token_after(str, "pyr_level:", &str);
    if (!token) return 0;
    if (strlen(token) == 0) return 0;
    const int pyr_level = atoi(token);
    if (pyr_level <= 0) return 0;
    step->pyr_level = pyr_level;

    token = read_token_after(str, "references:", &str);
    if (!token) {  // no references specified
      step->num_references = -1;
    } else if (strlen(token) > 0) {
      char delim[] = "^";
      char *ptr = token;
      while ((token = my_strtok_r(ptr, delim, &ptr)) != NULL) {
        if (strlen(token) == 0) return 0;
        if (step->num_references >= INTER_REFS_PER_FRAME) return 0;
        step->references[step->num_references] =
            (int8_t)strtol(token, NULL, 10);
        step->num_references++;
      }
    }
    step->refresh = -1;
    token = read_token_after(str, "refresh:", &str);
    if (!token) continue;
    if (strlen(token) == 0) continue;
    const int refresh = atoi(token);
    if (refresh < 0) return 0;
    step->refresh = refresh;
  }
  return 1;
}

int av1_process_subgop_config_set_fromfile(const char *paramfile,
                                           SubGOPSetCfg *config_set) {
  if (!paramfile) return 1;
  if (!strlen(paramfile)) return 1;
  FILE *fp = fopen(paramfile, "r");
  if (!fp) return 0;
  char line[256];
  int linesize = 256;
  while (readline(line, linesize, fp)) {
    if (read_token_after(line, "config:", NULL)) {
      int res = process_subgop_config_fromfile(
          fp, &config_set->config[config_set->num_configs]);
      if (res) {
        res = check_subgop_config(&config_set->config[config_set->num_configs]);
        if (res) {
          check_duplicate_and_add(config_set);
        } else {
          printf(
              "Warning: Subgop config validation failed for config #%d, "
              "skipping the rest.\n",
              config_set->num_configs);
          fclose(fp);
          return 0;
        }
      } else {
        printf("Warning: config parsing failed, skipping the rest.\n");
        fclose(fp);
        return 0;
      }
    } else {
      printf("Warning: config not found, skipping the rest.\n");
      fclose(fp);
      return 0;
    }
  }
  fclose(fp);
  return 1;
}

void av1_print_subgop_config_set(SubGOPSetCfg *config_set) {
  if (!config_set->num_configs) return;
  printf("#SUBGOP CONFIG SET\n");
  printf("##################\n");
  printf("#num_configs:%d\n", config_set->num_configs);
  for (int i = 0; i < config_set->num_configs; ++i) {
    printf("config:#%d\n", i);
    SubGOPCfg *config = &config_set->config[i];
    printf("  num_frames:%d", config->num_frames);
    printf(" subgop_in_gop_code:%d", config->subgop_in_gop_code);
    printf(" num_steps:%d\n", config->num_steps);
    for (int j = 0; j < config->num_steps; ++j) {
      printf("  [step:%d]", j);
      printf(" disp_frame_idx:%d", config->step[j].disp_frame_idx);
      printf(" type_code:%c", config->step[j].type_code);
      printf(" pyr_level:%d", config->step[j].pyr_level);
      if (config->step[j].type_code != FRAME_TYPE_INO_SHOWEXISTING &&
          config->step[j].num_references >= 0) {
        printf(" references:");
        for (int r = 0; r < config->step[j].num_references; ++r) {
          if (r) printf("^");
          printf("%d", config->step[j].references[r]);
        }
      }
      if (config->step[j].refresh >= 0)
        printf(" refresh:%d", config->step[j].refresh);
      printf("\n");
    }
  }
  printf("\n");
}

const SubGOPCfg *av1_find_subgop_config(SubGOPSetCfg *config_set,
                                        int num_frames, int is_last_subgop,
                                        int is_first_subgop) {
  SubGOPCfg *cfg = NULL;
  SUBGOP_IN_GOP_CODE subgop_in_gop_code;
  if (is_last_subgop)
    subgop_in_gop_code = SUBGOP_IN_GOP_LAST;
  else if (is_first_subgop)
    subgop_in_gop_code = SUBGOP_IN_GOP_FIRST;
  else
    subgop_in_gop_code = SUBGOP_IN_GOP_GENERIC;
  for (int i = 0; i < config_set->num_configs; ++i) {
    if (config_set->config[i].num_frames == num_frames) {
      if (config_set->config[i].subgop_in_gop_code == subgop_in_gop_code)
        return &config_set->config[i];
      else if (config_set->config[i].subgop_in_gop_code ==
               SUBGOP_IN_GOP_GENERIC)
        cfg = &config_set->config[i];
    }
  }
  return cfg;
}

const SubGOPCfg *av1_find_subgop_config_exact(
    SubGOPSetCfg *config_set, int num_frames,
    SUBGOP_IN_GOP_CODE subgop_in_gop_code) {
  for (int i = 0; i < config_set->num_configs; ++i) {
    if (config_set->config[i].num_frames == num_frames) {
      if (config_set->config[i].subgop_in_gop_code == subgop_in_gop_code)
        return &config_set->config[i];
    }
  }
  return NULL;
}
