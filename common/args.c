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

#include "common/args.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "aom/aom_integer.h"
#include "aom_ports/msvc.h"
#include "aom/aom_codec.h"

static const char kSbSizeWarningString[] =
    "super_block_size has to be 64 or 128.";
static const char kMinpartWarningString[] =
    "min_partition_size has to be smaller or equal to max_partition_size.";
static const char kMaxpartWarningString[] =
    "max_partition_size has to be smaller or equal to super_block_size.";
#if defined(__GNUC__) && __GNUC__
extern void die(const char *fmt, ...) __attribute__((noreturn));
#else
extern void die(const char *fmt, ...);
#endif

static char *ignore_front_spaces(const char *str) {
  while (str[0] == ' ' || str[0] == '\t') ++str;
  return (char *)str;
}

static void ignore_end_spaces(char *str) {
  char *end = str + strlen(str);
  while (end > str && (end[0] == ' ' || end[0] == '\t' || end[0] == '\n' ||
                       end[0] == '\r' || end[0] == '\0'))
    --end;
  if (end >= str) end[1] = '\0';
}

int parse_cfg(const char *file, cfg_options_t *config) {
  char line[1024 * 10];
  FILE *f = fopen(file, "r");
  if (!f) return 1;

#define GET_PARAMS(field)          \
  if (strcmp(left, #field) == 0) { \
    config->field = atoi(right);   \
    continue;                      \
  }

  while (fgets(line, sizeof(line) - 1, f)) {
    char *actual_line = ignore_front_spaces(line);
    char *left, *right, *comment;
    size_t length = strlen(actual_line);

    if (length == 0 || actual_line[0] == '#') continue;
    right = strchr(actual_line, '=');
    if (right == NULL) continue;
    right[0] = '\0';

    left = ignore_front_spaces(actual_line);
    right = ignore_front_spaces(right + 1);

    comment = strchr(right, '#');
    if (comment != NULL) comment[0] = '\0';

    ignore_end_spaces(left);
    ignore_end_spaces(right);

    GET_PARAMS(superblock_size);
    GET_PARAMS(max_partition_size);
    GET_PARAMS(min_partition_size);
    GET_PARAMS(enable_ab_partitions);
    GET_PARAMS(enable_rect_partitions);
    GET_PARAMS(enable_1to4_partitions);
    GET_PARAMS(disable_ml_partition_speed_features);
    GET_PARAMS(disable_ml_transform_speed_features);
    GET_PARAMS(enable_sdp);
    GET_PARAMS(enable_mrls);
#if CONFIG_TIP
    GET_PARAMS(enable_tip);
#endif  // CONFIG_TIP
#if CONFIG_BAWP
    GET_PARAMS(enable_bawp);
#endif  // CONFIG_BAWP
#if CONFIG_FORWARDSKIP
    GET_PARAMS(enable_fsc);
#endif  // CONFIG_FORWARDSKIP
#if CONFIG_ORIP
    GET_PARAMS(enable_orip);
#endif
#if CONFIG_IST
    GET_PARAMS(enable_ist);
#endif
#if CONFIG_CROSS_CHROMA_TX
    GET_PARAMS(enable_cctx);
#endif  // CONFIG_CROSS_CHROMA_TX
#if CONFIG_IBP_DC || CONFIG_IBP_DIR
    GET_PARAMS(enable_ibp);
#endif
#if CONFIG_ADAPTIVE_MVD
    GET_PARAMS(enable_adaptive_mvd);
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_FLEX_MVRES
    GET_PARAMS(enable_flex_mvres);
#endif  // CONFIG_FLEX_MVRES
#if CONFIG_ADAPTIVE_DS_FILTER
    GET_PARAMS(enable_cfl_ds_filter);
#endif  // CONFIG_ADAPTIVE_DS_FILTER
#if CONFIG_JOINT_MVD
    GET_PARAMS(enable_joint_mvd);
#endif  // CONFIG_JOINT_MVD
    GET_PARAMS(enable_flip_idtx);
    GET_PARAMS(enable_deblocking);
    GET_PARAMS(enable_cdef);
    GET_PARAMS(enable_restoration);
#if CONFIG_CCSO
    GET_PARAMS(enable_ccso);
#endif
#if CONFIG_PEF
    GET_PARAMS(enable_pef);
#endif  // CONFIG_PEF
    GET_PARAMS(enable_obmc);
    GET_PARAMS(enable_warped_motion);
    GET_PARAMS(enable_global_motion);
#if CONFIG_EXTENDED_WARP_PREDICTION
    GET_PARAMS(enable_warped_causal);
    GET_PARAMS(enable_warp_delta);
    GET_PARAMS(enable_warp_extend);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
    GET_PARAMS(enable_diff_wtd_comp);
    GET_PARAMS(enable_interintra_comp);
    GET_PARAMS(enable_masked_comp);
    GET_PARAMS(enable_onesided_comp);
    GET_PARAMS(enable_palette);
    GET_PARAMS(enable_intrabc);
#if CONFIG_IBC_SR_EXT
    GET_PARAMS(enable_intrabc_ext);
#endif  // CONFIG_IBC_SR_EXT
    GET_PARAMS(enable_cfl_intra);
    GET_PARAMS(enable_smooth_intra);
    GET_PARAMS(enable_filter_intra);
    GET_PARAMS(enable_angle_delta);
#if CONFIG_OPTFLOW_REFINEMENT
    GET_PARAMS(enable_opfl_refine);
#endif  // CONFIG_OPTFLOW_REFINEMENT
    GET_PARAMS(enable_intra_edge_filter);
    GET_PARAMS(enable_tx64);
    GET_PARAMS(enable_smooth_interintra);
    GET_PARAMS(enable_interinter_wedge);
    GET_PARAMS(enable_interintra_wedge);
    GET_PARAMS(enable_paeth_intra);
    GET_PARAMS(enable_trellis_quant);
    GET_PARAMS(enable_ref_frame_mvs);
    GET_PARAMS(enable_reduced_reference_set);
    GET_PARAMS(reduced_tx_type_set);
    GET_PARAMS(max_drl_refmvs);
#if CONFIG_REF_MV_BANK
    GET_PARAMS(enable_refmvbank);
#endif  // CONFIG_REF_MV_BANK
#if CONFIG_PAR_HIDING
    GET_PARAMS(enable_parity_hiding);
#endif  // CONFIG_PAR_HIDING

    fprintf(stderr, "\nInvalid parameter: %s", left);
    exit(-1);
  }

  if (config->superblock_size != 128 && config->superblock_size != 64) {
    fprintf(stderr, "\n%s", kSbSizeWarningString);
    exit(-1);
  }
  if (config->min_partition_size > config->max_partition_size) {
    fprintf(stderr, "\n%s", kMinpartWarningString);
    exit(-1);
  }
  if (config->max_partition_size > config->superblock_size) {
    fprintf(stderr, "\n%s", kMaxpartWarningString);
    exit(-1);
  }

  fclose(f);
  config->init_by_cfg_file = 1;

  return 0;
}

int arg_match(struct arg *arg_, const struct arg_def *def, char **argv) {
  char err_msg[ARG_ERR_MSG_MAX_LEN];
  int ret = arg_match_helper(arg_, def, argv, err_msg);
  if (err_msg[0] != '\0') {
    die(err_msg);
  }
  return ret;
}

const char *arg_next(struct arg *arg) {
  if (arg->argv[0]) arg->argv += arg->argv_step;

  return *arg->argv;
}

char **argv_dup(int argc, const char **argv) {
  char **new_argv = malloc((argc + 1) * sizeof(*argv));

  memcpy(new_argv, argv, argc * sizeof(*argv));
  new_argv[argc] = NULL;
  return new_argv;
}

void arg_show_usage(FILE *fp, const struct arg_def *const *defs) {
  char option_text[40] = { 0 };

  for (; *defs; defs++) {
    const struct arg_def *def = *defs;
    char *short_val = def->has_val ? " <arg>" : "";
    char *long_val = def->has_val ? "=<arg>" : "";

    if (def->short_name && def->long_name) {
      char *comma = def->has_val ? "," : ",      ";

      snprintf(option_text, 37, "-%s%s%s --%s%6s", def->short_name, short_val,
               comma, def->long_name, long_val);
    } else if (def->short_name)
      snprintf(option_text, 37, "-%s%s", def->short_name, short_val);
    else if (def->long_name)
      snprintf(option_text, 37, "          --%s%s", def->long_name, long_val);

    fprintf(fp, "  %-37s\t%s\n", option_text, def->desc);

    if (def->enums) {
      const struct arg_enum_list *listptr;

      fprintf(fp, "  %-37s\t  ", "");

      for (listptr = def->enums; listptr->name; listptr++)
        fprintf(fp, "%s%s", listptr->name, listptr[1].name ? ", " : "\n");
    }
  }
}

unsigned int arg_parse_uint(const struct arg *arg) {
  char err_msg[ARG_ERR_MSG_MAX_LEN];
  unsigned int ret = arg_parse_uint_helper(arg, err_msg);
  if (err_msg[0] != '\0') {
    die(err_msg);
  }
  return ret;
}

int arg_parse_int(const struct arg *arg) {
  char err_msg[ARG_ERR_MSG_MAX_LEN];
  int ret = arg_parse_int_helper(arg, err_msg);
  if (err_msg[0] != '\0') {
    die(err_msg);
  }
  return ret;
}

struct aom_rational arg_parse_rational(const struct arg *arg) {
  char err_msg[ARG_ERR_MSG_MAX_LEN];
  struct aom_rational ret = arg_parse_rational_helper(arg, err_msg);
  if (err_msg[0] != '\0') {
    die(err_msg);
  }
  return ret;
}

int arg_parse_enum(const struct arg *arg) {
  char err_msg[ARG_ERR_MSG_MAX_LEN];
  int ret = arg_parse_enum_helper(arg, err_msg);
  if (err_msg[0] != '\0') {
    die(err_msg);
  }
  return ret;
}

int arg_parse_enum_or_int(const struct arg *arg) {
  char err_msg[ARG_ERR_MSG_MAX_LEN];
  int ret = arg_parse_enum_or_int_helper(arg, err_msg);
  if (err_msg[0] != '\0') {
    die(err_msg);
  }
  return ret;
}

// parse a comma separated list of at most n integers
// return the number of elements in the list
int arg_parse_list(const struct arg *arg, int *list, int n) {
  char err_msg[ARG_ERR_MSG_MAX_LEN];
  int ret = arg_parse_list_helper(arg, list, n, err_msg);
  if (err_msg[0] != '\0') {
    die(err_msg);
  }
  return ret;
}
