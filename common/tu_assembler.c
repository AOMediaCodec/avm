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

#include "common/tu_assembler.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "avm/avm_codec.h"
#include "avm/avm_integer.h"
#include "avm/avmcx.h"
#include "avm_dsp/bitwriter_buffer.h"
#include "av2/common/level.h"
#include "av2/common/obu_util.h"

// Ensure the buffer has room for 'needed' more bytes
static int ensure_capacity(TUAssembler *ta, size_t needed) {
  size_t required = ta->size + needed;
  if (required <= ta->capacity) return 0;
  size_t new_cap = ta->capacity * 2;
  if (new_cap < required) new_cap = required;
  uint8_t *new_buf = (uint8_t *)realloc(ta->buffer, new_cap);
  if (!new_buf) return -1;
  ta->buffer = new_buf;
  ta->capacity = new_cap;
  return 0;
}

// Append raw bytes to the assembler buffer
static int append_bytes(TUAssembler *ta, const uint8_t *data, size_t len) {
  if (ensure_capacity(ta, len) != 0) return -1;
  memcpy(ta->buffer + ta->size, data, len);
  ta->size += len;
  return 0;
}

// Write a ULEB128-encoded size value to the buffer
static int append_uleb128(TUAssembler *ta, uint64_t value) {
  uint8_t coded[10];
  size_t coded_size = 0;
  if (avm_uleb_encode(value, sizeof(coded), coded, &coded_size) != 0) return -1;
  return append_bytes(ta, coded, coded_size);
}

// Parse a single OBU header byte
static void parse_obu_header_byte(uint8_t byte, ObuHeader *hdr) {
  hdr->obu_header_extension_flag = (byte >> 7) & 1;
  hdr->type = (OBU_TYPE)((byte >> 2) & 0x1F);
  hdr->obu_tlayer_id = byte & 0x3;
}

// Parse extension byte
static void parse_obu_ext_byte(uint8_t byte, ObuHeader *hdr) {
  hdr->obu_mlayer_id = (byte >> 5) & 0x7;
  hdr->obu_xlayer_id = byte & 0x1F;
}

// Write a 2-byte OBU header with extension (sets xlayer_id)
static void write_obu_header_with_xlayer(uint8_t *dst, const ObuHeader *hdr,
                                         int xlayer_id) {
  // Byte 0: extension_flag=1, type, tlayer_id
  dst[0] = (uint8_t)((1 << 7) | ((hdr->type & 0x1F) << 2) |
                     (hdr->obu_tlayer_id & 0x3));
  // Byte 1: mlayer_id, xlayer_id
  dst[1] = (uint8_t)(((hdr->obu_mlayer_id & 0x7) << 5) | (xlayer_id & 0x1F));
}

int tu_assembler_init(TUAssembler *ta, const MultiXLayerConfig *mcfg) {
  memset(ta, 0, sizeof(*ta));
  ta->capacity = TU_ASM_INITIAL_CAPACITY;
  ta->buffer = (uint8_t *)malloc(ta->capacity);
  if (!ta->buffer) return -1;

  ta->num_xlayers = mcfg->num_xlayers;
  for (int i = 0; i < mcfg->num_xlayers; i++) {
    ta->xlayer_ids[i] = mcfg->xlayers[i].xlayer_id;
  }

  ta->msdo_enabled = mcfg->enable_msdo;
  ta->num_ops_sets = mcfg->num_ops_sets;
  ta->config = mcfg;

  // Populate Global LCR from config
  if (mcfg->enable_global_lcr || mcfg->enable_local_lcr) {
    populate_global_lcr_from_config(mcfg, &ta->global_lcr);
    // In local_only mode, the Global LCR is present for stream detection
    // and PTL but does not carry per-xlayer payload — Local LCRs are
    // the authoritative source.
    if (mcfg->enable_local_lcr && mcfg->local_lcr_mode == 1) {
      ta->global_lcr.lcr_global_payload_present_flag = 0;
    }
  }

  // Populate OPS from config
  for (int s = 0; s < mcfg->num_ops_sets; s++) {
    populate_ops_from_config(&mcfg->ops_sets[s], GLOBAL_XLAYER_ID, mcfg,
                             &ta->ops_list[s]);
  }

  // Populate Atlas from config
  if (mcfg->enable_atlas) {
    populate_atlas_from_config(mcfg, &ta->atlas_info);
  }

  return 0;
}

void tu_assembler_free(TUAssembler *ta) {
  if (ta->buffer) {
    free(ta->buffer);
    ta->buffer = NULL;
  }
  ta->size = 0;
  ta->capacity = 0;
}

int tu_assembler_write_td(TUAssembler *ta) {
  // Write a minimal 1-byte TD OBU: [size=1][header_byte]
  // TD type = OBU_TEMPORAL_DELIMITER = 2
  // Header: ext=0, type=2, tlayer=0 => (2 << 2) = 0x08
  uint8_t td[2];
  td[0] = 1;     // ULEB128 size = 1 (just the header byte)
  td[1] = 0x08;  // OBU_TEMPORAL_DELIMITER << 2
  return append_bytes(ta, td, 2);
}

// Helper: write per-xlayer info (mirrors write_lcr_xlayer_info in
// bitstream_lcr.c)
static void tu_asm_write_lcr_xlayer_info(LCRXLayerInfo *xinfo,
                                         int atlas_id_present,
                                         struct avm_write_bit_buffer *wb) {
  avm_wb_write_bit(wb, xinfo->lcr_rep_info_present_flag);
  avm_wb_write_bit(wb, xinfo->lcr_xlayer_purpose_present_flag);
  avm_wb_write_bit(wb, xinfo->lcr_xlayer_color_info_present_flag);
  avm_wb_write_bit(wb, xinfo->lcr_embedded_layer_info_present_flag);

  if (xinfo->lcr_rep_info_present_flag) {
    avm_wb_write_uvlc(wb, xinfo->rep_params.lcr_max_pic_width);
    avm_wb_write_uvlc(wb, xinfo->rep_params.lcr_max_pic_height);
    avm_wb_write_bit(wb, xinfo->rep_params.lcr_format_info_present_flag);
    avm_wb_write_bit(wb, xinfo->crop_win.crop_window_present_flag);
    if (xinfo->rep_params.lcr_format_info_present_flag) {
      avm_wb_write_uvlc(wb, xinfo->rep_params.lcr_bit_depth_idc);
      avm_wb_write_uvlc(wb, xinfo->rep_params.lcr_chroma_format_idc);
    }
    if (xinfo->crop_win.crop_window_present_flag) {
      avm_wb_write_uvlc(wb, xinfo->crop_win.crop_win_left_offset);
      avm_wb_write_uvlc(wb, xinfo->crop_win.crop_win_right_offset);
      avm_wb_write_uvlc(wb, xinfo->crop_win.crop_win_top_offset);
      avm_wb_write_uvlc(wb, xinfo->crop_win.crop_win_bottom_offset);
    }
  }

  if (xinfo->lcr_xlayer_purpose_present_flag)
    avm_wb_write_literal(wb, xinfo->lcr_xlayer_purpose_id, 7);

  if (xinfo->lcr_xlayer_color_info_present_flag) {
    struct XLayerColorInfo *col = &xinfo->xlayer_col_params;
    avm_wb_write_rice_golomb(wb, col->layer_color_description_idc, 2);
    if (col->layer_color_description_idc == 0) {
      avm_wb_write_literal(wb, col->layer_color_primaries, 8);
      avm_wb_write_literal(wb, col->layer_transfer_characteristics, 8);
      avm_wb_write_literal(wb, col->layer_matrix_coefficients, 8);
    }
    avm_wb_write_bit(wb, col->layer_full_range_flag);
  }

  // Byte alignment after per-xlayer flags/info
  avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % 8) % 8);

  if (xinfo->lcr_embedded_layer_info_present_flag) {
    struct EmbeddedLayerInfo *ml = &xinfo->mlayer_params;
    avm_wb_write_literal(wb, ml->lcr_mlayer_map, MAX_NUM_MLAYERS);
    for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
      if (ml->lcr_mlayer_map & (1 << m)) {
        avm_wb_write_literal(wb, ml->lcr_tlayer_map[m], MAX_NUM_TLAYERS);
        if (atlas_id_present) {
          avm_wb_write_literal(wb, ml->lcr_layer_atlas_segment_id[m], 8);
          avm_wb_write_literal(wb, ml->lcr_priority_order[m], 8);
          avm_wb_write_literal(wb, ml->lcr_rendering_method[m], 8);
        }
        avm_wb_write_literal(wb, ml->lcr_layer_type[m], 8);
        if (ml->lcr_layer_type[m] == AUX_LAYER) {
          avm_wb_write_literal(wb, ml->lcr_auxiliary_type[m], 8);
        }
        avm_wb_write_literal(wb, ml->lcr_view_type[m], 8);
        if (ml->lcr_view_type[m] == VIEW_EXPLICIT) {
          avm_wb_write_literal(wb, ml->lcr_view_id[m], 8);
        }
        if (m > 0) {
          avm_wb_write_literal(wb, ml->lcr_dependent_layer_map[m], m);
        }
        avm_wb_write_bit(wb, ml->lcr_same_sh_max_resolution_flag[m]);
        if (!ml->lcr_same_sh_max_resolution_flag[m]) {
          avm_wb_write_uvlc(wb, ml->lcr_max_expected_width[m]);
          avm_wb_write_uvlc(wb, ml->lcr_max_expected_height[m]);
        }
        // Byte alignment per mlayer
        int remaining = wb->bit_offset % 8;
        if (remaining != 0) avm_wb_write_literal(wb, 0, 8 - remaining);
      }
    }
  } else {
    if (atlas_id_present) {
      avm_wb_write_literal(wb, xinfo->lcr_xlayer_atlas_segment_id, 8);
      avm_wb_write_literal(wb, xinfo->lcr_xlayer_priority_order, 8);
      avm_wb_write_literal(wb, xinfo->lcr_xlayer_rendering_method, 8);
    }
  }
}

