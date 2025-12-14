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

#ifndef AVM_COMMON_ARGS_H_
#define AVM_COMMON_ARGS_H_
#include <stdio.h>

#include "avm/avm_codec.h"
#include "avm/avm_encoder.h"
#include "common/args_helper.h"

#ifdef __cplusplus
extern "C" {
#endif

int arg_match(struct arg *arg_, const struct arg_def *def, char **argv);
int parse_cfg(const char *file, cfg_options_t *config);
const char *arg_next(struct arg *arg);
void arg_show_usage(FILE *fp, const struct arg_def *const *defs);
char **argv_dup(int argc, const char **argv);

unsigned int arg_parse_uint(const struct arg *arg);
int arg_parse_int(const struct arg *arg);
struct avm_rational arg_parse_rational(const struct arg *arg);
int arg_parse_enum(const struct arg *arg);
int arg_parse_enum_or_int(const struct arg *arg);
int arg_parse_list(const struct arg *arg, int *list, int n);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_ARGS_H_
