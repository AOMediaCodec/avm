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
#include <string.h>

#include <cstdio>
#include <string>

#include "avm/avm_codec.h"
#include "avm/avm_integer.h"
#include "avm_ports/mem_ops.h"
#include "av2/common/obu_util.h"
#include "tools/obu_parser.h"

namespace avm_tools {

// Basic OBU syntax
// 8 bits: Header
//   7
//     extension flag bit
//   6,5,4,3,2
//     type bits
//   1,0
//     tlayer ID
const uint32_t kObuExtensionFlagBitMask = 0x1;
const uint32_t kObuExtensionFlagBitShift = 7;
const uint32_t kObuTypeBitsMask = 0x1F;
const uint32_t kObuTypeBitsShift = 2;
const uint32_t kObuExtTlayerIdBitsMask = 0x3;
const uint32_t kObuExtTlayerIdBitsShift = 0;

// When extension flag bit is set:
// 8 bits: extension header
// 7,6,5
//   mlayer ID
// 4,3,2,1,0
//   xlayer ID
const uint32_t kObuExtMlayerIdBitsMask = 0x7;
const uint32_t kObuExtMlayerIdBitsShift = 5;
const uint32_t kObuExtXlayerIdBitsMask = 0x1F;
const uint32_t kObuExtXlayerIdBitsShift = 0;

bool ValidObuType(int obu_type) {
  switch (obu_type) {
    case OBU_SEQUENCE_HEADER:
    case OBU_TEMPORAL_DELIMITER:
    case OBU_MULTI_FRAME_HEADER:
    case OBU_CLK:
    case OBU_OLK:
    case OBU_LEADING_TILE_GROUP:
    case OBU_REGULAR_TILE_GROUP:
    case OBU_SWITCH:
    case OBU_LEADING_SEF:
    case OBU_REGULAR_SEF:
    case OBU_LEADING_TIP:
    case OBU_REGULAR_TIP:
    case OBU_METADATA_SHORT:
    case OBU_METADATA_GROUP:
    case OBU_BUFFER_REMOVAL_TIMING:
    case OBU_LAYER_CONFIGURATION_RECORD:
    case OBU_ATLAS_SEGMENT:
    case OBU_OPERATING_POINT_SET:
    case OBU_MSDO:
    case OBU_RAS_FRAME:
    case OBU_QM:
    case OBU_FGM:
#if CONFIG_CWG_F270_CI_OBU
    case OBU_CONTENT_INTERPRETATION:
#endif  // CONFIG_CWG_F270_CI_OBU
    case OBU_PADDING: return true;
  }
  return false;
}

bool ParseObuHeader(uint8_t obu_header_byte, ObuHeader *obu_header) {
  obu_header->obu_extension_flag =
      (obu_header_byte >> kObuExtensionFlagBitShift) & kObuExtensionFlagBitMask;
  obu_header->type = static_cast<OBU_TYPE>(
      (obu_header_byte >> kObuTypeBitsShift) & kObuTypeBitsMask);
  if (!ValidObuType(obu_header->type)) {
    fprintf(stderr, "Invalid OBU type: %d.\n", obu_header->type);
    return false;
  }

  obu_header->obu_tlayer_id =
      (obu_header_byte >> kObuExtTlayerIdBitsShift) & kObuExtTlayerIdBitsMask;
  return true;
}

bool ParseObuExtensionHeader(uint8_t ext_header_byte, ObuHeader *obu_header) {
  obu_header->obu_mlayer_id =
      (ext_header_byte >> kObuExtMlayerIdBitsShift) & kObuExtMlayerIdBitsMask;
  obu_header->obu_xlayer_id =
      (ext_header_byte >> kObuExtXlayerIdBitsShift) & kObuExtXlayerIdBitsMask;

  return true;
}

void PrintObuHeader(const ObuHeader *header, bool first_tile_group_in_frame) {
  printf(
      "  OBU extension: %s\n"
      "      type:      %s%s\n"
      "      tlayer_id: %d\n",
      header->obu_extension_flag ? "yes" : "no",
      avm_obu_type_to_string(static_cast<OBU_TYPE>(header->type)),
      first_tile_group_in_frame ? " (new frame)" : "", header->obu_tlayer_id);
  if (header->obu_extension_flag) {
    printf(
        "      mlayer_id: %d\n"
        "      xlayer_id: %d\n",
        header->obu_mlayer_id, header->obu_xlayer_id);
  }
}

bool DumpObu(const uint8_t *data, int length, int *obu_overhead_bytes) {
  const int kObuHeaderSizeBytes = 1;
  const int kMinimumBytesRequired = 1 + kObuHeaderSizeBytes;
  int consumed = 0;
  int obu_overhead = 0;
  ObuHeader obu_header;
  while (consumed < length) {
    const int remaining = length - consumed;
    if (remaining < kMinimumBytesRequired) {
      fprintf(stderr,
              "OBU parse error. Did not consume all data, %d bytes remain.\n",
              remaining);
      return false;
    }

    uint64_t obu_size = 0;
    size_t length_field_size = 0;
    if (avm_uleb_decode(data + consumed, remaining, &obu_size,
                        &length_field_size) != 0) {
      fprintf(stderr, "OBU size parsing failed at offset %d.\n", consumed);
      return false;
    }
    int current_obu_length = static_cast<int>(obu_size);
    if (static_cast<int>(length_field_size) + current_obu_length > remaining ||
        current_obu_length < kObuHeaderSizeBytes) {
      fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
      return false;
    }

    memset(&obu_header, 0, sizeof(obu_header));
    const uint8_t obu_header_byte = *(data + consumed + length_field_size);
    if (!ParseObuHeader(obu_header_byte, &obu_header)) {
      fprintf(stderr, "OBU parsing failed at offset %d.\n",
              static_cast<int>(consumed + length_field_size));
      return false;
    }

    ++obu_overhead;

    if (obu_header.obu_extension_flag) {
      if (current_obu_length < kObuHeaderSizeBytes + 1) {
        fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
        return false;
      }
      const uint8_t obu_ext_header_byte =
          *(data + consumed + length_field_size + kObuHeaderSizeBytes);
      if (!ParseObuExtensionHeader(obu_ext_header_byte, &obu_header)) {
        fprintf(stderr, "OBU extension parsing failed at offset %d.\n",
                static_cast<int>(consumed + length_field_size +
                                 kObuHeaderSizeBytes));
        return false;
      }

      ++obu_overhead;
    }

    bool is_tile_group = obu_header.type == OBU_LEADING_TILE_GROUP ||
                         obu_header.type == OBU_REGULAR_TILE_GROUP ||
                         obu_header.type == OBU_CLK ||
                         obu_header.type == OBU_OLK;
    is_tile_group = is_tile_group || obu_header.type == OBU_SWITCH;
    is_tile_group = is_tile_group || obu_header.type == OBU_BRIDGE_FRAME;
    bool first_tile_group_in_frame = false;
    if (is_tile_group) {
      if (current_obu_length <
          kObuHeaderSizeBytes + obu_header.obu_extension_flag + 1) {
        fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
        return false;
      }
      const uint8_t tile_group_header_first_byte =
          *(data + consumed + length_field_size + kObuHeaderSizeBytes +
            obu_header.obu_extension_flag);
      first_tile_group_in_frame = (tile_group_header_first_byte >> 7) != 0;
    }
    PrintObuHeader(&obu_header, first_tile_group_in_frame);

    consumed += static_cast<int>(length_field_size) + current_obu_length;
    printf("      length:    %d\n",
           static_cast<int>(length_field_size) + current_obu_length);
  }

  if (obu_overhead_bytes != nullptr) *obu_overhead_bytes = obu_overhead;
  printf("  TU size: %d\n", consumed);

  return true;
}

}  // namespace avm_tools
