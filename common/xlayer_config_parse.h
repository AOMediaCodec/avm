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

#ifndef AVM_COMMON_XLAYER_CONFIG_PARSE_H_
#define AVM_COMMON_XLAYER_CONFIG_PARSE_H_

#include "common/xlayer_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Parse a JSON configuration file for multi-xlayer encoding.
// Returns 0 on success, -1 on error (with message printed to stderr).
int parse_multi_xlayer_config(const char *json_path, MultiXLayerConfig *cfg);

// Resolve input_source_idx for each xlayer after parsing.
// Must be called between parse and validate.
// Returns 0 on success, -1 on error.
int resolve_input_sources(MultiXLayerConfig *cfg);

// Resolve per-mlayer CI inheritance: if an mlayer's CI field is -1, inherit
// from the parent xlayer's value.  Must be called after
// resolve_input_sources().
void resolve_mlayer_ci(MultiXLayerConfig *cfg);

// Validate a parsed multi-xlayer configuration.
// Returns 0 on success, -1 on error.
int validate_multi_xlayer_config(const MultiXLayerConfig *cfg);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_XLAYER_CONFIG_PARSE_H_
