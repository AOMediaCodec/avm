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

#include <string.h>

#include <string>

#include "avm/avm_integer.h"
#include "av2/common/enums.h"
#include "common/tu_assembler.h"
#include "common/xlayer_config.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

// Helper: build a minimal MultiXLayerConfig for testing
static void MakeMinimalConfig(MultiXLayerConfig *cfg, int num_xlayers,
                              const int *xlayer_ids) {
  xlayer_config_init(cfg);
  cfg->num_xlayers = num_xlayers;
  for (int i = 0; i < num_xlayers; i++) {
    cfg->xlayers[i].xlayer_id = xlayer_ids[i];
    snprintf(cfg->xlayers[i].input_filename, PATH_MAX, "input_%d", i);
    cfg->xlayers[i].width = 1920;
    cfg->xlayers[i].height = 1080;
  }
  cfg->enable_global_lcr = 1;
  snprintf(cfg->output_filename, PATH_MAX, "test_out.obu");
}

// Helper: parse an OBU at the given offset, return header info.
// Returns the total consumed bytes (length_field + obu_total_size).
static int ParseObuAt(const uint8_t *buf, size_t buf_size, size_t offset,
                      int *out_type, int *out_ext_flag, int *out_xlayer_id,
                      size_t *out_payload_size) {
  if (offset >= buf_size) return -1;

  uint64_t obu_total_size = 0;
  size_t length_field_size = 0;
  if (avm_uleb_decode(buf + offset, buf_size - offset, &obu_total_size,
                      &length_field_size) != 0) {
    return -1;
  }

  if (obu_total_size == 0) return -1;

  const uint8_t *hdr = buf + offset + length_field_size;
  *out_ext_flag = (hdr[0] >> 7) & 1;
  *out_type = (hdr[0] >> 2) & 0x1F;

  int hdr_size = 1;
  *out_xlayer_id = 0;
  if (*out_ext_flag) {
    *out_xlayer_id = hdr[1] & 0x1F;
    hdr_size = 2;
  }

  *out_payload_size = (size_t)obu_total_size - hdr_size;

  return (int)(length_field_size + (size_t)obu_total_size);
}

// --- Init / Free Tests ---

TEST(TUAssembler, InitAndFree) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  EXPECT_NE(ta.buffer, nullptr);
  EXPECT_EQ(ta.size, 0u);
  EXPECT_GE(ta.capacity, (size_t)TU_ASM_INITIAL_CAPACITY);
  EXPECT_EQ(ta.num_xlayers, 2);
  EXPECT_EQ(ta.xlayer_ids[0], 0);
  EXPECT_EQ(ta.xlayer_ids[1], 1);

  tu_assembler_free(&ta);
  EXPECT_EQ(ta.buffer, nullptr);
  EXPECT_EQ(ta.size, 0u);
}

// --- TD Write Test ---

TEST(TUAssembler, WriteTD) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  ASSERT_EQ(tu_assembler_write_td(&ta), 0);

  // TD should be 2 bytes: [size=1][header_byte=0x08]
  ASSERT_EQ(ta.size, 2u);
  EXPECT_EQ(ta.buffer[0], 1);     // ULEB128 size = 1
  EXPECT_EQ(ta.buffer[1], 0x08);  // OBU_TEMPORAL_DELIMITER << 2

  // Parse it back
  int type, ext_flag, xlayer_id;
  size_t payload_size;
  int consumed = ParseObuAt(ta.buffer, ta.size, 0, &type, &ext_flag, &xlayer_id,
                            &payload_size);
  EXPECT_EQ(consumed, 2);
  EXPECT_EQ(type, OBU_TEMPORAL_DELIMITER);
  EXPECT_EQ(ext_flag, 0);
  EXPECT_EQ(payload_size, 0u);

  tu_assembler_free(&ta);
}

// --- OBU Header Rewriting Test ---