// Calculate lcr_data_size for a single xlayer payload
static uint32_t tu_asm_calculate_lcr_data_size(
    GlobalLayerConfigurationRecord *glcr, int i) {
  uint8_t temp[2048];
  struct avm_write_bit_buffer wb = { temp, 0 };
  int n = glcr->LcrXLayerID[i];

  if (glcr->lcr_dependent_xlayers_flag && n > 0)
    avm_wb_write_unsigned_literal(&wb, glcr->lcr_num_dependent_xlayer_map[i],
                                  n);

  tu_asm_write_lcr_xlayer_info(&glcr->xlayer_info[i],
                               glcr->lcr_global_atlas_id_present_flag, &wb);

  return (wb.bit_offset + 7) / 8;
}

int tu_assembler_write_global_lcr(TUAssembler *ta) {
  // Spec-compliant Global LCR OBU serialization matching
  // write_lcr_global_info() in bitstream_lcr.c.
  uint8_t lcr_buf[4096];
  struct avm_write_bit_buffer wb = { lcr_buf, 0 };
  GlobalLayerConfigurationRecord *glcr = &ta->global_lcr;

  // OBU header with extension (xlayer_id = GLOBAL_XLAYER_ID)
  avm_wb_write_bit(&wb, 1);  // extension flag
  avm_wb_write_literal(&wb, OBU_LAYER_CONFIGURATION_RECORD, 5);
  avm_wb_write_literal(&wb, 0, 2);                 // tlayer
  avm_wb_write_literal(&wb, 0, 3);                 // mlayer
  avm_wb_write_literal(&wb, GLOBAL_XLAYER_ID, 5);  // xlayer

  // Global LCR payload — matches write_lcr_global_info() exactly
  avm_wb_write_literal(&wb, glcr->lcr_global_config_record_id, 3);
  avm_wb_write_literal(&wb, glcr->lcr_xlayer_map, 31);
  avm_wb_write_bit(&wb, glcr->lcr_aggregate_info_present_flag);
  avm_wb_write_bit(&wb, glcr->lcr_seq_profile_tier_level_info_present_flag);
  avm_wb_write_bit(&wb, glcr->lcr_global_payload_present_flag);
  avm_wb_write_bit(&wb, glcr->lcr_dependent_xlayers_flag);
  avm_wb_write_bit(&wb, glcr->lcr_global_atlas_id_present_flag);
  avm_wb_write_literal(&wb, glcr->lcr_global_purpose_id, 7);
  avm_wb_write_bit(&wb, glcr->lcr_doh_constraint_flag);
  avm_wb_write_bit(&wb, glcr->lcr_enforce_tile_alignment_flag);
  if (glcr->lcr_global_atlas_id_present_flag)
    avm_wb_write_literal(&wb, glcr->lcr_global_atlas_id, 3);
  else
    avm_wb_write_literal(&wb, 0, 3);  // reserved
  avm_wb_write_literal(&wb, 0, 5);    // reserved

  if (glcr->lcr_aggregate_info_present_flag) {
    avm_wb_write_literal(&wb, glcr->aggregate_ptl.lcr_config_idc, 6);
    avm_wb_write_literal(&wb, glcr->aggregate_ptl.lcr_aggregate_level_idx, 5);
    avm_wb_write_bit(&wb, glcr->aggregate_ptl.lcr_max_tier_flag);
    avm_wb_write_literal(&wb, glcr->aggregate_ptl.lcr_max_interop, 4);
  }

  if (glcr->lcr_seq_profile_tier_level_info_present_flag) {
    for (int i = 0; i < glcr->LcrMaxNumXLayerCount; i++) {
      avm_wb_write_literal(&wb, glcr->seq_ptl[i].lcr_seq_profile_idc, 5);
      avm_wb_write_literal(&wb, glcr->seq_ptl[i].lcr_max_level_idx, 5);
      avm_wb_write_bit(&wb, glcr->seq_ptl[i].lcr_tier_flag);
      avm_wb_write_literal(&wb, glcr->seq_ptl[i].lcr_max_mlayer_count, 3);
      avm_wb_write_literal(&wb, glcr->seq_ptl[i].lcr_reserved_2bits, 2);
    }
  }

  if (glcr->lcr_global_payload_present_flag) {
    // Pre-calculate data sizes
    for (int i = 0; i < glcr->LcrMaxNumXLayerCount; i++) {
      glcr->lcr_data_size[i] = tu_asm_calculate_lcr_data_size(glcr, i);
    }
    for (int i = 0; i < glcr->LcrMaxNumXLayerCount; i++) {
      avm_wb_write_uleb(&wb, glcr->lcr_data_size[i]);
      // Write payload
      const uint32_t start_position = wb.bit_offset;
      int n = glcr->LcrXLayerID[i];
      if (glcr->lcr_dependent_xlayers_flag && n > 0)
        avm_wb_write_unsigned_literal(&wb,
                                      glcr->lcr_num_dependent_xlayer_map[i], n);
      tu_asm_write_lcr_xlayer_info(&glcr->xlayer_info[i],
                                   glcr->lcr_global_atlas_id_present_flag, &wb);
      // Pad remaining bits to match lcr_data_size
      const uint32_t parsed_bits = wb.bit_offset - start_position;
      const int remaining =
          (int)(glcr->lcr_data_size[i] * 8) - (int)parsed_bits;
      for (int j = 0; j < remaining; j++) avm_wb_write_bit(&wb, 0);
    }
  }

  // Extension flag + trailing bits
  avm_wb_write_bit(&wb, 0);  // lcr_extension_present_flag
  if (avm_wb_is_byte_aligned(&wb))
    avm_wb_write_literal(&wb, 0x80, 8);
  else
    avm_wb_write_bit(&wb, 1);

  uint32_t obu_payload_size = avm_wb_bytes_written(&wb);

  // Write: [uleb128 total size][obu data]
  if (append_uleb128(ta, (uint64_t)obu_payload_size) != 0) return -1;
  return append_bytes(ta, lcr_buf, obu_payload_size);
}

int tu_assembler_write_local_lcr(TUAssembler *ta, int xlayer_idx) {
  // Spec-compliant Local LCR OBU serialization matching
  // write_lcr_local_info() in bitstream_lcr.c.
  GlobalLayerConfigurationRecord *glcr = &ta->global_lcr;

  if (xlayer_idx < 0 || xlayer_idx >= glcr->LcrMaxNumXLayerCount) return -1;

  int xlayer_id = glcr->LcrXLayerID[xlayer_idx];
  uint8_t lcr_buf[4096];
  struct avm_write_bit_buffer wb = { lcr_buf, 0 };

  // OBU header with extension (xlayer_id = per-xlayer, NOT global)
  avm_wb_write_bit(&wb, 1);  // extension flag
  avm_wb_write_literal(&wb, OBU_LAYER_CONFIGURATION_RECORD, 5);
  avm_wb_write_literal(&wb, 0, 2);                 // tlayer
  avm_wb_write_literal(&wb, 0, 3);                 // mlayer
  avm_wb_write_literal(&wb, xlayer_id & 0x1F, 5);  // xlayer

  // Local LCR payload — matches write_lcr_local_info()
  avm_wb_write_literal(&wb, glcr->lcr_global_config_record_id,
                       3);          // lcr_global_id
  avm_wb_write_literal(&wb, 1, 3);  // lcr_local_id (matches encoder.c:938)
  avm_wb_write_bit(&wb, 1);         // lcr_profile_tier_level_info_present_flag
  avm_wb_write_bit(&wb, 0);         // lcr_local_atlas_id_present_flag

  // PTL — reuse same data as Global LCR seq_ptl for this xlayer
  avm_wb_write_literal(&wb, glcr->seq_ptl[xlayer_idx].lcr_seq_profile_idc, 5);
  avm_wb_write_literal(&wb, glcr->seq_ptl[xlayer_idx].lcr_max_level_idx, 5);
  avm_wb_write_bit(&wb, glcr->seq_ptl[xlayer_idx].lcr_tier_flag);
  avm_wb_write_literal(&wb, glcr->seq_ptl[xlayer_idx].lcr_max_mlayer_count, 3);
  avm_wb_write_literal(&wb, 0, 2);  // lcr_reserved_2bits

  // Reserved bits (atlas_id not present)
  avm_wb_write_literal(&wb, 0, 3);  // lcr_reserved_zero_3bits
  avm_wb_write_literal(&wb, 0, 5);  // lcr_reserved_zero_5bits

  // xlayer_info — identical data to Global LCR to pass decoder validation
  tu_asm_write_lcr_xlayer_info(&glcr->xlayer_info[xlayer_idx], 0, &wb);

  // Extension flag + trailing bits
  avm_wb_write_bit(&wb, 0);  // lcr_extension_present_flag
  if (avm_wb_is_byte_aligned(&wb))
    avm_wb_write_literal(&wb, 0x80, 8);
  else
    avm_wb_write_bit(&wb, 1);

  uint32_t obu_payload_size = avm_wb_bytes_written(&wb);

  if (append_uleb128(ta, (uint64_t)obu_payload_size) != 0) return -1;
  return append_bytes(ta, lcr_buf, obu_payload_size);
}

