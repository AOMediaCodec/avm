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

// Implements Annex F: Sub-bitstream extraction (informative) of the
// AV2 Bitstream & Decoding Process Specification.
//
// The extraction process has two phases:
//   Phase 1 (Operating Point Selection and Analysis): Builds a 3D retention
//     map indicating which (xlayer, mlayer, tlayer) combinations to retain.
//   Phase 2 (Sub-bitstream Extraction): Filters OBUs based on the map.

/* clang-format off */
/*
//=============================================================================
// Table F.1: Phase 1 Steps and Corresponding Functions
//=============================================================================
 *
 * Step | Trigger                           | Function
 * -----|-----------------------------------|----------------------------------
 *   1  | Decoder init / new CVS            | av2_sbe_init()
 *   2  | MSDO OBU parsed                   | av2_sbe_process_msdo()
 *   2  | Global LCR (xlayer_id==31)        | av2_sbe_process_global_lcr()
 *   2  | No MSDO or global LCR (fallback)  | av2_sbe_build_retention_map()
 *   3  | Global OPS OBU parsed             | av2_sbe_process_global_ops()
 *   3  | No global OPS match (fallback)    | av2_sbe_build_retention_map()
 *   4  | Local OPS OBU parsed              | av2_sbe_process_local_ops()
 *   4  | Retention map construction        | av2_sbe_build_retention_map()
 *   5  | Sequence header parsed (fallback) | av2_sbe_extract_seq_header_params()
 *
 * Notes:
 * - Steps 1-5 correspond directly to Section F.3.1 of the AV2 specification.
 * - av2_sbe_build_retention_map() finalizes Steps 2-5 and is called once
 *   before the first non-structural OBU in each temporal unit.
 * - extraction_enabled is set by the CLI (--select-ops) and is not reset
 *   by av2_sbe_init() across coded video sequence boundaries.
 */

/*
//=============================================================================
// Table F.2: Bitstream Type Determination (Step 2)
//=============================================================================
 *
 * OBU(s) seen          | is_multistream | xlayers recorded
 * ---------------------|----------------|-------------------------------
 * MSDO only            |       1        | from MSDO stream list
 * Global LCR only      |       1        | from LCR xlayer_ids list
 * MSDO + Global LCR    |       1        | from both (duplicates ignored)
 * Neither              |       0        | xlayer 0 only (singlestream)
 *
 * Notes:
 * - MSDO takes priority: if seen, global LCR does not re-determine type.
 * - In all multistream cases, retention_map[GLOBAL_XLAYER_ID][0][0] is
 *   pre-set to 1 to ensure global OBUs (xlayer_id==31) are always retained.
 * - Singlestream fallback is finalized in av2_sbe_build_retention_map()
 *   when bitstream_type_determined == 0 at map construction time.
 */

/*
//=============================================================================
// Table F.3: Retention Map Population Priority (Step 4, per selected xlayer)
//=============================================================================
 *
 * Priority | Source                               | Condition
 * ---------|--------------------------------------|------------------------
 *    1     | Local OPS via --select-local-ops     | Explicit CLI per xlayer
 *    2     | Local OPS via --select-ops           | Singlestream only
 *    3     | Global OPS (ops_mlayer_info_idc >= 1)| Global OP provides maps
 *    4     | Retain all (all mlayer x tlayer)     | No OPS info available
 *
 * Notes:
 * - retention_map[xid][mlayer][tlayer] = 1 for each (mlayer, tlayer) pair
 *   indicated by the ops_mlayer_map and ops_tlayer_map bitmaps.
 * - Priority 4 (retain all) is the safe default: if the bitstream
 *   carries no mlayer/tlayer constraint info, no frames are dropped.
 */

/*
//=============================================================================
// Table F.4: Structural OBUs (always parsed, never filtered in Phase 2)
//=============================================================================
 *
 * OBU Type                       | Purpose in Phase 1
 * -------------------------------|---------------------------------------------
 * OBU_TEMPORAL_DELIMITER         | Marks start of temporal unit
 * OBU_MULTI_STREAM_DECODER_OPERATION | Step 2: multistream detection, xlayer list
 * OBU_LAYER_CONFIGURATION_RECORD | Step 2: xlayer discovery (global or local)
 * OBU_ATLAS_SEGMENT              | Cross-layer structural metadata
 * OBU_OPERATING_POINT_SET        | Steps 3/4: xlayer selection and map data
 * OBU_BUFFER_REMOVAL_TIMING      | Decoder timing model
 * OBU_METADATA_SHORT             | Global metadata, no SBE-relevant content
 * OBU_METADATA_GROUP             | Global metadata, no SBE-relevant content
 * OBU_PADDING                    | No semantic content, always passed through
 *
 * Notes:
 * - Structural OBUs are identified by is_sbe_structural_obu() (annexF.h).
 * - They are always parsed by the OBU loop regardless of SBE state, so that
 *   Phase 1 state is fully populated before Phase 2 filtering begins.
 * - OBU_SEQUENCE_HEADER is NOT in this list; it is preserved by Phase 2
 *   via the essential-OBU exception at (mId=0, tId=0) in
 *   av2_sbe_should_retain_obu().
 */