TEST(TUAssembler, AppendXLayerObusRewritesHeaders) {
  int ids[] = { 0, 3 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);

  // Construct a fake per-xlayer OBU: a Sequence Header with no extension.
  // Format: [uleb128 size][header_byte][payload...]
  // OBU_SEQUENCE_HEADER = 1, header byte: ext=0, type=1, tlayer=0 => (1<<2)=4
  const uint8_t fake_payload[] = { 0xAA, 0xBB, 0xCC };
  uint8_t input_obu[16];
  size_t input_size = 0;

  // ULEB128 size = 1 (header) + 3 (payload) = 4
  uint8_t size_buf[4];
  size_t size_len = 0;
  avm_uleb_encode(4, sizeof(size_buf), size_buf, &size_len);
  memcpy(input_obu + input_size, size_buf, size_len);
  input_size += size_len;

  // Header byte: ext=0, type=OBU_SEQUENCE_HEADER(1), tlayer=0
  input_obu[input_size++] = (uint8_t)((1 << 2));  // 0x04

  // Payload
  memcpy(input_obu + input_size, fake_payload, sizeof(fake_payload));
  input_size += sizeof(fake_payload);

  // Append with xlayer_id = 3
  ASSERT_EQ(tu_assembler_append_xlayer_obus(&ta, 3, input_obu, input_size), 0);

  // Parse the rewritten OBU
  int type, ext_flag, xlayer_id;
  size_t payload_size;
  int consumed = ParseObuAt(ta.buffer, ta.size, 0, &type, &ext_flag, &xlayer_id,
                            &payload_size);
  ASSERT_GT(consumed, 0);
  EXPECT_EQ(type, OBU_SEQUENCE_HEADER);
  EXPECT_EQ(ext_flag, 1);       // Should now have extension
  EXPECT_EQ(xlayer_id, 3);      // Rewritten xlayer_id
  EXPECT_EQ(payload_size, 3u);  // Original payload preserved

  // Verify payload content
  size_t hdr_offset = 0;
  uint64_t obu_total = 0;
  size_t lfs = 0;
  avm_uleb_decode(ta.buffer, ta.size, &obu_total, &lfs);
  hdr_offset = lfs + 2;  // Skip size field and 2-byte header
  EXPECT_EQ(ta.buffer[hdr_offset], 0xAA);
  EXPECT_EQ(ta.buffer[hdr_offset + 1], 0xBB);
  EXPECT_EQ(ta.buffer[hdr_offset + 2], 0xCC);

  tu_assembler_free(&ta);
}

TEST(TUAssembler, AppendXLayerObusSkipsTD) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);

  // Construct a TD OBU (should be skipped by append)
  // OBU_TEMPORAL_DELIMITER = 2, header: ext=0, type=2, tlayer=0 => 0x08
  uint8_t td_obu[] = { 1, 0x08 };  // size=1, header

  ASSERT_EQ(tu_assembler_append_xlayer_obus(&ta, 0, td_obu, sizeof(td_obu)), 0);

  // No output — TD should be filtered
  EXPECT_EQ(ta.size, 0u);

  tu_assembler_free(&ta);
}

TEST(TUAssembler, AppendXLayerObusPreservesExtension) {
  int ids[] = { 0, 2 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);

  // Construct an OBU that already has an extension byte
  // OBU_MULTI_FRAME_HEADER = 3
  // Header: ext=1, type=3, tlayer=1 => (1<<7)|(3<<2)|1 = 0x8D
  // Extension: mlayer=2, xlayer=0 => (2<<5)|0 = 0x40
  const uint8_t payload[] = { 0xDE, 0xAD };
  uint8_t input_obu[16];
  size_t input_size = 0;

  // ULEB128 size = 2 (header+ext) + 2 (payload) = 4
  uint8_t size_buf[4];
  size_t size_len = 0;
  avm_uleb_encode(4, sizeof(size_buf), size_buf, &size_len);
  memcpy(input_obu + input_size, size_buf, size_len);
  input_size += size_len;

  input_obu[input_size++] = 0x8D;  // Header: ext=1, type=3, tlayer=1
  input_obu[input_size++] = 0x40;  // Extension: mlayer=2, xlayer=0
  memcpy(input_obu + input_size, payload, sizeof(payload));
  input_size += sizeof(payload);

  // Append with xlayer_id = 2
  ASSERT_EQ(tu_assembler_append_xlayer_obus(&ta, 2, input_obu, input_size), 0);

  // Parse rewritten OBU
  int type, ext_flag, xlayer_id;
  size_t payload_size;
  int consumed = ParseObuAt(ta.buffer, ta.size, 0, &type, &ext_flag, &xlayer_id,
                            &payload_size);
  ASSERT_GT(consumed, 0);
  EXPECT_EQ(type, OBU_MULTI_FRAME_HEADER);
  EXPECT_EQ(ext_flag, 1);
  EXPECT_EQ(xlayer_id, 2);      // Rewritten to target xlayer
  EXPECT_EQ(payload_size, 2u);  // Payload size unchanged

  // Verify mlayer_id is preserved in extension byte
  uint64_t obu_total = 0;
  size_t lfs = 0;
  avm_uleb_decode(ta.buffer, ta.size, &obu_total, &lfs);
  uint8_t ext_byte = ta.buffer[lfs + 1];
  int mlayer_id = (ext_byte >> 5) & 0x7;
  EXPECT_EQ(mlayer_id, 2);  // mlayer preserved

  tu_assembler_free(&ta);
}