int tu_assembler_write_msdo(TUAssembler *ta) {
  if (!ta->msdo_enabled) return 0;

  // Write MSDO OBU — ported from stream_multiplexer.cc
  uint8_t msdo_buf[128];
  struct avm_write_bit_buffer wb = { msdo_buf, 0 };

  // OBU header with extension (xlayer_id = GLOBAL_XLAYER_ID)
  avm_wb_write_bit(&wb, 1);  // extension flag
  avm_wb_write_literal(&wb, OBU_MULTI_STREAM_DECODER_OPERATION, 5);
  avm_wb_write_literal(&wb, 0, 2);                 // tlayer
  avm_wb_write_literal(&wb, 0, 3);                 // mlayer
  avm_wb_write_literal(&wb, GLOBAL_XLAYER_ID, 5);  // xlayer

  // MSDO payload
  avm_wb_write_literal(&wb, ta->num_xlayers - 2, 3);  // num_streams - 2
  avm_wb_write_literal(&wb, MAIN_420_10_IP1, PROFILE_BITS);
  avm_wb_write_literal(&wb, SEQ_LEVEL_4_0, LEVEL_BITS);
  avm_wb_write_bit(&wb, 0);  // tier

  // Even allocation flag
  avm_wb_write_bit(&wb, 1);  // multistream_even_allocation_flag

  // Per-stream info
  for (int i = 0; i < ta->num_xlayers; i++) {
    avm_wb_write_literal(&wb, ta->xlayer_ids[i], XLAYER_BITS);
    avm_wb_write_literal(&wb, 0, PROFILE_BITS);
    avm_wb_write_literal(&wb, SEQ_LEVEL_4_0, LEVEL_BITS);
    avm_wb_write_bit(&wb, 0);  // tier
  }

  // doh_constraint_flag
  avm_wb_write_bit(&wb, ta->config->lcr_doh_constraint_flag);

  // Trailing bit
  if ((wb.bit_offset % 8) == 0) {
    avm_wb_write_literal(&wb, 0x80, 8);
  } else {
    avm_wb_write_bit(&wb, 1);
    while ((wb.bit_offset % 8) != 0) avm_wb_write_bit(&wb, 0);
  }

  uint32_t obu_size = avm_wb_bytes_written(&wb);

  if (append_uleb128(ta, (uint64_t)obu_size) != 0) return -1;
  return append_bytes(ta, msdo_buf, obu_size);
}

// Compute ops_data_size for a single operating point.
// Mirrors calculate_ops_data_size() in bitstream_ops.c.
static uint32_t tu_asm_calculate_ops_data_size(const OperatingPointSet *ops,
                                               int obu_xlayer_id,
                                               int op_index) {
  uint8_t temp_buffer[1024];
  struct avm_write_bit_buffer temp_wb = { temp_buffer, 0 };
  const OperatingPoint *op = &ops->op[op_index];

  if (ops->ops_intent_present_flag)
    avm_wb_write_literal(&temp_wb, op->ops_intent_op, 7);

  if (ops->ops_ptl_present_flag) {
    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      avm_wb_write_literal(&temp_wb, op->ops_config_idc, MULTI_SEQ_CONFIG_BITS);
      avm_wb_write_literal(&temp_wb, op->ops_aggregate_level_idx, LEVEL_BITS);
      avm_wb_write_bit(&temp_wb, op->ops_max_tier_flag);
      avm_wb_write_literal(&temp_wb, op->ops_max_interop, INTEROP_BITS);
    } else {
      avm_wb_write_literal(&temp_wb, op->ops_seq_profile_idc[obu_xlayer_id],
                           PROFILE_BITS);
      avm_wb_write_literal(&temp_wb, op->ops_level_idx[obu_xlayer_id],
                           LEVEL_BITS);
      avm_wb_write_bit(&temp_wb, op->ops_tier_flag[obu_xlayer_id]);
      avm_wb_write_literal(&temp_wb, op->ops_mlayer_count[obu_xlayer_id], 3);
      avm_wb_write_literal(&temp_wb, 0, 2);
    }
  }

  if (ops->ops_color_info_present_flag) {
    // Simplified: write ops_color_description_idc=1 (unspecified, no payload)
    avm_wb_write_rice_golomb(&temp_wb, op->color_info.ops_color_description_idc,
                             2);
    if (op->color_info.ops_color_description_idc == 0) {
      avm_wb_write_literal(&temp_wb, op->color_info.ops_color_primaries, 8);
      avm_wb_write_literal(&temp_wb,
                           op->color_info.ops_transfer_characteristics, 8);
      avm_wb_write_literal(&temp_wb, op->color_info.ops_matrix_coefficients, 8);
    }
    avm_wb_write_bit(&temp_wb, op->color_info.ops_full_range_flag);
  }

  avm_wb_write_bit(&temp_wb,
                   op->ops_decoder_model_info_for_this_op_present_flag);

  int ops_initial_display_delay_present_flag =
      op->ops_initial_display_delay != BUFFER_POOL_MAX_SIZE;
  avm_wb_write_bit(&temp_wb, ops_initial_display_delay_present_flag);
  if (ops_initial_display_delay_present_flag) {
    avm_wb_write_literal(&temp_wb, op->ops_initial_display_delay - 1, 4);
  }

  if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
    avm_wb_write_literal(&temp_wb, op->ops_xlayer_map, MAX_NUM_XLAYERS - 1);
    for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
      if (op->ops_xlayer_map & (1 << j)) {
        if (ops->ops_ptl_present_flag) {
          avm_wb_write_literal(&temp_wb, op->ops_seq_profile_idc[j],
                               PROFILE_BITS);
          avm_wb_write_literal(&temp_wb, op->ops_level_idx[j], LEVEL_BITS);
          avm_wb_write_bit(&temp_wb, op->ops_tier_flag[j]);
          avm_wb_write_literal(&temp_wb, op->ops_mlayer_count[j], 3);
          avm_wb_write_literal(&temp_wb, 0, 2);
        }
        if (ops->ops_mlayer_info_idc == 1) {
          avm_wb_write_literal(&temp_wb, op->mlayer_info.ops_mlayer_map[j],
                               MAX_NUM_MLAYERS);
          for (int m = 0; m < 8; m++) {
            if (op->mlayer_info.ops_mlayer_map[j] & (1 << m)) {
              avm_wb_write_literal(&temp_wb,
                                   op->mlayer_info.ops_tlayer_map[j][m],
                                   MAX_NUM_TLAYERS);
            }
          }
        } else if (ops->ops_mlayer_info_idc == 2) {
          avm_wb_write_bit(&temp_wb, op->ops_mlayer_explicit_info_flag[j]);
          if (op->ops_mlayer_explicit_info_flag[j]) {
            avm_wb_write_literal(&temp_wb, op->mlayer_info.ops_mlayer_map[j],
                                 MAX_NUM_MLAYERS);
            for (int m = 0; m < 8; m++) {
              if (op->mlayer_info.ops_mlayer_map[j] & (1 << m)) {
                avm_wb_write_literal(&temp_wb,
                                     op->mlayer_info.ops_tlayer_map[j][m],
                                     MAX_NUM_TLAYERS);
              }
            }
          } else {
            avm_wb_write_literal(&temp_wb, op->ops_embedded_ops_id[j], 4);
            avm_wb_write_literal(&temp_wb, op->ops_embedded_op_index[j], 3);
          }
        }
      }
    }
  } else {
    avm_wb_write_literal(&temp_wb,
                         op->mlayer_info.ops_mlayer_map[obu_xlayer_id],
                         MAX_NUM_MLAYERS);
    for (int m = 0; m < 8; m++) {
      if (op->mlayer_info.ops_mlayer_map[obu_xlayer_id] & (1 << m)) {
        avm_wb_write_literal(&temp_wb,
                             op->mlayer_info.ops_tlayer_map[obu_xlayer_id][m],
                             MAX_NUM_TLAYERS);
      }
    }
  }

  // Byte alignment
  avm_wb_write_literal(&temp_wb, 0, (8 - temp_wb.bit_offset % 8) % 8);
  return (temp_wb.bit_offset + 7) / 8;
}

