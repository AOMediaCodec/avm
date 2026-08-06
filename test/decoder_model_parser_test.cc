/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c/. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <cstring>
#include <thread>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "avm_dsp/bitreader.h"
#include "avm_dsp/bitwriter.h"
#include "avm_mem/avm_mem.h"
#include "av2/decoder/annexF.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decoder_model.h"

namespace {

class DecoderModelParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pbi_ = static_cast<AV2Decoder *>(avm_memalign(32, sizeof(*pbi_)));
    ASSERT_NE(pbi_, nullptr);
    memset(pbi_, 0, sizeof(*pbi_));
    av2_decoder_model_verifier_init(pbi_);
    ASSERT_NE(pbi_->decoder_model_verifier, nullptr);
  }

  void TearDown() override {
    av2_decoder_model_verifier_destroy(pbi_);
    avm_free(pbi_);
  }

  void AddWholeXlayerContext(int xlayer_id, int sequence_header_id) {
    pbi_->seq_list[xlayer_id][sequence_header_id].seq_header_id =
        sequence_header_id;
    av2_decoder_model_verifier_on_sequence_header(pbi_, xlayer_id,
                                                  sequence_header_id);
  }

  AV2Decoder *pbi_ = nullptr;
};

TEST_F(DecoderModelParserTest, LifecycleStartsWithAvailableEmptyState) {
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_TRUE(stats.available);
  EXPECT_FALSE(stats.failed);
  EXPECT_EQ(stats.raw_obus, 0u);
  EXPECT_EQ(stats.contexts, 0u);
}

TEST_F(DecoderModelParserTest, ReplaysPrefixAndClosesCompleteDfg) {
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        16);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_SEQUENCE_HEADER, 0, 0, 0, 80);
  AddWholeXlayerContext(0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 0, 0,
                                        120);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_METADATA_SHORT, 0, 0, 0, 40);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 256u);

  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 0u);
  EXPECT_EQ(context.last_closed_dfg_bits, 256u);
  EXPECT_EQ(context.closed_dfgs, 1u);
}

TEST_F(DecoderModelParserTest, ShowExistingDoesNotConsumePendingDfgBits) {
  AddWholeXlayerContext(0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_SEQUENCE_HEADER, 0, 0, 0, 24);
  pbi_->common.show_existing_frame = 1;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 24u);
  EXPECT_EQ(context.closed_dfgs, 0u);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_METADATA_SHORT, 0, 0, 0, 8);
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.last_closed_dfg_bits, 32u);
  EXPECT_EQ(context.closed_dfgs, 1u);
}

TEST_F(DecoderModelParserTest, OperatingPointUsesAnnexFMembership) {
  av2_decoder_model_verifier_record_obu(pbi_, OBU_OPERATING_POINT_SET, 0, 0, 0,
                                        32);
  OperatingPointSet *const ops = &pbi_->ops_list[0][3];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 3;
  ops->ops_cnt = 1;
  ops->ops_mlayer_info_idc = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1 << 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][1] = 1 << 2;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 3);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 1, 2,
                                        100);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 0, 1,
                                        200);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_SEQUENCE_HEADER, 0, 0, 0, 16);
  av2_decoder_model_verifier_record_obu(
      pbi_, OBU_MULTI_STREAM_DECODER_OPERATION, GLOBAL_XLAYER_ID, 0, 0, 24);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_FALSE(context.scope.whole_xlayer);
  EXPECT_EQ(context.scope.ops_xlayer_id, 0);
  EXPECT_EQ(context.scope.ops_id, 3);
  // OPS prefix + selected frame + preserved sequence/global structural OBUs.
  EXPECT_EQ(context.pending_dfg_bits, 172u);
}

