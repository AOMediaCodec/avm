/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * Unified SMS partition pre-screener — public API.
 */

#ifndef AV2_ENCODER_PARTITION_SMS_H_
#define AV2_ENCODER_PARTITION_SMS_H_

#include "av2/encoder/encoder.h"
#include "av2/encoder/encodeframe_utils.h"
#include "av2/encoder/partition_strategy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of AVM partition types (NONE..SPLIT). */
#define SMS_UNIFIED_N_CLASSES 10

/* Run MLP inference for the block at (mi_row, mi_col) with the given bsize.
 * Stores per-class softmax probabilities in sms_tree->sms_unified_probs[],
 * indexed by PARTITION_TYPE enum value. Must be called before any pruning
 * gates that consume sms_unified_probs. */
void av2_sms_unified_compute(AV2_COMP *const cpi, MACROBLOCK *x,
                             SIMPLE_MOTION_DATA_TREE *sms_tree, int mi_row,
                             int mi_col, BLOCK_SIZE bsize);

/* Prune HORZ partition using the probabilities computed by
 * av2_sms_unified_compute. Sets part_search_state->prune_horz if the HORZ
 * probability is below the per-bsize threshold. */
void av2_sms_unified_prune_rect(const AV2_COMP *cpi,
                                SIMPLE_MOTION_DATA_TREE *sms_tree,
                                PartitionSearchState *part_search_state);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV2_ENCODER_PARTITION_SMS_H_