int tu_assembler_write_ops(TUAssembler *ta, int xlayer_id) {
  // Spec-compliant OPS OBU serialization matching
  // av2_write_operating_point_set_obu() in bitstream_ops.c.

  for (int s = 0; s < ta->num_ops_sets; s++) {
    const OperatingPointSet *ops = &ta->ops_list[s];
    if (!ops->valid) continue;

    int obu_xlayer_id = (xlayer_id >= 0) ? xlayer_id : ops->obu_xlayer_id;

    uint8_t ops_buf[2048];
    struct avm_write_bit_buffer wb = { ops_buf, 0 };

    // OBU header with extension
    avm_wb_write_bit(&wb, 1);  // extension flag
    avm_wb_write_literal(&wb, OBU_OPERATING_POINT_SET, 5);
    avm_wb_write_literal(&wb, 0, 2);                     // tlayer
    avm_wb_write_literal(&wb, 0, 3);                     // mlayer
    avm_wb_write_literal(&wb, obu_xlayer_id & 0x1F, 5);  // xlayer

    // OPS payload — mirrors av2_write_operating_point_set_obu()
    avm_wb_write_bit(&wb, ops->ops_reset_flag);
    avm_wb_write_literal(&wb, ops->ops_id, OPS_ID_BITS);
    avm_wb_write_literal(&wb, ops->ops_cnt, OPS_COUNT_BITS);

    if (ops->ops_cnt > 0) {
      avm_wb_write_literal(&wb, ops->ops_priority, 4);
      avm_wb_write_literal(&wb, ops->ops_intent, 7);
      avm_wb_write_bit(&wb, ops->ops_intent_present_flag);
      avm_wb_write_bit(&wb, ops->ops_ptl_present_flag);
      avm_wb_write_bit(&wb, ops->ops_color_info_present_flag);
      if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
        avm_wb_write_literal(&wb, ops->ops_mlayer_info_idc, 2);
      } else {
        avm_wb_write_literal(&wb, 0, 2);
      }
    }

    for (int p = 0; p < ops->ops_cnt; p++) {
      OperatingPoint *op = (OperatingPoint *)&ops->op[p];

      // Calculate and write ops_data_size
      uint32_t data_size =
          tu_asm_calculate_ops_data_size(ops, obu_xlayer_id, p);
      avm_wb_write_uleb(&wb, data_size);

      if (ops->ops_intent_present_flag)
        avm_wb_write_literal(&wb, op->ops_intent_op, 7);

      if (ops->ops_ptl_present_flag) {
        if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
          avm_wb_write_literal(&wb, op->ops_config_idc, MULTI_SEQ_CONFIG_BITS);
          avm_wb_write_literal(&wb, op->ops_aggregate_level_idx, LEVEL_BITS);
          avm_wb_write_bit(&wb, op->ops_max_tier_flag);
          avm_wb_write_literal(&wb, op->ops_max_interop, INTEROP_BITS);
        } else {
          avm_wb_write_literal(&wb, op->ops_seq_profile_idc[obu_xlayer_id],
                               PROFILE_BITS);
          avm_wb_write_literal(&wb, op->ops_level_idx[obu_xlayer_id],
                               LEVEL_BITS);
          avm_wb_write_bit(&wb, op->ops_tier_flag[obu_xlayer_id]);
          avm_wb_write_literal(&wb, op->ops_mlayer_count[obu_xlayer_id], 3);
          avm_wb_write_literal(&wb, 0, 2);
        }
      }

      if (ops->ops_color_info_present_flag) {
        avm_wb_write_rice_golomb(&wb, op->color_info.ops_color_description_idc,
                                 2);
        if (op->color_info.ops_color_description_idc == 0) {
          avm_wb_write_literal(&wb, op->color_info.ops_color_primaries, 8);
          avm_wb_write_literal(&wb, op->color_info.ops_transfer_characteristics,
                               8);
          avm_wb_write_literal(&wb, op->color_info.ops_matrix_coefficients, 8);
        }
        avm_wb_write_bit(&wb, op->color_info.ops_full_range_flag);
      }

      avm_wb_write_bit(&wb,
                       op->ops_decoder_model_info_for_this_op_present_flag);

      int ops_initial_display_delay_present_flag =
          op->ops_initial_display_delay != BUFFER_POOL_MAX_SIZE;
      avm_wb_write_bit(&wb, ops_initial_display_delay_present_flag);
      if (ops_initial_display_delay_present_flag) {
        avm_wb_write_literal(&wb, op->ops_initial_display_delay - 1, 4);
      }

      if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
        avm_wb_write_literal(&wb, op->ops_xlayer_map, MAX_NUM_XLAYERS - 1);
        for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
          if (op->ops_xlayer_map & (1 << j)) {
            if (ops->ops_ptl_present_flag) {
              avm_wb_write_literal(&wb, op->ops_seq_profile_idc[j],
                                   PROFILE_BITS);
              avm_wb_write_literal(&wb, op->ops_level_idx[j], LEVEL_BITS);
              avm_wb_write_bit(&wb, op->ops_tier_flag[j]);
              avm_wb_write_literal(&wb, op->ops_mlayer_count[j], 3);
              avm_wb_write_literal(&wb, 0, 2);
            }
            if (ops->ops_mlayer_info_idc == 1) {
              avm_wb_write_literal(&wb, op->mlayer_info.ops_mlayer_map[j],
                                   MAX_NUM_MLAYERS);
              for (int m = 0; m < 8; m++) {
                if (op->mlayer_info.ops_mlayer_map[j] & (1 << m)) {
                  avm_wb_write_literal(&wb,
                                       op->mlayer_info.ops_tlayer_map[j][m],
                                       MAX_NUM_TLAYERS);
                }
              }
            } else if (ops->ops_mlayer_info_idc == 2) {
              avm_wb_write_bit(&wb, op->ops_mlayer_explicit_info_flag[j]);
              if (op->ops_mlayer_explicit_info_flag[j]) {
                avm_wb_write_literal(&wb, op->mlayer_info.ops_mlayer_map[j],
                                     MAX_NUM_MLAYERS);
                for (int m = 0; m < 8; m++) {
                  if (op->mlayer_info.ops_mlayer_map[j] & (1 << m)) {
                    avm_wb_write_literal(&wb,
                                         op->mlayer_info.ops_tlayer_map[j][m],
                                         MAX_NUM_TLAYERS);
                  }
                }
              } else {
                avm_wb_write_literal(&wb, op->ops_embedded_ops_id[j], 4);
                avm_wb_write_literal(&wb, op->ops_embedded_op_index[j], 3);
              }
            }
          }
        }
      } else {
        avm_wb_write_literal(&wb, op->mlayer_info.ops_mlayer_map[obu_xlayer_id],
                             MAX_NUM_MLAYERS);
        for (int m = 0; m < 8; m++) {
          if (op->mlayer_info.ops_mlayer_map[obu_xlayer_id] & (1 << m)) {
            avm_wb_write_literal(
                &wb, op->mlayer_info.ops_tlayer_map[obu_xlayer_id][m],
                MAX_NUM_TLAYERS);
          }
        }
      }

      // Byte alignment at end of each operating point
      avm_wb_write_literal(&wb, 0, (8 - wb.bit_offset % 8) % 8);
    }

    // Extension flag
    avm_wb_write_bit(&wb, 0);

    // Trailing bits
    if (avm_wb_is_byte_aligned(&wb)) {
      avm_wb_write_literal(&wb, 0x80, 8);
    } else {
      avm_wb_write_bit(&wb, 1);
    }

    uint32_t obu_size = avm_wb_bytes_written(&wb);
    if (append_uleb128(ta, (uint64_t)obu_size) != 0) return -1;
    if (append_bytes(ta, ops_buf, obu_size) != 0) return -1;
  }

  return 0;
}

int tu_assembler_write_atlas(TUAssembler *ta) {
  if (!ta->config->enable_atlas) return 0;

  AtlasSegmentInfo *atlas = &ta->atlas_info;
  if (!atlas->valid) return 0;

  uint8_t atlas_buf[4096];
  struct avm_write_bit_buffer wb = { atlas_buf, 0 };

  // OBU header with extension (xlayer_id = GLOBAL_XLAYER_ID)
  avm_wb_write_bit(&wb, 1);  // extension flag
  avm_wb_write_literal(&wb, OBU_ATLAS_SEGMENT, 5);
  avm_wb_write_literal(&wb, 0, 2);                 // tlayer
  avm_wb_write_literal(&wb, 0, 3);                 // mlayer
  avm_wb_write_literal(&wb, GLOBAL_XLAYER_ID, 5);  // xlayer

  // Atlas payload — mirrors av2_write_atlas_segment_info_obu()
  avm_wb_write_literal(&wb, atlas->atlas_segment_id, 3);
  avm_wb_write_uvlc(&wb, atlas->atlas_segment_mode_idc);

  int num_segments = 0;
  if (atlas->atlas_segment_mode_idc == ENHANCED_ATLAS) {
    // Write region info
    struct AtlasRegionInfo *reg = &atlas->ats_reg_params;
    avm_wb_write_uvlc(&wb, reg->ats_num_region_columns_minus_1);
    avm_wb_write_uvlc(&wb, reg->ats_num_region_rows_minus_1);
    avm_wb_write_bit(&wb, reg->ats_uniform_spacing_flag);
    if (!reg->ats_uniform_spacing_flag) {
      for (int i = 0; i <= reg->ats_num_region_columns_minus_1; i++)
        avm_wb_write_uvlc(&wb, reg->ats_column_width_minus_1[i]);
      for (int i = 0; i <= reg->ats_num_region_rows_minus_1; i++)
        avm_wb_write_uvlc(&wb, reg->ats_row_height_minus_1[i]);
    } else {
      avm_wb_write_uvlc(&wb, reg->ats_region_width_minus_1);
      avm_wb_write_uvlc(&wb, reg->ats_region_height_minus_1);
    }

    // Write region to segment mapping
    struct AtlasRegionToSegmentMapping *map = &atlas->ats_reg_seg_map;
    avm_wb_write_bit(&wb, map->ats_single_region_per_atlas_segment_flag);
    if (!map->ats_single_region_per_atlas_segment_flag) {
      avm_wb_write_uvlc(&wb, map->ats_num_atlas_segments_minus_1);
      int ns = map->ats_num_atlas_segments_minus_1 + 1;
      for (int i = 0; i < ns; i++) {
        avm_wb_write_uvlc(&wb, map->ats_top_left_region_column[i]);
        avm_wb_write_uvlc(&wb, map->ats_top_left_region_row[i]);
        avm_wb_write_uvlc(&wb, map->ats_bottom_right_region_column_offset[i]);
        avm_wb_write_uvlc(&wb, map->ats_bottom_right_region_row_offset[i]);
      }
      num_segments = ns;
    } else {
      num_segments = reg->NumRegionsInAtlas;
      map->ats_num_atlas_segments_minus_1 = num_segments - 1;
    }
  } else if (atlas->atlas_segment_mode_idc == MULTISTREAM_ATLAS) {
    // Write basic info for multistream
    struct AtlasBasicInfo *basic = &atlas->ats_basic_info_s;
    avm_wb_write_bit(&wb, basic->ats_stream_id_present);
    avm_wb_write_uvlc(&wb, basic->ats_atlas_width);
    avm_wb_write_uvlc(&wb, basic->ats_atlas_height);
    avm_wb_write_uvlc(&wb, basic->ats_num_atlas_segments_minus_1);

    int ns = basic->ats_num_atlas_segments_minus_1 + 1;
    for (int i = 0; i < ns; i++) {
      if (basic->ats_stream_id_present)
        avm_wb_write_literal(&wb, basic->ats_input_stream_id[i], 5);
      avm_wb_write_uvlc(&wb, basic->ats_segment_top_left_pos_x[i]);
      avm_wb_write_uvlc(&wb, basic->ats_segment_top_left_pos_y[i]);
      avm_wb_write_uvlc(&wb, basic->ats_segment_width[i]);
      avm_wb_write_uvlc(&wb, basic->ats_segment_height[i]);
    }
    num_segments = ns;
  }

  // Label segment info
  avm_wb_write_bit(&wb,
                   atlas->ats_label_seg.ats_signalled_atlas_segment_ids_flag);
  if (atlas->ats_label_seg.ats_signalled_atlas_segment_ids_flag) {
    for (int i = 0; i < num_segments; i++) {
      avm_wb_write_literal(&wb, atlas->ats_label_seg.ats_atlas_segment_id[i],
                           ATLAS_LABEL_SEG_ID_BITS);
    }
  }

  // Extension + trailing bits
  avm_wb_write_bit(&wb, 0);  // ats_extension_present_flag
  if (avm_wb_is_byte_aligned(&wb))
    avm_wb_write_literal(&wb, 0x80, 8);
  else
    avm_wb_write_bit(&wb, 1);

  uint32_t obu_size = avm_wb_bytes_written(&wb);
  if (append_uleb128(ta, (uint64_t)obu_size) != 0) return -1;
  return append_bytes(ta, atlas_buf, obu_size);
}