TEST_F(DecoderModelParserTest, RasSeedsAreFilteredPerOperatingPoint) {
  SequenceHeader *const sequence = &pbi_->seq_list[0][0];
  sequence->seq_header_id = 0;
  sequence->seq_max_level_idx = SEQ_LEVEL_2_0;
  sequence->seq_profile_idc = MAIN_420_10_IP0;
  sequence->ref_frames = 8;
  sequence->max_frame_width = 64;
  sequence->max_frame_height = 64;
  sequence->seq_max_mlayer_cnt = 2;
  sequence->still_picture = 1;
  pbi_->common.seq_params = *sequence;
  ContentInterpretation *const ci = &pbi_->common.ci_params_per_layer[0];
  ci->ci_timing_info_present_flag = 1;
  ci->timing_info.num_units_in_display_tick = 1;
  ci->timing_info.time_scale = 30;
  ci->timing_info.equal_elemental_interval = 1;
  ci->timing_info.num_ticks_per_elemental_duration = 1;
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);

  OperatingPointSet *const ops = &pbi_->ops_list[0][3];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 3;
  ops->ops_cnt = 2;
  ops->ops_mlayer_info_idc = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  ops->op[1].mlayer_info.ops_mlayer_map[0] = 3;
  ops->op[1].mlayer_info.ops_tlayer_map[0][0] = 1;
  ops->op[1].mlayer_info.ops_tlayer_map[0][1] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 3);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  RefCntBuffer long_term;
  RefCntBuffer untracked_long_term;
  RefCntBuffer ras_frame;
  memset(&long_term, 0, sizeof(long_term));
  memset(&untracked_long_term, 0, sizeof(untracked_long_term));
  memset(&ras_frame, 0, sizeof(ras_frame));
  long_term.xlayer_id = 0;
  long_term.mlayer_id = 1;
  long_term.tlayer_id = 0;
  long_term.long_term_id = 7;
  long_term.width = 64;
  long_term.height = 64;
  untracked_long_term.xlayer_id = 0;
  untracked_long_term.mlayer_id = 1;
  untracked_long_term.tlayer_id = 0;
  untracked_long_term.long_term_id = 8;

  const auto snapshot_frame = [this](int obu_type, int mlayer_id,
                                     RefCntBuffer *frame) {
    av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, mlayer_id,
                                                          0);
    av2_decoder_model_verifier_record_obu(pbi_, obu_type, 0, mlayer_id, 0, 800);
    AV2_COMMON *const cm = &pbi_->common;
    pbi_->obu_type = static_cast<OBU_TYPE>(obu_type);
    cm->xlayer_id = 0;
    cm->mlayer_id = mlayer_id;
    cm->tlayer_id = 0;
    cm->show_existing_frame = 0;
    cm->cur_frame = frame;
    cm->width = 64;
    cm->height = 64;
    cm->mi_params.mi_cols = 16;
    cm->mi_params.mi_rows = 16;
    cm->mib_size_log2 = 0;
    cm->tiles.cols = 1;
    cm->tiles.rows = 1;
    cm->tiles.col_start_sb[0] = 0;
    cm->tiles.col_start_sb[1] = 16;
    cm->tiles.row_start_sb[0] = 0;
    cm->tiles.row_start_sb[1] = 16;
    cm->current_frame.frame_type = KEY_FRAME;
    frame->xlayer_id = 0;
    frame->mlayer_id = mlayer_id;
    frame->tlayer_id = 0;
    frame->width = 64;
    frame->height = 64;
    av2_decoder_model_verifier_on_frame_wrapup_start(pbi_);
    av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  };

  snapshot_frame(OBU_REGULAR_TILE_GROUP, 1, &long_term);
  pbi_->common.ref_frame_map[0] = &long_term;
  pbi_->common.ref_frame_map[1] = &untracked_long_term;
  pbi_->valid_for_referencing[0] = 1;
  pbi_->valid_for_referencing[1] = 1;
  snapshot_frame(OBU_RAS_FRAME, 0, &ras_frame);

  bool found_excluding_op = false;
  bool found_including_op = false;
  Av2DmVerifierStats verifier_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &verifier_stats));
  for (uint32_t i = 0; i < verifier_stats.contexts; ++i) {
    Av2DmContextStats context;
    ASSERT_TRUE(
        av2_decoder_model_verifier_get_context_stats(pbi_, i, &context));
    if (context.scope.ops_id != 3) continue;
    if (context.scope.operating_point == 0) {
      found_excluding_op = true;
      EXPECT_TRUE(context.last_ras_seed_complete);
      EXPECT_EQ(context.last_ras_seed_count, 0u);
    } else if (context.scope.operating_point == 1) {
      found_including_op = true;
      EXPECT_FALSE(context.last_ras_seed_complete);
      EXPECT_EQ(context.last_ras_seed_count, 1u);
    }
  }
  EXPECT_TRUE(found_excluding_op);
  EXPECT_TRUE(found_including_op);
}