// --- Multiple OBU Append Test ---

TEST(TUAssembler, AppendMultipleObus) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);

  // Build a packet with: TD + SH + Frame data
  uint8_t packet[64];
  size_t pkt_size = 0;

  // OBU 1: TD (should be skipped)
  packet[pkt_size++] = 1;     // size=1
  packet[pkt_size++] = 0x08;  // TD header

  // OBU 2: Sequence Header (type=1), 2 bytes payload
  uint8_t sb[4];
  size_t sl = 0;
  avm_uleb_encode(3, sizeof(sb), sb, &sl);  // size = 1 hdr + 2 payload
  memcpy(packet + pkt_size, sb, sl);
  pkt_size += sl;
  packet[pkt_size++] = 0x04;  // SH header, no ext
  packet[pkt_size++] = 0x11;  // payload byte 1
  packet[pkt_size++] = 0x22;  // payload byte 2

  // OBU 3: Leading Tile Group (type=6), 3 bytes payload
  avm_uleb_encode(4, sizeof(sb), sb, &sl);  // size = 1 hdr + 3 payload
  memcpy(packet + pkt_size, sb, sl);
  pkt_size += sl;
  packet[pkt_size++] = (uint8_t)(6 << 2);  // Leading TG header, no ext
  packet[pkt_size++] = 0x33;
  packet[pkt_size++] = 0x44;
  packet[pkt_size++] = 0x55;

  ASSERT_EQ(tu_assembler_append_xlayer_obus(&ta, 1, packet, pkt_size), 0);

  // Should have 2 OBUs output (TD skipped)
  size_t offset = 0;
  int type, ext_flag, xlayer_id;
  size_t payload_size;

  // First OBU: Sequence Header
  int consumed = ParseObuAt(ta.buffer, ta.size, offset, &type, &ext_flag,
                            &xlayer_id, &payload_size);
  ASSERT_GT(consumed, 0);
  EXPECT_EQ(type, OBU_SEQUENCE_HEADER);
  EXPECT_EQ(xlayer_id, 1);
  EXPECT_EQ(payload_size, 2u);
  offset += consumed;

  // Second OBU: Leading Tile Group
  consumed = ParseObuAt(ta.buffer, ta.size, offset, &type, &ext_flag,
                        &xlayer_id, &payload_size);
  ASSERT_GT(consumed, 0);
  EXPECT_EQ(type, OBU_LEADING_TILE_GROUP);
  EXPECT_EQ(xlayer_id, 1);
  EXPECT_EQ(payload_size, 3u);
  offset += consumed;

  // Should have consumed all output
  EXPECT_EQ(offset, ta.size);

  tu_assembler_free(&ta);
}

// --- Flush Test ---

TEST(TUAssembler, FlushWritesToFile) {
  int ids[] = { 0 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 1, ids);

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  ASSERT_EQ(tu_assembler_write_td(&ta), 0);

  std::string path_str = testing::TempDir() + "tu_asm_flush_test.obu";
  const char *path = path_str.c_str();
  FILE *f = fopen(path, "wb");
  ASSERT_NE(f, nullptr);

  size_t pre_flush_size = ta.size;
  ASSERT_EQ(tu_assembler_flush(&ta, f), 0);
  fclose(f);

  // Buffer should be reset
  EXPECT_EQ(ta.size, 0u);

  // Verify file contents
  f = fopen(path, "rb");
  ASSERT_NE(f, nullptr);
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fclose(f);
  EXPECT_EQ((size_t)file_size, pre_flush_size);

  tu_assembler_free(&ta);
}

