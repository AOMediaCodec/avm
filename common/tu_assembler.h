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

#ifndef AVM_COMMON_TU_ASSEMBLER_H_
#define AVM_COMMON_TU_ASSEMBLER_H_

#include <stdio.h>
#include <stdint.h>

#include "av2/common/enums.h"
#include "av2/common/av2_common_int.h"
#include "common/xlayer_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TU_ASM_INITIAL_CAPACITY (256 * 1024)

typedef struct TUAssembler {
  uint8_t *buffer;
  size_t size;
  size_t capacity;
  int num_xlayers;
  int xlayer_ids[MAX_NUM_XLAYERS - 1];
  // Structural OBU data populated from config
  GlobalLayerConfigurationRecord global_lcr;
  int msdo_enabled;
  int num_ops_sets;
  OperatingPointSet ops_list[MAX_NUM_OPS_ID];
  AtlasSegmentInfo atlas_info;
  // Reference to the config for OBU population
  const MultiXLayerConfig *config;
} TUAssembler;

// Initialize assembler from multi-xlayer config
int tu_assembler_init(TUAssembler *ta, const MultiXLayerConfig *mcfg);

// Free assembler resources
void tu_assembler_free(TUAssembler *ta);

// Write a Temporal Delimiter OBU (xlayer_id=31)
int tu_assembler_write_td(TUAssembler *ta);

// Write a Global LCR OBU
int tu_assembler_write_global_lcr(TUAssembler *ta);

// Write a Local LCR OBU for the specified xlayer config index.
// The xlayer_info is copied from the Global LCR to ensure decoder-side
// consistency validation passes when both Global and Local LCRs are present.
int tu_assembler_write_local_lcr(TUAssembler *ta, int xlayer_idx);

// Write an MSDO OBU
int tu_assembler_write_msdo(TUAssembler *ta);

// Write an OPS OBU for the specified xlayer_id
int tu_assembler_write_ops(TUAssembler *ta, int xlayer_id);

// Write an Atlas OBU
int tu_assembler_write_atlas(TUAssembler *ta);

// Append per-xlayer OBUs from an encoder packet, rewriting OBU headers
// with the given xlayer_id. Skips per-xlayer TDs and structural OBUs.
int tu_assembler_append_xlayer_obus(TUAssembler *ta, int xlayer_id,
                                    const uint8_t *data, size_t size);

// Flush the assembled buffer to the output file and reset size to 0
int tu_assembler_flush(TUAssembler *ta, FILE *outfile);

// Write structural OBUs (LCR, OPS, Atlas) into the assembler buffer.
// Emits once per TU: only when *first_output is set or has_keyframe is true.
void tu_assembler_write_structural_obus(TUAssembler *ta,
                                        const MultiXLayerConfig *mcfg,
                                        int *first_output, int has_keyframe);

// Split encoder output at internal TD boundaries and write each segment as
// a separate TU.  This is used for multi_layers_lag_test mode where the
// encoder inserts TDs between implicit_output frames at different OrderHints
// to satisfy the DOH constraint.  Each segment gets its own TD, structural
// OBUs (on first_output or keyframe), and xlayer-rewritten frame data.
// Returns the number of TUs written, or -1 on error.
int tu_assembler_write_split_tus(TUAssembler *ta, const MultiXLayerConfig *mcfg,
                                 int xlayer_id, const uint8_t *data,
                                 size_t size, int *first_output, FILE *outfile);

// A parsed TU segment: a contiguous byte range of OBU data between two
// consecutive TD boundaries in an encoder's output.
#define MAX_TU_SEGMENTS 64

typedef struct TUSegmentInfo {
  size_t offset;     // start offset in the source data
  size_t size;       // byte size of this segment (including the TD)
  int has_keyframe;  // 1 if segment contains a keyframe OBU
} TUSegmentInfo;

// Parse encoder output into TU segments split at TD boundaries.
// Each segment spans from one TD to the next (or end of data).
// Returns the number of segments found (stored in segs[]), or -1 on error.
int tu_assembler_parse_tu_segments(const uint8_t *data, size_t size,
                                   TUSegmentInfo *segs, int max_segs);

// Print a summary of all OBUs in the current assembled TU buffer to stdout.
// Must be called before tu_assembler_flush() (which resets the buffer).
void tu_assembler_print_contents(const TUAssembler *ta, int tu_index);

// Populate a GlobalLayerConfigurationRecord from config
void populate_global_lcr_from_config(const MultiXLayerConfig *mcfg,
                                     GlobalLayerConfigurationRecord *glcr);

// Populate an OperatingPointSet from config
void populate_ops_from_config(const OPSConfig *ops_cfg, int xlayer_id,
                              const MultiXLayerConfig *mcfg,
                              OperatingPointSet *ops);

// Populate AtlasSegmentInfo from config
void populate_atlas_from_config(const MultiXLayerConfig *mcfg,
                                AtlasSegmentInfo *atlas);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_TU_ASSEMBLER_H_