TEST_F(DecoderModelParserTest, GlobalOperatingPointCreatesPerXlayerContexts) {
  OperatingPointSet *const ops = &pbi_->ops_list[GLOBAL_XLAYER_ID][2];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = GLOBAL_XLAYER_ID;
  ops->ops_id = 2;
  ops->ops_cnt = 1;
  ops->ops_mlayer_info_idc = 1;
  ops->op[0].ops_xlayer_map = (1 << 1) | (1 << 3);
  ops->op[0].mlayer_info.ops_mlayer_map[1] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[1][0] = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[3] = 1 << 2;
  ops->op[0].mlayer_info.ops_tlayer_map[3][2] = 1 << 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, GLOBAL_XLAYER_ID, 2);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  ASSERT_EQ(stats.contexts, 2u);
  Av2DmContextStats first;
  Av2DmContextStats second;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &first));
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 1, &second));
  EXPECT_EQ(first.scope.xlayer_id, 1);
  EXPECT_EQ(first.scope.ops_xlayer_id, GLOBAL_XLAYER_ID);
  EXPECT_EQ(second.scope.xlayer_id, 3);
  EXPECT_EQ(second.scope.ops_xlayer_id, GLOBAL_XLAYER_ID);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 1, 0, 0,
                                        40);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 3, 2, 1,
                                        80);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &first));
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 1, &second));
  EXPECT_EQ(first.pending_dfg_bits, 40u);
  EXPECT_EQ(second.pending_dfg_bits, 80u);
}

TEST_F(DecoderModelParserTest, UnselectedFrameDoesNotCloseOperatingPointDfg) {
  OperatingPointSet *const ops = &pbi_->ops_list[0][1];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 1;
  ops->ops_cnt = 1;
  ops->ops_mlayer_info_idc = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1 << 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][1] = 1 << 2;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_SEQUENCE_HEADER, 0, 0, 0, 24);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 0, 0,
                                        40);
  pbi_->common.xlayer_id = 0;
  pbi_->common.mlayer_id = 0;
  pbi_->common.tlayer_id = 0;
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 24u);
  EXPECT_EQ(context.closed_dfgs, 0u);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 1, 2,
                                        80);
  pbi_->common.mlayer_id = 1;
  pbi_->common.tlayer_id = 2;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.last_closed_dfg_bits, 104u);
  EXPECT_EQ(context.closed_dfgs, 1u);
}

TEST_F(DecoderModelParserTest, OtherXlayerDoesNotCloseWholeXlayerDfg) {
  AddWholeXlayerContext(2, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_SEQUENCE_HEADER, 2, 0, 0, 16);
  pbi_->common.xlayer_id = 4;
  pbi_->common.mlayer_id = 0;
  pbi_->common.tlayer_id = 0;
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 16u);
  EXPECT_EQ(context.closed_dfgs, 0u);
}

TEST_F(DecoderModelParserTest, RedefinedOpsDoesNotRewriteOldDfgMembership) {
  OperatingPointSet *const ops = &pbi_->ops_list[0][4];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 4;
  ops->ops_cnt = 1;
  ops->ops_mlayer_info_idc = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 4);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 1, 0,
                                        40);
  pbi_->common.xlayer_id = 0;
  pbi_->common.mlayer_id = 1;
  pbi_->common.tlayer_id = 0;
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_OPERATING_POINT_SET, 0, 0, 0,
                                        20);
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1 << 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][1] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 4);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 20u);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 1, 0,
                                        80);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.last_closed_dfg_bits, 100u);
  EXPECT_EQ(context.closed_dfgs, 1u);
}

TEST_F(DecoderModelParserTest, OpsResetDeactivatesPriorScope) {
  OperatingPointSet *const ops = &pbi_->ops_list[0][5];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 5;
  ops->ops_cnt = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 5);

  ops->ops_cnt = 0;
  ops->ops_reset_flag = 0;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 5);
  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_FALSE(context.active);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 0, 0,
                                        64);
  pbi_->common.xlayer_id = 0;
  pbi_->common.mlayer_id = 0;
  pbi_->common.tlayer_id = 0;
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 0u);
  EXPECT_EQ(context.closed_dfgs, 0u);
}