// --- Global LCR Population Test ---

TEST(TUAssembler, PopulateGlobalLcr) {
  int ids[] = { 0, 5 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);
  cfg.lcr_purpose_id = 2;
  cfg.lcr_dependent_xlayers_flag = 1;
  cfg.lcr_doh_constraint_flag = 1;
  cfg.xlayers[0].layer_type = TEXTURE_LAYER;
  cfg.xlayers[1].layer_type = AUX_LAYER;
  cfg.xlayers[1].auxiliary_type = LCR_DEPTH_AUX;

  GlobalLayerConfigurationRecord glcr;
  populate_global_lcr_from_config(&cfg, &glcr);

  EXPECT_EQ(glcr.LcrMaxNumXLayerCount, 2);
  EXPECT_EQ(glcr.LcrXLayerID[0], 0);
  EXPECT_EQ(glcr.LcrXLayerID[1], 5);

  // xlayer_map should have bits 0 and 5 set
  uint32_t expected_map = (1u << 0) | (1u << 5);
  EXPECT_EQ((uint32_t)glcr.lcr_xlayer_map, expected_map);

  EXPECT_EQ(glcr.lcr_global_purpose_id, 2);
  EXPECT_EQ(glcr.lcr_dependent_xlayers_flag, 1);
  EXPECT_EQ(glcr.lcr_doh_constraint_flag, 1);
  EXPECT_EQ(glcr.lcr_global_payload_present_flag, 1);

  // Per-xlayer info: xlayer 0
  EXPECT_EQ(glcr.xlayer_info[0].lcr_rep_info_present_flag, 1);
  EXPECT_EQ(glcr.xlayer_info[0].rep_params.lcr_max_pic_width, 1920);
  EXPECT_EQ(glcr.xlayer_info[0].rep_params.lcr_max_pic_height, 1080);

  // Per-xlayer info: xlayer 5 is at positional index 1
  EXPECT_EQ(glcr.xlayer_info[1].lcr_rep_info_present_flag, 1);
  EXPECT_EQ(glcr.xlayer_info[1].rep_params.lcr_max_pic_width, 1920);
  EXPECT_EQ(glcr.xlayer_info[1].rep_params.lcr_max_pic_height, 1080);

  // Embedded layer type info
  EXPECT_EQ(glcr.xlayer_info[0].mlayer_params.lcr_layer_type[0], TEXTURE_LAYER);
  EXPECT_EQ(glcr.xlayer_info[1].mlayer_params.lcr_layer_type[0], AUX_LAYER);
  EXPECT_EQ(glcr.xlayer_info[1].mlayer_params.lcr_auxiliary_type[0],
            LCR_DEPTH_AUX);
}

// --- OPS Population Test ---