int tu_assembler_append_xlayer_obus(TUAssembler *ta, int xlayer_id,
                                    const uint8_t *data, size_t size) {
  // Parse OBUs from per-xlayer encoder output and rewrite headers
  // with the specified xlayer_id. Skip TDs and structural OBUs
  // (the assembler writes those globally).
  size_t consumed = 0;

  while (consumed < size) {
    size_t remaining = size - consumed;
    size_t length_field_size = 0;
    uint64_t obu_total_size = 0;

    // Read OBU total size (ULEB128)
    if (avm_uleb_decode(data + consumed, remaining, &obu_total_size,
                        &length_field_size) != 0) {
      fprintf(stderr, "OBU size parsing failed at offset %zu\n", consumed);
      return -1;
    }

    if (obu_total_size == 0 ||
        consumed + length_field_size + obu_total_size > size) {
      break;
    }

    // Parse OBU header
    const uint8_t *obu_start = data + consumed + length_field_size;
    ObuHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    parse_obu_header_byte(obu_start[0], &hdr);

    int obu_header_size = 1;
    if (hdr.obu_header_extension_flag) {
      parse_obu_ext_byte(obu_start[1], &hdr);
      obu_header_size = 2;
    }

    consumed += length_field_size + (size_t)obu_total_size;

    // Skip TD OBUs — the assembler writes a single global TD
    if (hdr.type == OBU_TEMPORAL_DELIMITER) continue;

    // Skip structural OBUs — the assembler generates global versions
    if (hdr.type == OBU_MULTI_STREAM_DECODER_OPERATION) continue;
    if (hdr.type == OBU_LAYER_CONFIGURATION_RECORD) continue;
    if (hdr.type == OBU_OPERATING_POINT_SET) continue;
    if (hdr.type == OBU_ATLAS_SEGMENT) continue;

    // Rewrite OBU header with xlayer_id and recalculate size
    // New header is always 2 bytes (extension flag set)
    uint8_t new_header[2];
    write_obu_header_with_xlayer(new_header, &hdr, xlayer_id);

    // Payload is everything after the original header
    const uint8_t *payload = obu_start + obu_header_size;
    size_t payload_size = (size_t)obu_total_size - (size_t)obu_header_size;

    // New OBU total size = 2 (header) + payload_size
    uint64_t new_obu_total_size = 2 + payload_size;

    // Write: [uleb128 new total size][2-byte header][payload]
    if (append_uleb128(ta, new_obu_total_size) != 0) return -1;
    if (append_bytes(ta, new_header, 2) != 0) return -1;
    if (payload_size > 0) {
      if (append_bytes(ta, payload, payload_size) != 0) return -1;
    }
  }

  return 0;
}

int tu_assembler_flush(TUAssembler *ta, FILE *outfile) {
  if (ta->size == 0) return 0;
  size_t written = fwrite(ta->buffer, 1, ta->size, outfile);
  if (written != ta->size) {
    fprintf(stderr, "Error: failed to write TU (%zu of %zu bytes)\n", written,
            ta->size);
    return -1;
  }
  ta->size = 0;
  return 0;
}

// Write structural OBUs (LCR, OPS, Atlas) into the assembler buffer.
// Called at the start of a TU when first_output or a keyframe is present.
// OBU order per spec: Global config (MSDO, Global LCR, Global OPS, Global
// Atlas) then per-xlayer data with Local LCR preceding each xlayer's OBUs.
// Local LCRs are NOT emitted here; they are emitted per-xlayer in the caller.
void tu_assembler_write_structural_obus(TUAssembler *ta,
                                        const MultiXLayerConfig *mcfg,
                                        int *first_output, int has_keyframe) {
  if (*first_output || has_keyframe) {
    *first_output = 0;
    if (mcfg->enable_msdo) tu_assembler_write_msdo(ta);
    if (mcfg->enable_global_lcr || mcfg->enable_local_lcr)
      tu_assembler_write_global_lcr(ta);
    tu_assembler_write_ops(ta, GLOBAL_XLAYER_ID);
    if (mcfg->enable_atlas) tu_assembler_write_atlas(ta);
  }
}

// Rewrite an OBU's header with a new xlayer_id and append it to the assembler.
// obu_start points to the OBU data, obu_size is the total OBU size (header +
// payload), and obu_header_size is 1 or 2 bytes.
static void rewrite_and_append_obu(TUAssembler *ta, const uint8_t *obu_start,
                                   size_t obu_size, int obu_header_size,
                                   int xlayer_id) {
  ObuHeader hdr;
  memset(&hdr, 0, sizeof(hdr));
  parse_obu_header_byte(obu_start[0], &hdr);
  if (hdr.obu_header_extension_flag) parse_obu_ext_byte(obu_start[1], &hdr);
  uint8_t new_header[2];
  write_obu_header_with_xlayer(new_header, &hdr, xlayer_id);
  const uint8_t *payload = obu_start + obu_header_size;
  size_t payload_size = obu_size - (size_t)obu_header_size;
  uint64_t new_obu_total_size = 2 + payload_size;
  append_uleb128(ta, new_obu_total_size);
  append_bytes(ta, new_header, 2);
  if (payload_size > 0) append_bytes(ta, payload, payload_size);
}