TEST_F(DecoderModelParserTest, ActiveConfigurationUsesActivatedSequence) {
  pbi_->seq_list[0][1].seq_header_id = 1;
  pbi_->seq_list[0][2].seq_header_id = 2;
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 1);
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 2);
  pbi_->common.seq_params = pbi_->seq_list[0][1];
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 1);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_TRUE(context.active_configuration_present);
  EXPECT_EQ(context.active_sequence_header_id, 1);

  OperatingPointSet *const ops = &pbi_->ops_list[0][6];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 6;
  ops->ops_cnt = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 6);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 1, &context));
  EXPECT_TRUE(context.active_configuration_present);
  EXPECT_EQ(context.active_sequence_header_id, 1);
}

TEST_F(DecoderModelParserTest, LaterXlayerKeepsCurrentTuGlobalPrefix) {
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_record_obu(
      pbi_, OBU_MULTI_STREAM_DECODER_OPERATION, GLOBAL_XLAYER_ID, 0, 0, 24);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 0, 0,
                                        40);
  pbi_->common.xlayer_id = 0;
  pbi_->common.mlayer_id = 0;
  pbi_->common.tlayer_id = 0;
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_SEQUENCE_HEADER, 1, 0, 0, 16);
  AddWholeXlayerContext(1, 0);
  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 48u);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 1, 0, 0,
                                        80);
  pbi_->common.xlayer_id = 1;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.last_closed_dfg_bits, 128u);
}

TEST_F(DecoderModelParserTest, RedundantSequencePreservesOpenSefDfg) {
  AddWholeXlayerContext(0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_SEF, 0, 0, 0, 16);
  pbi_->common.xlayer_id = 0;
  pbi_->common.mlayer_id = 0;
  pbi_->common.tlayer_id = 0;
  pbi_->common.show_existing_frame = 1;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_SEQUENCE_HEADER, 0, 0, 0, 24);
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 0, 0,
                                        40);
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.last_closed_dfg_bits, 96u);
}

TEST_F(DecoderModelParserTest, RedundantOpsPreservesOpenSefDfg) {
  OperatingPointSet *const ops = &pbi_->ops_list[0][4];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 4;
  ops->ops_cnt = 1;
  ops->ops_mlayer_info_idc = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 4);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_SEF, 0, 0, 0, 16);
  pbi_->common.xlayer_id = 0;
  pbi_->common.mlayer_id = 0;
  pbi_->common.tlayer_id = 0;
  pbi_->common.show_existing_frame = 1;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_OPERATING_POINT_SET, 0, 0, 0,
                                        20);
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 4);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_TILE_GROUP, 0, 0, 0,
                                        40);
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.last_closed_dfg_bits, 92u);
}

TEST_F(DecoderModelParserTest, StreamBoundaryRelinksIdenticalConfiguration) {
  AddWholeXlayerContext(0, 0);
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  Av2DmVerifierStats before;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &before));
  av2_decoder_model_verifier_on_stream_configuration_change(pbi_, false);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  Av2DmVerifierStats after;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &after));
  EXPECT_EQ(after.event_count, before.event_count + 2);
  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_TRUE(context.active);
  EXPECT_TRUE(context.active_configuration_present);
}

TEST_F(DecoderModelParserTest, StreamBoundaryKeepsNewTemporalUnitPrefix) {
  AddWholeXlayerContext(0, 0);
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER,
                                        GLOBAL_XLAYER_ID, 0, 0, 8);
  av2_decoder_model_verifier_record_obu(
      pbi_, OBU_MULTI_STREAM_DECODER_OPERATION, GLOBAL_XLAYER_ID, 0, 0, 80);

  av2_decoder_model_verifier_on_stream_configuration_change(pbi_, true);
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 0, 0, 0,
                                        800);
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.last_closed_dfg_bits, 888u);
}

TEST_F(DecoderModelParserTest, StreamBoundaryDropsStaleTemporalUnitPrefix) {
  AddWholeXlayerContext(0, 0);
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_SEF, 0, 0, 0, 80);

  av2_decoder_model_verifier_on_stream_configuration_change(pbi_, false);
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 0, 0, 0,
                                        800);
  pbi_->common.show_existing_frame = 0;
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.last_closed_dfg_bits, 800u);
}