TEST(TUAssembler, PopulateOps) {
  OPSConfig ops_cfg;
  memset(&ops_cfg, 0, sizeof(ops_cfg));
  ops_cfg.enable = 1;
  ops_cfg.ops_id = 0;
  ops_cfg.priority = 1;
  ops_cfg.intent_present_flag = 1;
  ops_cfg.ptl_present_flag = 1;
  ops_cfg.num_operating_points = 2;

  // OP0: xlayer 0 only
  ops_cfg.ops[0].intent = 0;
  ops_cfg.ops[0].xlayer_map = (1u << 0);

  // OP1: xlayers 0 and 3
  ops_cfg.ops[1].intent = 1;
  ops_cfg.ops[1].xlayer_map = (1u << 0) | (1u << 3);

  // Set up a minimal MultiXLayerConfig for derivation
  int ids[] = { 0, 3 };
  MultiXLayerConfig mcfg;
  MakeMinimalConfig(&mcfg, 2, ids);
  mcfg.xlayers[0].width = 960;
  mcfg.xlayers[0].height = 540;
  mcfg.xlayers[0].level = SEQ_LEVEL_4_0;
  mcfg.xlayers[1].width = 960;
  mcfg.xlayers[1].height = 540;
  mcfg.xlayers[1].level = SEQ_LEVEL_4_0;

  OperatingPointSet ops;
  populate_ops_from_config(&ops_cfg, GLOBAL_XLAYER_ID, &mcfg, &ops);

  EXPECT_EQ(ops.valid, 1);
  EXPECT_EQ(ops.ops_id, 0);
  EXPECT_EQ(ops.ops_cnt, 2);
  EXPECT_EQ(ops.ops_priority, 1);
  EXPECT_EQ(ops.ops_intent_present_flag, 1);

  // OP0: single xlayer
  EXPECT_EQ(ops.op[0].ops_intent_op, 0);
  EXPECT_EQ(ops.op[0].ops_xlayer_map, 1);
  EXPECT_EQ(ops.op[0].XCount, 1);
  EXPECT_EQ(ops.op[0].OpsxLayerID[0], 0);

  // OP1: two xlayers
  EXPECT_EQ(ops.op[1].ops_intent_op, 1);
  EXPECT_EQ(ops.op[1].ops_xlayer_map, (int)((1u << 0) | (1u << 3)));
  EXPECT_EQ(ops.op[1].XCount, 2);
  EXPECT_EQ(ops.op[1].OpsxLayerID[0], 0);
  EXPECT_EQ(ops.op[1].OpsxLayerID[1], 3);
}

// --- Global LCR OBU Write Test ---

TEST(TUAssembler, WriteGlobalLcrObu) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  ASSERT_EQ(tu_assembler_write_global_lcr(&ta), 0);

  // Should have produced some output
  EXPECT_GT(ta.size, 0u);

  // Parse the OBU header
  int type, ext_flag, xlayer_id;
  size_t payload_size;
  int consumed = ParseObuAt(ta.buffer, ta.size, 0, &type, &ext_flag, &xlayer_id,
                            &payload_size);
  ASSERT_GT(consumed, 0);
  EXPECT_EQ(type, OBU_LAYER_CONFIGURATION_RECORD);
  EXPECT_EQ(ext_flag, 1);
  EXPECT_EQ(xlayer_id, GLOBAL_XLAYER_ID);

  tu_assembler_free(&ta);
}

// --- MSDO OBU Write Test ---

TEST(TUAssembler, WriteMsdoObu) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);
  cfg.enable_msdo = 1;

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  ASSERT_EQ(tu_assembler_write_msdo(&ta), 0);

  EXPECT_GT(ta.size, 0u);

  int type, ext_flag, xlayer_id;
  size_t payload_size;
  int consumed = ParseObuAt(ta.buffer, ta.size, 0, &type, &ext_flag, &xlayer_id,
                            &payload_size);
  ASSERT_GT(consumed, 0);
  EXPECT_EQ(type, OBU_MULTI_STREAM_DECODER_OPERATION);
  EXPECT_EQ(ext_flag, 1);
  EXPECT_EQ(xlayer_id, GLOBAL_XLAYER_ID);

  tu_assembler_free(&ta);
}

TEST(TUAssembler, MsdoSkippedWhenDisabled) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);
  cfg.enable_msdo = 0;

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  ASSERT_EQ(tu_assembler_write_msdo(&ta), 0);

  // Should produce no output when disabled
  EXPECT_EQ(ta.size, 0u);

  tu_assembler_free(&ta);
}

// --- Full TU Assembly Test ---