int tu_assembler_write_split_tus(TUAssembler *ta, const MultiXLayerConfig *mcfg,
                                 int xlayer_id, const uint8_t *data,
                                 size_t size, int *first_output,
                                 FILE *outfile) {
  // Preserve the encoder's frame order exactly to maintain DPB consistency.
  //
  // In multi_layers_lag_test mode, the encoder codes hidden frames (ARF,
  // INTNL_ARF) followed by the displayable frame for each mlayer, then
  // repeats for the next mlayer.  It inserts a TD before each group of
  // frames that belong to the same temporal unit.  We respect these TDs
  // as TU boundaries, bundling hidden frames with their displayable frame
  // into a single TU.

  // Single-pass: parse all OBUs into a stack-allocated array.
  // A typical encoder packet contains at most a few dozen OBUs per TU
  // (TD + SH + MFH + QM + FGM + CI + BRT + frame OBUs per mlayer).
  // 256 entries is generous for any realistic configuration.
  typedef struct {
    size_t data_offset;  // start of OBU data (after length field)
    size_t data_size;    // OBU total size (header + payload)
    int type;
    int mlayer_id;
    int is_td;
    int is_structural;
    int is_keyframe;
    int obu_header_size;  // 1 or 2 bytes
  } ObuEntry;

  enum { MAX_OBU_ENTRIES = 256 };
  ObuEntry obus[MAX_OBU_ENTRIES];
  int num_obus = 0;

  {
    size_t consumed = 0;
    while (consumed < size && num_obus < MAX_OBU_ENTRIES) {
      size_t length_field_size = 0;
      uint64_t obu_total_size = 0;
      if (avm_uleb_decode(data + consumed, size - consumed, &obu_total_size,
                          &length_field_size) != 0)
        break;
      if (obu_total_size == 0 ||
          consumed + length_field_size + obu_total_size > size)
        break;
      const uint8_t *obu_start = data + consumed + length_field_size;
      ObuHeader hdr;
      memset(&hdr, 0, sizeof(hdr));
      parse_obu_header_byte(obu_start[0], &hdr);
      if (hdr.obu_header_extension_flag) parse_obu_ext_byte(obu_start[1], &hdr);

      obus[num_obus].data_offset = consumed + length_field_size;
      obus[num_obus].data_size = (size_t)obu_total_size;
      obus[num_obus].type = hdr.type;
      obus[num_obus].mlayer_id = hdr.obu_mlayer_id;
      obus[num_obus].is_td = (hdr.type == OBU_TEMPORAL_DELIMITER);
      obus[num_obus].is_structural =
          (hdr.type == OBU_MULTI_STREAM_DECODER_OPERATION ||
           hdr.type == OBU_LAYER_CONFIGURATION_RECORD ||
           hdr.type == OBU_OPERATING_POINT_SET ||
           hdr.type == OBU_ATLAS_SEGMENT);
      obus[num_obus].is_keyframe = (hdr.type == OBU_CLOSED_LOOP_KEY);
      obus[num_obus].obu_header_size =
          1 + (hdr.obu_header_extension_flag ? 1 : 0);

      consumed += length_field_size + (size_t)obu_total_size;
      num_obus++;
    }
  }

  if (num_obus == 0) return 0;

  // Respect the encoder's TD placement to form TUs.  The encoder inserts a
  // TD before each group of hidden + displayable frames that belong to the
  // same temporal unit.  All frames between two consecutive encoder TDs are
  // bundled into a single output TU, keeping hidden frames together with
  // their displayable frame as the spec requires.
  int tu_count = 0;
  int tu_started = 0;          // 1 once we've written our TD for the current TU
  int structural_written = 0;  // 1 once structural OBUs written for current TU
  int pending_start = -1;      // Start of non-frame OBUs preceding a frame

  for (int i = 0; i < num_obus; i++) {
    if (obus[i].is_td) {
      // Encoder TD marks start of a new temporal unit.
      // Flush the previous TU if one was started.
      if (tu_started) {
        tu_assembler_flush(ta, outfile);
        tu_count++;
      }
      // Begin new TU with our own TD.
      ta->size = 0;
      tu_assembler_write_td(ta);
      tu_started = 1;
      structural_written = 0;
      pending_start = -1;
      continue;
    }

    if (obus[i].is_structural) continue;  // skip encoder structural OBUs

    int is_frame = (obus[i].type != OBU_SEQUENCE_HEADER) &&
                   (obus[i].type != OBU_MULTI_FRAME_HEADER) &&
                   (obus[i].type != OBU_BUFFER_REMOVAL_TIMING) &&
                   (obus[i].type != OBU_QUANTIZATION_MATRIX) &&
                   (obus[i].type != OBU_FILM_GRAIN_MODEL) &&
                   (obus[i].type != OBU_CONTENT_INTERPRETATION);

    if (!is_frame) {
      // Track where non-frame OBUs start
      if (pending_start < 0) pending_start = i;
      continue;
    }

    // Frame OBU — append to the current TU.
    if (!tu_started) {
      // Frame without a preceding encoder TD (e.g. ml>0 frames in a
      // separate call).  Start a new TU.
      ta->size = 0;
      tu_assembler_write_td(ta);
      tu_started = 1;
      structural_written = 0;
    }

    // Write structural OBUs once per TU, before the first frame that
    // needs them.  tu_assembler_write_structural_obus() has its own
    // first_output / keyframe guard, so calling it for each frame is safe
    // — it will only emit once.
    if (!structural_written) {
      tu_assembler_write_structural_obus(ta, mcfg, first_output,
                                         obus[i].is_keyframe);
      structural_written = 1;
    }

    // Write any non-frame OBUs that preceded this frame (SH, etc.)
    for (int j = (pending_start >= 0 ? pending_start : i); j < i; j++) {
      if (obus[j].is_td || obus[j].is_structural) continue;
      rewrite_and_append_obu(ta, data + obus[j].data_offset, obus[j].data_size,
                             obus[j].obu_header_size, xlayer_id);
    }
    pending_start = -1;

    // Write the frame OBU itself with xlayer_id
    rewrite_and_append_obu(ta, data + obus[i].data_offset, obus[i].data_size,
                           obus[i].obu_header_size, xlayer_id);
  }

  // Flush the last TU if one is in progress.
  if (tu_started) {
    tu_assembler_flush(ta, outfile);
    tu_count++;
  }

  return tu_count;
}

int tu_assembler_parse_tu_segments(const uint8_t *data, size_t size,
                                   TUSegmentInfo *segs, int max_segs) {
  // Scan OBUs, splitting at TD boundaries.  Each segment starts at a TD
  // and extends to just before the next TD (or end of data).
  int nseg = 0;
  size_t seg_start = 0;
  int has_kf = 0;
  int in_segment = 0;
  size_t consumed = 0;

  while (consumed < size) {
    size_t length_field_size = 0;
    uint64_t obu_total_size = 0;
    if (avm_uleb_decode(data + consumed, size - consumed, &obu_total_size,
                        &length_field_size) != 0)
      break;
    if (obu_total_size == 0 ||
        consumed + length_field_size + obu_total_size > size)
      break;

    const uint8_t *obu_start = data + consumed + length_field_size;
    ObuHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    parse_obu_header_byte(obu_start[0], &hdr);

    size_t obu_end = consumed + length_field_size + (size_t)obu_total_size;

    if (hdr.type == OBU_TEMPORAL_DELIMITER) {
      // Close previous segment if any
      if (in_segment && nseg < max_segs) {
        segs[nseg].offset = seg_start;
        segs[nseg].size = consumed - seg_start;
        segs[nseg].has_keyframe = has_kf;
        nseg++;
      }
      // Start new segment at this TD
      seg_start = consumed;
      has_kf = 0;
      in_segment = 1;
    } else if (hdr.type == OBU_CLOSED_LOOP_KEY) {
      has_kf = 1;
    }

    consumed = obu_end;
  }

  // Close last segment
  if (in_segment && consumed > seg_start && nseg < max_segs) {
    segs[nseg].offset = seg_start;
    segs[nseg].size = consumed - seg_start;
    segs[nseg].has_keyframe = has_kf;
    nseg++;
  }

  return nseg;
}

void tu_assembler_print_contents(const TUAssembler *ta, int tu_index) {
  const uint8_t *buf = ta->buffer;
  size_t buf_size = ta->size;
  fprintf(stdout, "--- TU %d [%zu bytes] ---\n", tu_index, buf_size);
  size_t pos = 0;
  while (pos < buf_size) {
    ObuHeader hdr;
    size_t payload_size = 0;
    size_t bytes_read = 0;
    if (avm_read_obu_header_and_size(buf + pos, buf_size - pos, &hdr,
                                     &payload_size,
                                     &bytes_read) != AVM_CODEC_OK)
      break;
    size_t obu_total = bytes_read + payload_size;
    fprintf(stdout, "  %-36s xl:%2d ml:%d tl:%d %4zu bytes\n",
            avm_obu_type_to_string(hdr.type), hdr.obu_xlayer_id,
            hdr.obu_mlayer_id, hdr.obu_tlayer_id, obu_total);
    pos += obu_total;
  }
}

// --- Structural OBU content population ---

// Derive configuration_idc from the highest chroma format among the given
// xlayer profiles. See Table A.1 in annexA.c:
//   0 = C_MAIN_420_10 (4:0:0, 4:2:0)
//   1 = C_MAIN_422_10 (4:0:0, 4:2:0, 4:2:2)
//   2 = C_MAIN_444_10 (4:0:0, 4:2:0, 4:4:4)
static int derive_config_idc_from_profiles(const MultiXLayerConfig *mcfg,
                                           uint32_t xlayer_map) {
  int config_idc = 0;  // C_MAIN_420_10
  for (int i = 0; i < mcfg->num_xlayers; i++) {
    int id = mcfg->xlayers[i].xlayer_id;
    if (!(xlayer_map & (1u << id))) continue;
    unsigned int prof = mcfg->xlayers[i].profile;
    if (prof == MAIN_444_10_IP1) {
      config_idc = 2;  // C_MAIN_444_10 — highest, can stop
      break;
    } else if (prof == MAIN_422_10_IP1 && config_idc < 1) {
      config_idc = 1;  // C_MAIN_422_10
    }
  }
  return config_idc;
}

// Derive the aggregate level index for a set of xlayers identified by
// xlayer_map.  The aggregate level is the smallest level whose constraints
// accommodate the combined resources of all constituent xlayers:
//   1. max_picture_size >= sum of all xlayers' picture sizes
//   2. max_decode_rate  >= sum of all xlayers' decode rates (pic_size * fps)
// When frame_rate is 0 (not specified), only picture size is checked.
static int derive_aggregate_level(const MultiXLayerConfig *mcfg,
                                  uint32_t xlayer_map) {
  int64_t total_picture_size = 0;
  int64_t total_decode_rate = 0;
  int max_individual_level = 0;
  double fps = mcfg->frame_rate;

  for (int i = 0; i < mcfg->num_xlayers; i++) {
    int id = mcfg->xlayers[i].xlayer_id;
    if (!(xlayer_map & (1u << id))) continue;
    int64_t pic_size =
        (int64_t)mcfg->xlayers[i].width * mcfg->xlayers[i].height;
    total_picture_size += pic_size;
    if (fps > 0) total_decode_rate += (int64_t)(pic_size * fps);
    if ((int)mcfg->xlayers[i].level > max_individual_level)
      max_individual_level = (int)mcfg->xlayers[i].level;
  }

  // Walk the level table and find the smallest level that satisfies all
  // constraints.  The aggregate level must also be >= every individual level.
  int agg_level = max_individual_level;
  for (int l = 0; l < SEQ_LEVELS; l++) {
    if (l < max_individual_level) continue;
    if (av2_level_defs[l].max_picture_size < total_picture_size) continue;
    if (fps > 0 && av2_level_defs[l].max_decode_rate < total_decode_rate)
      continue;
    agg_level = l;
    break;
  }
  return agg_level;
}