TEST_F(DecoderModelParserTest, ReactivationDoesNotReusePriorConfiguration) {
  AddWholeXlayerContext(0, 0);
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  av2_decoder_model_verifier_on_stream_configuration_change(pbi_, false);

  pbi_->seq_list[0][1].seq_header_id = 1;
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 1);
  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_TRUE(context.active);
  EXPECT_FALSE(context.active_configuration_present);
  EXPECT_EQ(context.active_sequence_header_id, -1);
}

TEST_F(DecoderModelParserTest, GlobalResetDeactivatesLocalAndGlobalOps) {
  OperatingPointSet *const local = &pbi_->ops_list[1][0];
  memset(local, 0, sizeof(*local));
  local->valid = 1;
  local->obu_xlayer_id = 1;
  local->ops_id = 0;
  local->ops_cnt = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 1, 0);
  OperatingPointSet *const global = &pbi_->ops_list[GLOBAL_XLAYER_ID][0];
  memset(global, 0, sizeof(*global));
  global->valid = 1;
  global->obu_xlayer_id = GLOBAL_XLAYER_ID;
  global->ops_id = 0;
  global->ops_cnt = 1;
  global->op[0].ops_xlayer_map = 1 << 2;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, GLOBAL_XLAYER_ID, 0);

  global->ops_reset_flag = 1;
  global->ops_cnt = 0;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, GLOBAL_XLAYER_ID, 0);
  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_FALSE(context.active);
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 1, &context));
  EXPECT_FALSE(context.active);
}

TEST_F(DecoderModelParserTest, FilteredRapDoesNotSuppressOtherXlayerRap) {
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 1, 0, 0, 40);
  av2_decoder_model_verifier_on_obu_filtered(pbi_);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 2, 0, 0, 40);
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.rap_starts, 2u);
}

TEST_F(DecoderModelParserTest, FilteredRapDoesNotSuppressNextSourceRap) {
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 1, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 1, 0, 0, 40);
  av2_decoder_model_verifier_on_obu_filtered(pbi_);
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 1, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 1, 0, 0, 40);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 1, 0, 0, 40);
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.rap_starts, 2u);
}

TEST_F(DecoderModelParserTest, TemporalPointRetainsFullUlebValueAndPresence) {
  constexpr uint64_t kPresentationTime = 0xfedcba98u;
  av2_decoder_model_verifier_on_temporal_point(pbi_, kPresentationTime);
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_TRUE(stats.temporal_point_present);
  EXPECT_EQ(stats.temporal_point, kPresentationTime);
  EXPECT_EQ(stats.temporal_points, 1u);
}

TEST_F(DecoderModelParserTest, ConfigurationBoundaryIsImmutableEvent) {
  av2_decoder_model_verifier_on_stream_configuration_change(pbi_, false);
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.event_count, 1u);
}

TEST(DecoderModelAnnexFTest, WholeXlayerAndStructuralMembership) {
  SubBitstreamExtractionState scope;
  ASSERT_TRUE(av2_sbe_configure_decoder_model_scope(&scope, 2, nullptr, -1, 1));
  EXPECT_TRUE(
      av2_sbe_should_retain_obu(&scope, OBU_REGULAR_TILE_GROUP, 2, 7, 3));
  EXPECT_FALSE(
      av2_sbe_should_retain_obu(&scope, OBU_REGULAR_TILE_GROUP, 3, 0, 0));
  EXPECT_TRUE(
      av2_sbe_should_retain_obu(&scope, OBU_TEMPORAL_DELIMITER, 3, 0, 0));
  EXPECT_TRUE(av2_sbe_should_retain_obu(
      &scope, OBU_MULTI_STREAM_DECODER_OPERATION, GLOBAL_XLAYER_ID, 0, 0));
}

TEST(DecoderModelAnnexFTest, OperatingPointPreservesBaseConfiguration) {
  OperatingPointSet ops;
  memset(&ops, 0, sizeof(ops));
  ops.valid = 1;
  ops.obu_xlayer_id = 4;
  ops.ops_cnt = 1;
  ops.ops_mlayer_info_idc = 1;
  ops.op[0].mlayer_info.ops_mlayer_map[4] = 1 << 1;
  ops.op[0].mlayer_info.ops_tlayer_map[4][1] = 1 << 2;

  SubBitstreamExtractionState scope;
  ASSERT_TRUE(av2_sbe_configure_decoder_model_scope(&scope, 4, &ops, 0, 0));
  EXPECT_TRUE(
      av2_sbe_should_retain_obu(&scope, OBU_REGULAR_TILE_GROUP, 4, 1, 2));
  EXPECT_FALSE(
      av2_sbe_should_retain_obu(&scope, OBU_REGULAR_TILE_GROUP, 4, 0, 1));
  EXPECT_TRUE(av2_sbe_should_retain_obu(&scope, OBU_SEQUENCE_HEADER, 4, 0, 0));
}