TEST(TUAssembler, FullTuAssembly) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);
  cfg.enable_msdo = 1;

  // Add an OPS
  cfg.num_ops_sets = 1;
  cfg.ops_sets[0].enable = 1;
  cfg.ops_sets[0].ops_id = 0;
  cfg.ops_sets[0].intent_present_flag = 1;
  cfg.ops_sets[0].ptl_present_flag = 1;
  cfg.ops_sets[0].num_operating_points = 1;
  cfg.ops_sets[0].ops[0].xlayer_map = 0x3;

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);

  // Write TD
  ASSERT_EQ(tu_assembler_write_td(&ta), 0);

  // Write structural OBUs
  ASSERT_EQ(tu_assembler_write_global_lcr(&ta), 0);
  ASSERT_EQ(tu_assembler_write_msdo(&ta), 0);
  ASSERT_EQ(tu_assembler_write_ops(&ta, GLOBAL_XLAYER_ID), 0);

  // Fake per-xlayer data for xlayer 0
  uint8_t xl0_data[8];
  size_t xl0_size = 0;
  uint8_t sb[4];
  size_t sl = 0;
  avm_uleb_encode(3, sizeof(sb), sb, &sl);  // SH: 1 hdr + 2 payload
  memcpy(xl0_data + xl0_size, sb, sl);
  xl0_size += sl;
  xl0_data[xl0_size++] = 0x04;  // SH header
  xl0_data[xl0_size++] = 0xAA;
  xl0_data[xl0_size++] = 0xBB;

  ASSERT_EQ(tu_assembler_append_xlayer_obus(&ta, 0, xl0_data, xl0_size), 0);

  // Fake per-xlayer data for xlayer 1
  uint8_t xl1_data[8];
  size_t xl1_size = 0;
  avm_uleb_encode(3, sizeof(sb), sb, &sl);
  memcpy(xl1_data + xl1_size, sb, sl);
  xl1_size += sl;
  xl1_data[xl1_size++] = 0x04;
  xl1_data[xl1_size++] = 0xCC;
  xl1_data[xl1_size++] = 0xDD;

  ASSERT_EQ(tu_assembler_append_xlayer_obus(&ta, 1, xl1_data, xl1_size), 0);

  // Verify total output is non-empty and can be parsed
  EXPECT_GT(ta.size, 10u);

  // Walk through OBUs to verify ordering: TD, LCR, MSDO, OPS, xl0 SH, xl1 SH
  size_t offset = 0;
  int obu_count = 0;
  int types[16] = {};
  int xlayer_ids[16] = {};

  while (offset < ta.size && obu_count < 16) {
    int type, ext_flag, xlayer_id;
    size_t payload_size;
    int consumed = ParseObuAt(ta.buffer, ta.size, offset, &type, &ext_flag,
                              &xlayer_id, &payload_size);
    if (consumed <= 0) break;
    types[obu_count] = type;
    xlayer_ids[obu_count] = xlayer_id;
    obu_count++;
    offset += consumed;
  }

  // Should have at least 6 OBUs
  ASSERT_GE(obu_count, 6);

  // First OBU should be TD
  EXPECT_EQ(types[0], OBU_TEMPORAL_DELIMITER);

  // LCR should follow
  EXPECT_EQ(types[1], OBU_LAYER_CONFIGURATION_RECORD);
  EXPECT_EQ(xlayer_ids[1], GLOBAL_XLAYER_ID);

  // MSDO next
  EXPECT_EQ(types[2], OBU_MULTI_STREAM_DECODER_OPERATION);
  EXPECT_EQ(xlayer_ids[2], GLOBAL_XLAYER_ID);

  // OPS
  EXPECT_EQ(types[3], OBU_OPERATING_POINT_SET);
  EXPECT_EQ(xlayer_ids[3], GLOBAL_XLAYER_ID);

  // Per-xlayer OBUs: xlayer 0 then xlayer 1
  EXPECT_EQ(types[4], OBU_SEQUENCE_HEADER);
  EXPECT_EQ(xlayer_ids[4], 0);

  EXPECT_EQ(types[5], OBU_SEQUENCE_HEADER);
  EXPECT_EQ(xlayer_ids[5], 1);

  // Should have consumed all output
  EXPECT_EQ(offset, ta.size);

  tu_assembler_free(&ta);
}

// --- Atlas Population Tests ---