// Apply scaling mode to a dimension, returning the scaled size.
// Uses round-up division to match the encoder's internal scaling behavior.
void populate_global_lcr_from_config(const MultiXLayerConfig *mcfg,
                                     GlobalLayerConfigurationRecord *glcr) {
  memset(glcr, 0, sizeof(*glcr));

  glcr->lcr_global_config_record_id = 1;

  // Build xlayer_map bitmask and xlayer ID list
  uint32_t xlayer_map = 0;
  for (int i = 0; i < mcfg->num_xlayers; i++) {
    int id = mcfg->xlayers[i].xlayer_id;
    xlayer_map |= (1u << id);
    glcr->LcrXLayerID[i] = id;
  }
  glcr->lcr_xlayer_map = (int)xlayer_map;
  glcr->LcrMaxNumXLayerCount = mcfg->num_xlayers;

  glcr->lcr_global_payload_present_flag = 1;
  glcr->lcr_global_purpose_id = mcfg->lcr_purpose_id;
  glcr->lcr_dependent_xlayers_flag = mcfg->lcr_dependent_xlayers_flag;
  glcr->lcr_doh_constraint_flag = mcfg->lcr_doh_constraint_flag;
  glcr->lcr_seq_profile_tier_level_info_present_flag = 1;

  // Derive aggregate configuration_idc from all xlayers
  glcr->aggregate_ptl.lcr_config_idc =
      derive_config_idc_from_profiles(mcfg, (uint32_t)xlayer_map);

  // Populate per-xlayer info
  for (int i = 0; i < mcfg->num_xlayers; i++) {
    const XLayerEncConfig *xl = &mcfg->xlayers[i];
    LCRXLayerInfo *xinfo = &glcr->xlayer_info[i];

    // Representation info (resolution)
    xinfo->lcr_rep_info_present_flag = 1;
    xinfo->rep_params.lcr_max_pic_width = (int)xl->width;
    xinfo->rep_params.lcr_max_pic_height = (int)xl->height;

    // Color info
    if (xl->color_primaries >= 0) {
      xinfo->lcr_xlayer_color_info_present_flag = 1;
      xinfo->xlayer_col_params.layer_color_primaries = xl->color_primaries;
      xinfo->xlayer_col_params.layer_transfer_characteristics =
          xl->transfer_characteristics;
      xinfo->xlayer_col_params.layer_matrix_coefficients =
          xl->matrix_coefficients;
      xinfo->xlayer_col_params.layer_full_range_flag = xl->full_range_flag;
    }

    // Embedded layer info
    if (xl->num_embedded_layers > 1 || xl->num_temporal_layers > 1) {
      xinfo->lcr_embedded_layer_info_present_flag = 1;
      struct EmbeddedLayerInfo *ml = &xinfo->mlayer_params;
      ml->MLayerCount = xl->num_embedded_layers;
      // mlayer_map: bitmask of embedded layers present
      ml->lcr_mlayer_map = (1 << xl->num_embedded_layers) - 1;
      for (int m = 0; m < xl->num_embedded_layers; m++) {
        ml->LcrMlayerID[m] = m;
        ml->lcr_layer_type[m] = xl->layer_type;
        if (xl->layer_type == AUX_LAYER) {
          ml->lcr_auxiliary_type[m] = xl->auxiliary_type;
        }
        ml->lcr_view_type[m] = xl->view_type;
        ml->TLayerCount[m] = xl->num_temporal_layers;
        ml->lcr_tlayer_map[m] = (1 << xl->num_temporal_layers) - 1;
        // Set resolution flag based on scaling mode.
        // lcr_max_expected_width/height signals the maximum frame dimensions
        // that can appear for this mlayer.  For scaled layers, this must be
        // the xlayer's full resolution (not the scaled size) because the
        // encoder may produce full-res frames (e.g., on keyframes that reset
        // the resize state).
        int sm = xl->scaling_mode[m];
        if (sm != AVME_NORMAL) {
          ml->lcr_same_sh_max_resolution_flag[m] = 0;
          ml->lcr_max_expected_width[m] = (int)xl->width;
          ml->lcr_max_expected_height[m] = (int)xl->height;
        } else {
          ml->lcr_same_sh_max_resolution_flag[m] = 1;
        }
        // Populate dependency map from config
        ml->lcr_dependent_layer_map[m] =
            resolve_mlayer_dep_mask(&xl->mlayer_sources[m], m);
      }
    } else {
      // Single embedded layer, single temporal layer
      xinfo->lcr_embedded_layer_info_present_flag = 1;
      struct EmbeddedLayerInfo *ml = &xinfo->mlayer_params;
      ml->MLayerCount = 1;
      ml->lcr_mlayer_map = 1;
      ml->LcrMlayerID[0] = 0;
      ml->lcr_layer_type[0] = xl->layer_type;
      if (xl->layer_type == AUX_LAYER) {
        ml->lcr_auxiliary_type[0] = xl->auxiliary_type;
      }
      ml->lcr_view_type[0] = xl->view_type;
      ml->TLayerCount[0] = xl->num_temporal_layers;
      ml->lcr_tlayer_map[0] = (1 << xl->num_temporal_layers) - 1;
      ml->lcr_same_sh_max_resolution_flag[0] = 1;
    }

    // Seq profile/tier/level info
    glcr->seq_ptl[i].lcr_seq_profile_idc = xl->profile;
    glcr->seq_ptl[i].lcr_max_level_idx = xl->level;
    glcr->seq_ptl[i].lcr_tier_flag = xl->tier;
  }

  // Derive aggregate level and tier from all xlayers
  {
    int max_tier = 0;
    for (int i = 0; i < mcfg->num_xlayers; i++) {
      if ((int)mcfg->xlayers[i].tier > max_tier)
        max_tier = (int)mcfg->xlayers[i].tier;
    }
    glcr->aggregate_ptl.lcr_aggregate_level_idx =
        derive_aggregate_level(mcfg, (uint32_t)xlayer_map);
    glcr->aggregate_ptl.lcr_max_tier_flag = max_tier;
  }
}

void populate_ops_from_config(const OPSConfig *ops_cfg, int xlayer_id,
                              const MultiXLayerConfig *mcfg,
                              OperatingPointSet *ops) {
  memset(ops, 0, sizeof(*ops));
  if (!ops_cfg->enable) return;

  ops->valid = 1;
  ops->obu_xlayer_id = xlayer_id;
  ops->ops_id = ops_cfg->ops_id;
  ops->ops_cnt = ops_cfg->num_operating_points;
  ops->ops_priority = ops_cfg->priority;
  ops->ops_intent_present_flag = ops_cfg->intent_present_flag;
  ops->ops_ptl_present_flag = ops_cfg->ptl_present_flag;
  ops->ops_color_info_present_flag = ops_cfg->color_info_present_flag;
  ops->ops_mlayer_info_idc = ops_cfg->mlayer_info_idc;

  for (int p = 0; p < ops_cfg->num_operating_points; p++) {
    const OperatingPointConfig *opc = &ops_cfg->ops[p];
    OperatingPoint *op = &ops->op[p];

    op->ops_intent_op = opc->intent;
    op->ops_xlayer_map = (int)opc->xlayer_map;
    op->ops_initial_display_delay =
        BUFFER_POOL_MAX_SIZE;  // default: not present

    // Derive XCount and OpsxLayerID from xlayer_map
    op->XCount = 0;
    for (int bit = 0; bit < (int)(MAX_NUM_XLAYERS - 1); bit++) {
      if (opc->xlayer_map & (1u << bit)) {
        op->OpsxLayerID[op->XCount] = bit;
        op->XCount++;
      }
    }

    // Per-xlayer mlayer counts and map derivation
    for (int x = 0; x < op->XCount; x++) {
      int xl = op->OpsxLayerID[x];
      int ml_count = opc->mlayer_count[x];
      op->ops_mlayer_count[xl] = ml_count;
      // Derive ops_mlayer_map: include the first ml_count mlayers
      if (ml_count > 0 && ops->ops_mlayer_info_idc >= 1) {
        op->mlayer_info.ops_mlayer_map[xl] = (1 << ml_count) - 1;
        // Default: all temporal layers for each included mlayer
        for (int m = 0; m < ml_count; m++) {
          // Find the xlayer config to get num_temporal_layers
          int tl_count = 1;
          for (int j = 0; j < mcfg->num_xlayers; j++) {
            if (mcfg->xlayers[j].xlayer_id == xl) {
              tl_count = mcfg->xlayers[j].num_temporal_layers;
              break;
            }
          }
          op->mlayer_info.ops_tlayer_map[xl][m] = (1 << tl_count) - 1;
        }
        // For idc==2, use explicit info (not embedded OPS references)
        if (ops->ops_mlayer_info_idc == 2) {
          op->ops_mlayer_explicit_info_flag[xl] = 1;
        }
      }
    }

    // Derive ops_config_idc from the profiles of constituent xlayers
    op->ops_config_idc = derive_config_idc_from_profiles(mcfg, opc->xlayer_map);

    // Aggregate level/tier
    if (opc->aggregate_level_idx >= 0) {
      op->ops_aggregate_level_idx = opc->aggregate_level_idx;
    } else {
      // Derive: find smallest level accommodating summed picture sizes
      op->ops_aggregate_level_idx =
          derive_aggregate_level(mcfg, opc->xlayer_map);
    }

    if (opc->max_tier_flag >= 0) {
      op->ops_max_tier_flag = opc->max_tier_flag;
    } else {
      // Derive: max tier across constituent xlayers
      for (int x = 0; x < op->XCount; x++) {
        int xl_id = op->OpsxLayerID[x];
        for (int j = 0; j < mcfg->num_xlayers; j++) {
          if (mcfg->xlayers[j].xlayer_id == xl_id) {
            if ((int)mcfg->xlayers[j].tier > op->ops_max_tier_flag)
              op->ops_max_tier_flag = (int)mcfg->xlayers[j].tier;
            break;
          }
        }
      }
    }

    // Embedded OPS references
    for (int x = 0; x < MAX_NUM_XLAYERS; x++) {
      op->ops_embedded_ops_id[x] = opc->embedded_ops_id[x];
      op->ops_embedded_op_index[x] = opc->embedded_op_index[x];
    }
  }
}