/* clang-format on */

#include "av2/decoder/annexF.h"

#include <string.h>

#include "av2/common/av2_common_int.h"
#include "av2/decoder/decoder.h"

void av2_sbe_init(SubBitstreamExtractionState *sbe) {
  // Step 1: Initialize retentionMap to 0
  memset(sbe->retention_map, 0, sizeof(sbe->retention_map));
  memset(sbe->xlayer_is_selected, 0, sizeof(sbe->xlayer_is_selected));

  // Initialize profile/level/tier/mlayerCnt to INVALID
  for (int i = 0; i < MAX_NUM_XLAYERS; i++) {
    sbe->profile_idc[i] = ANNEX_F_INVALID;
    sbe->level_idc[i] = ANNEX_F_INVALID;
    sbe->tier_idc[i] = ANNEX_F_INVALID;
    sbe->mlayer_cnt[i] = ANNEX_F_INVALID;

    sbe->local_ops_selected[i] = 0;
    sbe->local_ops_id[i] = ANNEX_F_INVALID;
    sbe->local_op_idx[i] = ANNEX_F_INVALID;
    sbe->local_ops_seen[i] = 0;
    sbe->xlayer_present[i] = 0;
    sbe->xlayer_ids_present[i] = 0;
  }

  sbe->bitstream_type_determined = 0;
  sbe->is_multistream = 0;
  sbe->retention_map_ready = 0;

  sbe->global_ops_selected = 0;
  sbe->global_ops_id = ANNEX_F_INVALID;
  sbe->global_op_idx = ANNEX_F_INVALID;

  sbe->num_xlayers_present = 0;
  sbe->msdo_seen = 0;
  sbe->global_lcr_seen = 0;
  sbe->global_ops_seen = 0;

  sbe->obus_removed = 0;
  sbe->obus_retained = 0;
}

// Step 2: Process MSDO OBU.
// The presence of an MSDO OBU is definitive evidence of a multistream
// bitstream. Once seen, bitstream type is locked to multistream for this CVS.
void av2_sbe_process_msdo(SubBitstreamExtractionState *sbe, int num_streams,
                          const int *stream_ids) {
  // Part A: MSDO triggers multistream.
  // Mark that an MSDO was seen and lock in multistream mode. This flag is
  // checked by av2_sbe_process_global_lcr() and av2_sbe_build_retention_map()
  // so they do not override the bitstream type determination.
  sbe->msdo_seen = 1;
  sbe->is_multistream = 1;
  sbe->bitstream_type_determined = 1;

  // Record the extended layer IDs (xlayer_ids) carried in the MSDO stream
  // list. Each stream_id in the MSDO corresponds to one extended layer present
  // in the bitstream. Duplicates are skipped via the xlayer_present[] guard.
  for (int i = 0; i < num_streams; i++) {
    int xid = stream_ids[i];
    if (xid >= 0 && xid < MAX_NUM_XLAYERS - 1 && !sbe->xlayer_present[xid]) {
      sbe->xlayer_present[xid] = 1;
      sbe->xlayer_ids_present[sbe->num_xlayers_present] = xid;
      sbe->num_xlayers_present++;
    }
  }

  // Pre-mark global OBUs (obu_xlayer_id == 31) as retained. In a multistream
  // bitstream, global OBUs carry shared structural information (global LCR,
  // global OPS) that must always be passed through regardless of which
  // extended layer the user selected.
  sbe->retention_map[GLOBAL_XLAYER_ID][0][0] = 1;
}

