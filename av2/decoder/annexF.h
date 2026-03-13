/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_DECODER_ANNEX_F_H_
#define AVM_AV2_DECODER_ANNEX_F_H_

#include "av2/common/enums.h"
#include "av2/common/obu_util.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANNEX_F_INVALID (-1)

// Sub-bitstream extraction state, implementing Annex F of the AV2 spec.
// This state is maintained as OBUs are parsed,
// and used to filter OBUs during the decoding process.
typedef struct SubBitstreamExtractionState {
  // ---- Phase 1 outputs (Annex F Section: F.3.1) ----

  // retentionMap[xlayer_id][mlayer_id][tlayer_id] -> 0 (discard) or 1 (retain)
  int retention_map[MAX_NUM_XLAYERS][MAX_NUM_MLAYERS][MAX_NUM_TLAYERS];

  // xLayerIsSelected[xlayer_id] -> 0 or 1
  int xlayer_is_selected[MAX_NUM_XLAYERS];

  // Per-xlayer operational parameters from selected operating point
  int profile_idc[MAX_NUM_XLAYERS];
  int level_idc[MAX_NUM_XLAYERS];
  int tier_idc[MAX_NUM_XLAYERS];
  int mlayer_cnt[MAX_NUM_XLAYERS];

  // ---- Construction state ----

  // Whether sub-bitstream extraction is enabled (user selected an OP)
  int extraction_enabled;

  // Bitstream type determination (Step 2)
  int bitstream_type_determined;
  int is_multistream;  // 0 = singlestream, 1 = multistream

  // Whether the retention map has been fully constructed for current CVS
  int retention_map_ready;

  // Global operating point selection results (Step 3)
  int global_ops_selected;
  int global_ops_id;
  int global_op_idx;

  // Local operating point selection per xlayer (Step 4)
  int local_ops_selected[MAX_NUM_XLAYERS];
  int local_ops_id[MAX_NUM_XLAYERS];
  int local_op_idx[MAX_NUM_XLAYERS];

  // Extended layers present in the bitstream (from MSDO or global LCR)
  int xlayer_present[MAX_NUM_XLAYERS];
  int num_xlayers_present;
  int xlayer_ids_present[MAX_NUM_XLAYERS];

  // Track OBus
  int msdo_seen;
  int global_lcr_seen;
  int global_ops_seen;
  int local_ops_seen[MAX_NUM_XLAYERS];

  // ---- Statistics ----
  int64_t obus_removed;
  int64_t obus_retained;
} SubBitstreamExtractionState;

struct AV2Decoder;

// clang-format off
// Operating point selection and analysis process (Annex F, Section F.3.1):
//
// Spec Step | Description                             | Function
// ----------|-----------------------------------------|------------------------------------
//     1     | Initialize outputs                      | av2_sbe_init()
//     2     | Set bitstream type to multistream (MSDO) | av2_sbe_process_msdo()
//     2     | Set bitstream type to multistream (global LCR)  | av2_sbe_process_global_lcr()
//     3     | Global operating point selection        | av2_sbe_process_global_ops()
//     4     | Local OPS selection & retention map     | av2_sbe_process_local_ops()
//     4     | Retention map construction              | av2_sbe_build_retention_map()
//     5     | Extract profile/level/tier/mlayer count | av2_sbe_extract_seq_header_params()
//     6     | Return outputs                         | (implicit)
// clang-format on

// Initialize or reset the sub-bitstream extraction state.
// Called at decoder init and at each new coded video sequence.
// Does NOT reset extraction_enabled (preserved from CLI).
void av2_sbe_init(SubBitstreamExtractionState *sbe);

// Step 2: Process MSDO OBU to set bitstream type to multistream
// and record present extended layers.
void av2_sbe_process_msdo(SubBitstreamExtractionState *sbe, int num_streams,
                          const int *stream_ids);

// Step 2: Process global LCR OBU (obu_xlayer_id == 31) to determine
// multistream bitstream type and record present extended layers.
void av2_sbe_process_global_lcr(SubBitstreamExtractionState *sbe,
                                int num_xlayers, const int *xlayer_ids);

// Step 3: Process global OPS OBU (obu_xlayer_id == 31).
// If ops_id matches the user-selected OPS, populates xlayer_is_selected[]
// from ops_xlayer_map.
void av2_sbe_process_global_ops(SubBitstreamExtractionState *sbe, int ops_id,
                                int ops_cnt, int selected_ops_id,
                                int selected_op_index, int ops_xlayer_map,
                                int ops_mlayer_info_idc);

// Step 4: Process local OPS OBU (obu_xlayer_id != 31).
// Records that a local OPS was seen for this xlayer.
void av2_sbe_process_local_ops(SubBitstreamExtractionState *sbe, int xlayer_id,
                               int ops_id, int ops_cnt);

// Build the retention map from collected OPS/LCR data (Steps 3-5).
// Called once before the first non-structural OBU in a temporal unit.
// pbi is needed to access ops_list for mlayer/tlayer map data.
// Returns 1 if map was built, 0 if already ready.
int av2_sbe_build_retention_map(SubBitstreamExtractionState *sbe,
                                struct AV2Decoder *pbi);

// Phase 2 core filter: determine whether an OBU should be retained
// in the sub-bitstream (Annex F F.3.2).
// Returns 1 = retain, 0 = remove.
int av2_sbe_should_retain_obu(const SubBitstreamExtractionState *sbe,
                              OBU_TYPE obu_type, int obu_xlayer_id,
                              int obu_mlayer_id, int obu_tlayer_id);

// Step 5 fallback: extract profile/level/tier from sequence header
// when no OPS or LCR provides this information.
void av2_sbe_extract_seq_header_params(SubBitstreamExtractionState *sbe,
                                       int xlayer_id, int seq_profile_idc,
                                       int seq_max_level_idx, int seq_tier,
                                       int seq_max_mlayer_cnt);

// Returns 1 if the OBU type is a structural OBU that should always
// be parsed before filtering can begin.
static INLINE int is_sbe_structural_obu(OBU_TYPE obu_type) {
  return obu_type == OBU_TEMPORAL_DELIMITER ||
         obu_type == OBU_MULTI_STREAM_DECODER_OPERATION ||
         obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
         obu_type == OBU_ATLAS_SEGMENT || obu_type == OBU_OPERATING_POINT_SET ||
         obu_type == OBU_BUFFER_REMOVAL_TIMING ||
         obu_type == OBU_METADATA_SHORT || obu_type == OBU_METADATA_GROUP ||
         obu_type == OBU_PADDING;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_DECODER_ANNEX_F_H_