void populate_atlas_from_config(const MultiXLayerConfig *mcfg,
                                AtlasSegmentInfo *atlas) {
  memset(atlas, 0, sizeof(*atlas));
  if (!mcfg->enable_atlas) return;

  atlas->valid = 1;
  atlas->obu_xlayer_id = GLOBAL_XLAYER_ID;
  atlas->atlas_segment_id = 1;
  atlas->atlas_segment_mode_idc = mcfg->atlas_mode;

  const int n = mcfg->num_xlayers;

  if (mcfg->atlas_mode == ENHANCED_ATLAS) {
    // Enhanced Atlas: region grid from xlayer count/dimensions
    struct AtlasRegionInfo *reg = &atlas->ats_reg_params;

    if (mcfg->atlas_uniform_spacing) {
      // Auto-grid: N columns x 1 row
      reg->ats_uniform_spacing_flag = 1;
      reg->ats_num_region_columns_minus_1 = n - 1;
      reg->ats_num_region_rows_minus_1 = 0;

      // Use first xlayer's dimensions as the uniform region size
      reg->ats_region_width_minus_1 = (int)mcfg->xlayers[0].width - 1;
      reg->ats_region_height_minus_1 = (int)mcfg->xlayers[0].height - 1;
      reg->NumRegionsInAtlas = n;

      // Derive atlas dimensions
      reg->AtlasWidth = (int)mcfg->xlayers[0].width * n;
      reg->AtlasHeight = (int)mcfg->xlayers[0].height;

      // Single region per atlas segment (one xlayer per region)
      atlas->ats_reg_seg_map.ats_single_region_per_atlas_segment_flag = 1;
      atlas->ats_reg_seg_map.ats_num_atlas_segments_minus_1 = n - 1;
    } else {
      // Explicit positions: derive grid from per-xlayer atlas_pos_x/y.
      // Collect unique X and Y boundaries to determine columns and rows.
      reg->ats_uniform_spacing_flag = 0;

      // Collect unique column start positions and widths
      int col_x[MAX_NUM_XLAYERS];
      int col_w[MAX_NUM_XLAYERS];
      int num_cols = 0;
      int row_y[MAX_NUM_XLAYERS];
      int row_h[MAX_NUM_XLAYERS];
      int num_rows = 0;

      for (int i = 0; i < n; i++) {
        int px = mcfg->xlayers[i].atlas_pos_x >= 0
                     ? mcfg->xlayers[i].atlas_pos_x
                     : 0;
        int py = mcfg->xlayers[i].atlas_pos_y >= 0
                     ? mcfg->xlayers[i].atlas_pos_y
                     : 0;
        int w = (int)mcfg->xlayers[i].width;
        int h = (int)mcfg->xlayers[i].height;

        // Insert unique column
        int found = 0;
        for (int c = 0; c < num_cols; c++) {
          if (col_x[c] == px) {
            found = 1;
            break;
          }
        }
        if (!found) {
          col_x[num_cols] = px;
          col_w[num_cols] = w;
          num_cols++;
        }

        // Insert unique row
        found = 0;
        for (int r = 0; r < num_rows; r++) {
          if (row_y[r] == py) {
            found = 1;
            break;
          }
        }
        if (!found) {
          row_y[num_rows] = py;
          row_h[num_rows] = h;
          num_rows++;
        }
      }

      // Sort columns by X position (simple insertion sort)
      for (int i = 1; i < num_cols; i++) {
        int kx = col_x[i], kw = col_w[i];
        int j = i - 1;
        while (j >= 0 && col_x[j] > kx) {
          col_x[j + 1] = col_x[j];
          col_w[j + 1] = col_w[j];
          j--;
        }
        col_x[j + 1] = kx;
        col_w[j + 1] = kw;
      }

      // Sort rows by Y position
      for (int i = 1; i < num_rows; i++) {
        int ky = row_y[i], kh = row_h[i];
        int j = i - 1;
        while (j >= 0 && row_y[j] > ky) {
          row_y[j + 1] = row_y[j];
          row_h[j + 1] = row_h[j];
          j--;
        }
        row_y[j + 1] = ky;
        row_h[j + 1] = kh;
      }

      reg->ats_num_region_columns_minus_1 = num_cols - 1;
      reg->ats_num_region_rows_minus_1 = num_rows - 1;
      for (int c = 0; c < num_cols; c++)
        reg->ats_column_width_minus_1[c] = col_w[c] - 1;
      for (int r = 0; r < num_rows; r++)
        reg->ats_row_height_minus_1[r] = row_h[r] - 1;
      reg->NumRegionsInAtlas = num_cols * num_rows;

      // Use explicit region-to-segment mapping since not all grid cells
      // may be occupied (e.g., 3 regions in a 2x2 grid).
      atlas->ats_reg_seg_map.ats_single_region_per_atlas_segment_flag = 0;
      atlas->ats_reg_seg_map.ats_num_atlas_segments_minus_1 = n - 1;

      // Map each xlayer to its grid cell
      for (int i = 0; i < n; i++) {
        int px = mcfg->xlayers[i].atlas_pos_x >= 0
                     ? mcfg->xlayers[i].atlas_pos_x
                     : 0;
        int py = mcfg->xlayers[i].atlas_pos_y >= 0
                     ? mcfg->xlayers[i].atlas_pos_y
                     : 0;
        int col_idx = 0, row_idx = 0;
        for (int c = 0; c < num_cols; c++) {
          if (col_x[c] == px) {
            col_idx = c;
            break;
          }
        }
        for (int r = 0; r < num_rows; r++) {
          if (row_y[r] == py) {
            row_idx = r;
            break;
          }
        }
        atlas->ats_reg_seg_map.ats_top_left_region_column[i] = col_idx;
        atlas->ats_reg_seg_map.ats_top_left_region_row[i] = row_idx;
        atlas->ats_reg_seg_map.ats_bottom_right_region_column_offset[i] = 0;
        atlas->ats_reg_seg_map.ats_bottom_right_region_row_offset[i] = 0;
        // Derived fields
        atlas->ats_reg_seg_map.ats_bottom_right_region_column[i] = col_idx;
        atlas->ats_reg_seg_map.ats_bottom_right_region_row[i] = row_idx;
      }
    }

    // No signalled segment IDs
    atlas->ats_label_seg.ats_signalled_atlas_segment_ids_flag = 0;

  } else if (mcfg->atlas_mode == MULTISTREAM_ATLAS) {
    // Multistream Atlas: per-segment positions from xlayer config
    struct AtlasBasicInfo *basic = &atlas->ats_basic_info_s;
    atlas->ats_basic_info = basic;

    basic->ats_stream_id_present = 1;
    basic->ats_num_atlas_segments_minus_1 = n - 1;

    // Derive or use explicit atlas dimensions
    if (mcfg->atlas_width > 0) {
      basic->ats_atlas_width = mcfg->atlas_width;
      basic->ats_atlas_height = mcfg->atlas_height;
    } else {
      // Auto-derive: horizontal tiling
      int total_w = 0;
      int max_h = 0;
      for (int i = 0; i < n; i++) {
        total_w += (int)mcfg->xlayers[i].width;
        if ((int)mcfg->xlayers[i].height > max_h)
          max_h = (int)mcfg->xlayers[i].height;
      }
      basic->ats_atlas_width = total_w;
      basic->ats_atlas_height = max_h;
    }
    basic->AtlasWidth = basic->ats_atlas_width;
    basic->AtlasHeight = basic->ats_atlas_height;

    // Per-segment info
    int auto_x = 0;
    for (int i = 0; i < n; i++) {
      basic->ats_input_stream_id[i] = mcfg->xlayers[i].xlayer_id;
      basic->ats_segment_width[i] = (int)mcfg->xlayers[i].width;
      basic->ats_segment_height[i] = (int)mcfg->xlayers[i].height;

      if (mcfg->xlayers[i].atlas_pos_x >= 0) {
        basic->ats_segment_top_left_pos_x[i] = mcfg->xlayers[i].atlas_pos_x;
        basic->ats_segment_top_left_pos_y[i] = mcfg->xlayers[i].atlas_pos_y;
      } else {
        // Auto-place: horizontal tiling
        basic->ats_segment_top_left_pos_x[i] = auto_x;
        basic->ats_segment_top_left_pos_y[i] = 0;
      }
      auto_x += (int)mcfg->xlayers[i].width;
    }

    // No signalled segment IDs
    atlas->ats_label_seg.ats_signalled_atlas_segment_ids_flag = 0;
  }
}