// Step 2 (partial): Process global LCR OBU (obu_xlayer_id == 31).
// Part B: Global LCR (xlayer_id==31) also triggers multistream.
// A global LCR is an alternative indicator of a multistream bitstream. If an
// MSDO was already seen, the bitstream type is already determined and this
// function only adds any newly discovered xlayer IDs. If no MSDO was seen,
// the presence of a global LCR is sufficient to declare multistream.
void av2_sbe_process_global_lcr(SubBitstreamExtractionState *sbe,
                                int num_xlayers, const int *xlayer_ids) {
  // Record that a global LCR has been seen. This flag is used by
  // av2_sbe_build_retention_map() to distinguish between a global LCR that
  // arrived after an MSDO vs. one that arrived standalone.
  sbe->global_lcr_seen = 1;
  if (!sbe->msdo_seen) {
    // No MSDO was seen before this global LCR, so the global LCR alone is
    // sufficient to determine that this is a multistream bitstream.
    sbe->is_multistream = 1;
    sbe->bitstream_type_determined = 1;
  }

  // Record the extended layer IDs listed in the global LCR. These may
  // overlap with or extend the list already populated by the MSDO. The
  // xlayer_present[] guard ensures each xlayer is recorded only once even
  // if both MSDO and global LCR reference the same layer.
  for (int i = 0; i < num_xlayers; i++) {
    int xid = xlayer_ids[i];
    if (xid >= 0 && xid < MAX_NUM_XLAYERS - 1 && !sbe->xlayer_present[xid]) {
      sbe->xlayer_present[xid] = 1;
      sbe->xlayer_ids_present[sbe->num_xlayers_present] = xid;
      sbe->num_xlayers_present++;
    }
  }

  // Same as in av2_sbe_process_msdo(): pre-mark global OBUs (xlayer_id==31)
  // as retained so that shared structural OBUs are never filtered out,
  // regardless of which extended layer the user selected.
  sbe->retention_map[GLOBAL_XLAYER_ID][0][0] = 1;
}

// Step 3: Process global OPS OBU (obu_xlayer_id == 31).
void av2_sbe_process_global_ops(SubBitstreamExtractionState *sbe, int ops_id,
                                int ops_cnt, int selected_ops_id,
                                int selected_op_index, int ops_xlayer_map,
                                int ops_mlayer_info_idc) {
  sbe->global_ops_seen = 1;

  // The abstract function global_operating_point_selection() is implemented
  // as: match the CLI-provided selected_ops_id against this OPS.
  if (selected_ops_id >= 0 && ops_id == selected_ops_id && ops_cnt > 0) {
    if (selected_op_index >= 0 && selected_op_index < ops_cnt) {
      sbe->global_ops_selected = 1;
      sbe->global_ops_id = ops_id;
      sbe->global_op_idx = selected_op_index;

      (void)ops_mlayer_info_idc;

      // Set xLayerIsSelected from ops_xlayer_map bitmap
      for (int i = 0; i < MAX_NUM_XLAYERS - 1; i++) {
        if (ops_xlayer_map & (1 << i)) {
          sbe->xlayer_is_selected[i] = 1;
        }
      }
    }
  }
}

// Step 4: Process local OPS OBU (obu_xlayer_id != 31).
void av2_sbe_process_local_ops(SubBitstreamExtractionState *sbe, int xlayer_id,
                               int ops_id, int ops_cnt) {
  if (xlayer_id < 0 || xlayer_id >= MAX_NUM_XLAYERS - 1) return;
  sbe->local_ops_seen[xlayer_id] = 1;
  (void)ops_id;
  (void)ops_cnt;
}