TEST(DecoderModelAnnexFTest, GlobalOperatingPointIsScopedPerXlayer) {
  OperatingPointSet ops;
  memset(&ops, 0, sizeof(ops));
  ops.valid = 1;
  ops.obu_xlayer_id = GLOBAL_XLAYER_ID;
  ops.ops_cnt = 1;
  ops.ops_mlayer_info_idc = 1;
  ops.op[0].ops_xlayer_map = (1 << 1) | (1 << 3);
  ops.op[0].mlayer_info.ops_mlayer_map[1] = 1;
  ops.op[0].mlayer_info.ops_tlayer_map[1][0] = 1;
  ops.op[0].mlayer_info.ops_mlayer_map[3] = 1 << 2;
  ops.op[0].mlayer_info.ops_tlayer_map[3][2] = 1 << 1;

  SubBitstreamExtractionState scope;
  ASSERT_TRUE(av2_sbe_configure_decoder_model_scope(&scope, 3, &ops, 0, 0));
  EXPECT_TRUE(
      av2_sbe_should_retain_obu(&scope, OBU_REGULAR_TILE_GROUP, 3, 2, 1));
  EXPECT_FALSE(
      av2_sbe_should_retain_obu(&scope, OBU_REGULAR_TILE_GROUP, 1, 0, 0));
}

#if CONFIG_AV2_ENCODER
static bool DecodeCountedSymbols() {
  uint8_t buffer[64] = { 0 };
  avm_cdf_prob write_cdf[3] = { AVM_CDF2(16384) };
  avm_writer writer;
  memset(&writer, 0, sizeof(writer));
  avm_start_encode(&writer, buffer);
  avm_write_literal(&writer, 21, 5);
  avm_write_symbol(&writer, 1, write_cdf, 2);
  avm_stop_encode(&writer);

  avm_cdf_prob read_cdf[3] = { AVM_CDF2(16384) };
  avm_reader reader;
  if (avm_reader_init(&reader, buffer, writer.pos) != 0) return false;
  reader.allow_update_cdf = 0;
  if (avm_read_literal(&reader, 5, {}) != 21) return false;
  if (avm_read_symbol(&reader, read_cdf, 2, {}) != 1) return false;
  return reader.frame_symbol_count == 6;
}

TEST(DecoderModelSymbolCountTest, MirrorsEncoderLiteralAndSymbolCount) {
  EXPECT_TRUE(DecodeCountedSymbols());
}

TEST(DecoderModelSymbolCountTest, DirectCdfAndBitAreNotFrameSymbols) {
  uint8_t buffer[64] = { 0 };
  avm_cdf_prob cdf[3] = { AVM_CDF2(16384) };
  avm_writer writer;
  memset(&writer, 0, sizeof(writer));
  avm_start_encode(&writer, buffer);
  avm_write_bit(&writer, 1);
  avm_write_cdf(&writer, 0, cdf, 2);
  avm_stop_encode(&writer);

  avm_reader reader;
  ASSERT_EQ(avm_reader_init(&reader, buffer, writer.pos), 0);
  EXPECT_EQ(avm_read_bit(&reader, {}), 1);
  EXPECT_EQ(avm_read_cdf(&reader, cdf, 2, {}), 0);
  EXPECT_EQ(reader.frame_symbol_count, 0u);
}

TEST(DecoderModelSymbolCountTest, IndependentReadersAreThreadLocal) {
  constexpr int kThreads = 8;
  constexpr int kIterations = 100;
  std::vector<int> results(kThreads, 0);
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([i, &results]() {
      bool result = true;
      for (int j = 0; j < kIterations; ++j) result &= DecodeCountedSymbols();
      results[i] = result ? 1 : 0;
    });
  }
  for (std::thread &thread : threads) thread.join();
  for (int result : results) EXPECT_EQ(result, 1);
}
#endif  // CONFIG_AV2_ENCODER

}  // namespace