TEST(TUAssembler, PopulateAtlasEnhancedUniform) {
  int ids[] = { 0, 1, 2 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 3, ids);
  cfg.enable_atlas = 1;
  cfg.atlas_mode = ENHANCED_ATLAS;
  cfg.atlas_uniform_spacing = 1;
  // All xlayers same size
  for (int i = 0; i < 3; i++) {
    cfg.xlayers[i].width = 640;
    cfg.xlayers[i].height = 480;
  }

  AtlasSegmentInfo atlas;
  populate_atlas_from_config(&cfg, &atlas);

  EXPECT_EQ(atlas.valid, 1);
  EXPECT_EQ(atlas.atlas_segment_mode_idc, ENHANCED_ATLAS);
  EXPECT_EQ(atlas.atlas_segment_id, 1);

  // Region info: 3 columns x 1 row, uniform spacing
  EXPECT_EQ(atlas.ats_reg_params.ats_uniform_spacing_flag, 1);
  EXPECT_EQ(atlas.ats_reg_params.ats_num_region_columns_minus_1, 2);
  EXPECT_EQ(atlas.ats_reg_params.ats_num_region_rows_minus_1, 0);
  EXPECT_EQ(atlas.ats_reg_params.ats_region_width_minus_1, 639);
  EXPECT_EQ(atlas.ats_reg_params.ats_region_height_minus_1, 479);
  EXPECT_EQ(atlas.ats_reg_params.NumRegionsInAtlas, 3);

  // Segment mapping: single_region_per_segment
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_single_region_per_atlas_segment_flag, 1);
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_num_atlas_segments_minus_1, 2);
}

TEST(TUAssembler, PopulateAtlasEnhancedExplicit2x2) {
  // 3 regions in a 2x2 grid (bottom-right empty)
  int ids[] = { 0, 1, 2 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 3, ids);
  cfg.enable_atlas = 1;
  cfg.atlas_mode = ENHANCED_ATLAS;
  cfg.atlas_uniform_spacing = 0;

  cfg.xlayers[0].width = 960;
  cfg.xlayers[0].height = 540;
  cfg.xlayers[0].atlas_pos_x = 0;
  cfg.xlayers[0].atlas_pos_y = 0;

  cfg.xlayers[1].width = 960;
  cfg.xlayers[1].height = 540;
  cfg.xlayers[1].atlas_pos_x = 960;
  cfg.xlayers[1].atlas_pos_y = 0;

  cfg.xlayers[2].width = 960;
  cfg.xlayers[2].height = 540;
  cfg.xlayers[2].atlas_pos_x = 0;
  cfg.xlayers[2].atlas_pos_y = 540;

  AtlasSegmentInfo atlas;
  populate_atlas_from_config(&cfg, &atlas);

  EXPECT_EQ(atlas.valid, 1);
  EXPECT_EQ(atlas.atlas_segment_mode_idc, ENHANCED_ATLAS);

  // Grid should be 2 columns x 2 rows
  EXPECT_EQ(atlas.ats_reg_params.ats_uniform_spacing_flag, 0);
  EXPECT_EQ(atlas.ats_reg_params.ats_num_region_columns_minus_1, 1);
  EXPECT_EQ(atlas.ats_reg_params.ats_num_region_rows_minus_1, 1);
  EXPECT_EQ(atlas.ats_reg_params.ats_column_width_minus_1[0], 959);
  EXPECT_EQ(atlas.ats_reg_params.ats_column_width_minus_1[1], 959);
  EXPECT_EQ(atlas.ats_reg_params.ats_row_height_minus_1[0], 539);
  EXPECT_EQ(atlas.ats_reg_params.ats_row_height_minus_1[1], 539);
  EXPECT_EQ(atlas.ats_reg_params.NumRegionsInAtlas, 4);

  // Explicit segment mapping (not single_region_per_segment)
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_single_region_per_atlas_segment_flag, 0);
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_num_atlas_segments_minus_1, 2);

  // Segment 0 at col=0,row=0
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_top_left_region_column[0], 0);
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_top_left_region_row[0], 0);

  // Segment 1 at col=1,row=0
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_top_left_region_column[1], 1);
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_top_left_region_row[1], 0);

  // Segment 2 at col=0,row=1
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_top_left_region_column[2], 0);
  EXPECT_EQ(atlas.ats_reg_seg_map.ats_top_left_region_row[2], 1);
}