// Build the complete retention map (Steps 3-5).
int av2_sbe_build_retention_map(SubBitstreamExtractionState *sbe,
                                struct AV2Decoder *pbi) {
  if (sbe->retention_map_ready) return 0;

  // Step 2 finalization: if no MSDO or global LCR, it's singlestream
  if (!sbe->bitstream_type_determined) {
    sbe->is_multistream = 0;
    sbe->bitstream_type_determined = 1;
    // Singlestream: mark xlayer 0 as the only present xlayer
    if (sbe->num_xlayers_present == 0) {
      sbe->xlayer_present[0] = 1;
      sbe->xlayer_ids_present[0] = 0;
      sbe->num_xlayers_present = 1;
    }
  }

  // Step 3 finalization: if no global OP selected, mark all present
  // xlayers as selected
  if (!sbe->global_ops_selected) {
    for (int i = 0; i < sbe->num_xlayers_present; i++) {
      sbe->xlayer_is_selected[sbe->xlayer_ids_present[i]] = 1;
    }
  }

  // Step 4: For each selected xlayer, build retention map entries
  for (int xid = 0; xid < MAX_NUM_XLAYERS - 1; xid++) {
    if (!sbe->xlayer_is_selected[xid]) continue;

    int map_populated = 0;

    // Check if user has specified a local OPS selection for this xlayer
    // via the --select-local-ops CLI option (or via the selected_ops for
    // singlestream where the OPS is local).
    int target_local_ops_id = ANNEX_F_INVALID;
    int target_local_op_idx = ANNEX_F_INVALID;

    // Priority 1: Explicit --select-local-ops for this xlayer
    if (sbe->local_ops_selected[xid] &&
        sbe->local_ops_id[xid] != ANNEX_F_INVALID) {
      target_local_ops_id = sbe->local_ops_id[xid];
      target_local_op_idx = sbe->local_op_idx[xid];
    }
    // Priority 2: For singlestream, --select-ops acts as local OPS selection
    else if (!sbe->is_multistream && pbi->selected_ops_id >= 0) {
      target_local_ops_id = pbi->selected_ops_id;
      target_local_op_idx = pbi->selected_op_index;
    }

    // Check the ops_list for this xlayer
    if (target_local_ops_id >= 0 && target_local_ops_id < MAX_NUM_OPS_ID) {
      const struct OperatingPointSet *ops =
          &pbi->ops_list[xid][target_local_ops_id];
      if (ops->valid && ops->ops_id == target_local_ops_id &&
          target_local_op_idx >= 0 && target_local_op_idx < ops->ops_cnt) {
        const OperatingPoint *op = &ops->op[target_local_op_idx];
        sbe->local_ops_selected[xid] = 1;
        sbe->local_ops_id[xid] = target_local_ops_id;
        sbe->local_op_idx[xid] = target_local_op_idx;

        // Use ops_mlayer_map and ops_tlayer_map from the local OP
        int mlayer_map = op->mlayer_info.ops_mlayer_map[xid];
        for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
          if (mlayer_map & (1 << j)) {
            int tlayer_map = op->mlayer_info.ops_tlayer_map[xid][j];
            for (int k = 0; k < MAX_NUM_TLAYERS; k++) {
              if (tlayer_map & (1 << k)) {
                sbe->retention_map[xid][j][k] = 1;
              }
            }
          }
        }
        map_populated = 1;
      }
    }

    // If global OP was selected and provides mlayer info for this xlayer
    // (ops_mlayer_info_idc == 1), use the global OPS mlayer/tlayer maps
    if (!map_populated && sbe->global_ops_selected) {
      const struct OperatingPointSet *global_ops =
          &pbi->ops_list[GLOBAL_XLAYER_ID][sbe->global_ops_id];
      if (global_ops->valid && global_ops->ops_mlayer_info_idc >= 1 &&
          sbe->global_op_idx >= 0 && sbe->global_op_idx < global_ops->ops_cnt) {
        const OperatingPoint *op = &global_ops->op[sbe->global_op_idx];
        int mlayer_map = op->mlayer_info.ops_mlayer_map[xid];
        if (mlayer_map != 0) {
          for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
            if (mlayer_map & (1 << j)) {
              int tlayer_map = op->mlayer_info.ops_tlayer_map[xid][j];
              for (int k = 0; k < MAX_NUM_TLAYERS; k++) {
                if (tlayer_map & (1 << k)) {
                  sbe->retention_map[xid][j][k] = 1;
                }
              }
            }
          }
          map_populated = 1;
        }
      }
    }

    // If no operating point provided mlayer/tlayer info, retain all
    if (!map_populated) {
      for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
        for (int k = 0; k < MAX_NUM_TLAYERS; k++) {
          sbe->retention_map[xid][j][k] = 1;
        }
      }
    }
  }

  // Step 5: Extract profile/level/tier/mlayerCnt per selected xlayer
  for (int xid = 0; xid < MAX_NUM_XLAYERS - 1; xid++) {
    if (!sbe->xlayer_is_selected[xid]) continue;
    if (sbe->profile_idc[xid] != ANNEX_F_INVALID) continue;  // already set

    // Try global OP first
    if (sbe->global_ops_selected) {
      const struct OperatingPointSet *global_ops =
          &pbi->ops_list[GLOBAL_XLAYER_ID][sbe->global_ops_id];
      if (global_ops->valid && global_ops->ops_ptl_present_flag &&
          sbe->global_op_idx >= 0 && sbe->global_op_idx < global_ops->ops_cnt) {
        const OperatingPoint *op = &global_ops->op[sbe->global_op_idx];
        // Global OPS stores per-xlayer PTL when ops_xlayer_map includes xid
        if (op->ops_xlayer_map & (1 << xid)) {
          sbe->profile_idc[xid] = op->ops_seq_profile_idc[xid];
          sbe->level_idc[xid] = op->ops_level_idx[xid];
          sbe->tier_idc[xid] = op->ops_tier_flag[xid];
          sbe->mlayer_cnt[xid] = op->ops_mlayer_count[xid];
          continue;
        }
      }
    }

    // Try local OP
    if (sbe->local_ops_selected[xid]) {
      const struct OperatingPointSet *local_ops =
          &pbi->ops_list[xid][sbe->local_ops_id[xid]];
      if (local_ops->valid && local_ops->ops_ptl_present_flag &&
          sbe->local_op_idx[xid] >= 0 &&
          sbe->local_op_idx[xid] < local_ops->ops_cnt) {
        const OperatingPoint *op = &local_ops->op[sbe->local_op_idx[xid]];
        sbe->profile_idc[xid] = op->ops_seq_profile_idc[xid];
        sbe->level_idc[xid] = op->ops_level_idx[xid];
        sbe->tier_idc[xid] = op->ops_tier_flag[xid];
        sbe->mlayer_cnt[xid] = op->ops_mlayer_count[xid];
      }
    }
  }

  sbe->retention_map_ready = 1;
  return 1;
}

