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

#include "common/warnings.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avm/avm_encoder.h"
#include "apps/avmenc.h"
#include "common/tools_common.h"

static const char quantizer_warning_string[] =
    "Bad quantizer values. Quantizer values should not be equal, and should "
    "differ by at least 8.";

struct WarningListNode {
  const char *warning_string;
  struct WarningListNode *next_warning;
};

struct WarningList {
  struct WarningListNode *warning_node;
};

static void add_warning(const char *warning_string,
                        struct WarningList *warning_list) {
  struct WarningListNode **node = &warning_list->warning_node;

  struct WarningListNode *new_node = malloc(sizeof(*new_node));
  if (new_node == NULL) {
    fatal("Unable to allocate warning node.");
  }

  new_node->warning_string = warning_string;
  new_node->next_warning = NULL;

  while (*node != NULL) node = &(*node)->next_warning;

  *node = new_node;
}

static void free_warning_list(struct WarningList *warning_list) {
  while (warning_list->warning_node != NULL) {
    struct WarningListNode *const node = warning_list->warning_node;
    warning_list->warning_node = node->next_warning;
    free(node);
  }
}

static int continue_prompt(int num_warnings) {
  int c;
  fprintf(stderr,
          "%d encoder configuration warning(s). Continue? (y to continue) ",
          num_warnings);
  c = getchar();
  return c == 'y';
}

static void check_quantizer(int min_qp, int max_qp,
                            struct WarningList *warning_list) {
  if (abs(max_qp - min_qp) < 32)
    add_warning(quantizer_warning_string, warning_list);
}

void check_encoder_config(int disable_prompt,
                          const struct AvxEncoderConfig *global_config,
                          const struct avm_codec_enc_cfg *stream_config) {
  int num_warnings = 0;
  struct WarningListNode *warning = NULL;
  struct WarningList warning_list = { 0 };
  (void)global_config;
  check_quantizer(stream_config->rc_min_quantizer,
                  stream_config->rc_max_quantizer, &warning_list);
  /* Count and print warnings. */
  for (warning = warning_list.warning_node; warning != NULL;
       warning = warning->next_warning, ++num_warnings) {
    warn(warning->warning_string);
  }

  free_warning_list(&warning_list);

  if (num_warnings) {
    if (!disable_prompt && !continue_prompt(num_warnings)) exit(EXIT_FAILURE);
  }
}