TEST(TUAssembler, PopulateAtlasMultistream) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);
  cfg.enable_atlas = 1;
  cfg.atlas_mode = MULTISTREAM_ATLAS;
  cfg.atlas_width = 1920;
  cfg.atlas_height = 1080;
  cfg.xlayers[0].width = 960;
  cfg.xlayers[0].height = 1080;
  cfg.xlayers[0].atlas_pos_x = 0;
  cfg.xlayers[0].atlas_pos_y = 0;
  cfg.xlayers[1].width = 960;
  cfg.xlayers[1].height = 1080;
  cfg.xlayers[1].atlas_pos_x = 960;
  cfg.xlayers[1].atlas_pos_y = 0;

  AtlasSegmentInfo atlas;
  populate_atlas_from_config(&cfg, &atlas);

  EXPECT_EQ(atlas.valid, 1);
  EXPECT_EQ(atlas.atlas_segment_mode_idc, MULTISTREAM_ATLAS);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_stream_id_present, 1);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_atlas_width, 1920);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_atlas_height, 1080);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_num_atlas_segments_minus_1, 1);

  // Segment 0: xlayer_id=0, pos (0,0), 960x1080
  EXPECT_EQ(atlas.ats_basic_info_s.ats_input_stream_id[0], 0);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_segment_top_left_pos_x[0], 0);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_segment_top_left_pos_y[0], 0);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_segment_width[0], 960);

  // Segment 1: xlayer_id=1, pos (960,0), 960x1080
  EXPECT_EQ(atlas.ats_basic_info_s.ats_input_stream_id[1], 1);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_segment_top_left_pos_x[1], 960);
  EXPECT_EQ(atlas.ats_basic_info_s.ats_segment_width[1], 960);
}

// --- Atlas OBU Write Tests ---

TEST(TUAssembler, WriteAtlasEnhancedObu) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);
  cfg.enable_atlas = 1;
  cfg.atlas_mode = ENHANCED_ATLAS;
  cfg.atlas_uniform_spacing = 1;
  cfg.xlayers[0].width = 960;
  cfg.xlayers[0].height = 540;
  cfg.xlayers[1].width = 960;
  cfg.xlayers[1].height = 540;

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  ASSERT_EQ(tu_assembler_write_atlas(&ta), 0);

  EXPECT_GT(ta.size, 0u);

  // Parse OBU header
  int type, ext_flag, xlayer_id;
  size_t payload_size;
  int consumed = ParseObuAt(ta.buffer, ta.size, 0, &type, &ext_flag, &xlayer_id,
                            &payload_size);
  ASSERT_GT(consumed, 0);
  EXPECT_EQ(type, OBU_ATLAS_SEGMENT);
  EXPECT_EQ(ext_flag, 1);
  EXPECT_EQ(xlayer_id, GLOBAL_XLAYER_ID);

  tu_assembler_free(&ta);
}

TEST(TUAssembler, WriteAtlasMultistreamObu) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);
  cfg.enable_atlas = 1;
  cfg.atlas_mode = MULTISTREAM_ATLAS;
  cfg.atlas_width = 1920;
  cfg.atlas_height = 1080;
  cfg.xlayers[0].width = 960;
  cfg.xlayers[0].height = 1080;
  cfg.xlayers[0].atlas_pos_x = 0;
  cfg.xlayers[0].atlas_pos_y = 0;
  cfg.xlayers[1].width = 960;
  cfg.xlayers[1].height = 1080;
  cfg.xlayers[1].atlas_pos_x = 960;
  cfg.xlayers[1].atlas_pos_y = 0;

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  ASSERT_EQ(tu_assembler_write_atlas(&ta), 0);

  EXPECT_GT(ta.size, 0u);

  int type, ext_flag, xlayer_id;
  size_t payload_size;
  int consumed = ParseObuAt(ta.buffer, ta.size, 0, &type, &ext_flag, &xlayer_id,
                            &payload_size);
  ASSERT_GT(consumed, 0);
  EXPECT_EQ(type, OBU_ATLAS_SEGMENT);
  EXPECT_EQ(ext_flag, 1);
  EXPECT_EQ(xlayer_id, GLOBAL_XLAYER_ID);

  tu_assembler_free(&ta);
}

TEST(TUAssembler, AtlasSkippedWhenDisabled) {
  int ids[] = { 0, 1 };
  MultiXLayerConfig cfg;
  MakeMinimalConfig(&cfg, 2, ids);
  cfg.enable_atlas = 0;

  TUAssembler ta;
  ASSERT_EQ(tu_assembler_init(&ta, &cfg), 0);
  ASSERT_EQ(tu_assembler_write_atlas(&ta), 0);

  EXPECT_EQ(ta.size, 0u);

  tu_assembler_free(&ta);
}

}  // namespace