// Phase 2: Sub-bitstream extraction OBU filter (Annex F F.3.2).
int av2_sbe_should_retain_obu(const SubBitstreamExtractionState *sbe,
                              OBU_TYPE obu_type, int obu_xlayer_id,
                              int obu_mlayer_id, int obu_tlayer_id) {
  if (!sbe->extraction_enabled || !sbe->retention_map_ready) {
    return 1;  // Retain all if extraction not active or map not ready
  }

  const int xId = obu_xlayer_id;
  const int mId = obu_mlayer_id;
  const int tId = obu_tlayer_id;

  // Bounds check — invalid layer IDs mean the OBU is not part of any
  // valid operating point; discard it.
  if (xId < 0 || xId >= MAX_NUM_XLAYERS || mId < 0 || mId >= MAX_NUM_MLAYERS ||
      tId < 0 || tId >= MAX_NUM_TLAYERS) {
    return 0;
  }

  // Check if extended layer xId is selected: exists at least one pair (j,k)
  // where retention_map[xId][j][k] == 1
  int is_xlayer_selected = 0;
  for (int j = 0; j < MAX_NUM_MLAYERS && !is_xlayer_selected; j++) {
    for (int k = 0; k < MAX_NUM_TLAYERS && !is_xlayer_selected; k++) {
      if (sbe->retention_map[xId][j][k]) {
        is_xlayer_selected = 1;
      }
    }
  }

  if (!is_xlayer_selected) {
    return 0;  // Remove: extended layer not selected at all
  }

  if (!sbe->retention_map[xId][mId][tId]) {
    if (mId == 0 && tId == 0) {
      // Preserve certain OBU types at (mId=0, tId=0) within selected xlayers
      if (obu_type == OBU_SEQUENCE_HEADER ||
          obu_type == OBU_TEMPORAL_DELIMITER ||
          obu_type == OBU_LAYER_CONFIGURATION_RECORD ||
          obu_type == OBU_ATLAS_SEGMENT ||
          obu_type == OBU_OPERATING_POINT_SET) {
        return 1;  // Preserve these essential OBU types
      }
    }
    return 0;  // Remove: (mId, tId) not retained
  }

  return 1;  // Retain: layer combination is in retention map
}

// Step 5 fallback: extract profile/level/tier from sequence header.
void av2_sbe_extract_seq_header_params(SubBitstreamExtractionState *sbe,
                                       int xlayer_id, int seq_profile_idc,
                                       int seq_max_level_idx, int seq_tier,
                                       int seq_max_mlayer_cnt) {
  if (xlayer_id < 0 || xlayer_id >= MAX_NUM_XLAYERS) return;
  if (!sbe->xlayer_is_selected[xlayer_id]) return;

  // Only fill in if not already set by OPS
  if (sbe->profile_idc[xlayer_id] == ANNEX_F_INVALID) {
    sbe->profile_idc[xlayer_id] = seq_profile_idc;
  }
  if (sbe->level_idc[xlayer_id] == ANNEX_F_INVALID) {
    sbe->level_idc[xlayer_id] = seq_max_level_idx;
  }
  if (sbe->tier_idc[xlayer_id] == ANNEX_F_INVALID) {
    sbe->tier_idc[xlayer_id] = seq_tier;
  }
  if (sbe->mlayer_cnt[xlayer_id] == ANNEX_F_INVALID) {
    sbe->mlayer_cnt[xlayer_id] = seq_max_mlayer_cnt;
  }
}
