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

#include "aom/aom_codec.h"
#include "aom/aom_integer.h"
#include "aom_ports/mem_ops.h"
#include "av1/common/obu_util.h"
#include "tools/obu_parser.h"

namespace aom_tools {

#if CONFIG_NEW_OBU_HEADER
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
#else
// Basic OBU syntax
// 8 bits: Header
//   7
//     forbidden bit
//   6,5,4,3
//     type bits
//   2
//     extension flag bit
//   1
//     has size field bit
//   0
//     reserved bit
const uint32_t kObuForbiddenBitMask = 0x1;
const uint32_t kObuForbiddenBitShift = 7;
const uint32_t kObuTypeBitsMask = 0xF;
const uint32_t kObuTypeBitsShift = 3;
const uint32_t kObuExtensionFlagBitMask = 0x1;
const uint32_t kObuExtensionFlagBitShift = 2;
const uint32_t kObuHasSizeFieldBitMask = 0x1;
const uint32_t kObuHasSizeFieldBitShift = 1;
#endif  // CONFIG_NEW_OBU_HEADER

#if CONFIG_NEW_OBU_HEADER
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
#else
// When extension flag bit is set:
// 8 bits: extension header
// 7,6,5
//   temporal ID
// 4,3
//   spatial ID
// 2,1,0
//   reserved bits
const uint32_t kObuExtTemporalIdBitsMask = 0x7;
const uint32_t kObuExtTemporalIdBitsShift = 5;
const uint32_t kObuExtSpatialIdBitsMask = 0x3;
const uint32_t kObuExtSpatialIdBitsShift = 3;
#endif  // CONFIG_NEW_OBU_HEADER

bool ValidObuType(int obu_type) {
  switch (obu_type) {
    case OBU_SEQUENCE_HEADER:
    case OBU_TEMPORAL_DELIMITER:
#if CONFIG_F106_OBU_TILEGROUP
#if CONFIG_F106_OBU_SWITCH
    case OBU_SWITCH:
#endif  // CONFIG_F106_OBU_SWITCH
#if CONFIG_F106_OBU_SEF
    case OBU_SEF:
#endif  // CONFIG_F106_OBU_SEF
#if CONFIG_F106_OBU_TIP
    case OBU_TIP:
#endif  // CONFIG_F106_OBU_TIP
    case OBU_TILE_GROUP:
#else
    case OBU_FRAME_HEADER:
    case OBU_TILE_GROUP:
#endif  // CONFIG_F106_OBU_TILEGROUP
    case OBU_METADATA:
#if !CONFIG_F106_OBU_TILEGROUP
    case OBU_FRAME:
    case OBU_REDUNDANT_FRAME_HEADER:
#endif  // CONFIG_F106_OBU_TILEGROUP
    case OBU_PADDING: return true;
  }
  return false;
}

bool ParseObuHeader(uint8_t obu_header_byte, ObuHeader *obu_header) {
#if CONFIG_NEW_OBU_HEADER
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
#else
  const int forbidden_bit =
      (obu_header_byte >> kObuForbiddenBitShift) & kObuForbiddenBitMask;
  if (forbidden_bit) {
    fprintf(stderr, "Invalid OBU, forbidden bit set.\n");
    return false;
  }

  obu_header->type = static_cast<OBU_TYPE>(
      (obu_header_byte >> kObuTypeBitsShift) & kObuTypeBitsMask);
  if (!ValidObuType(obu_header->type)) {
    fprintf(stderr, "Invalid OBU type: %d.\n", obu_header->type);
    return false;
  }

  obu_header->has_extension =
      (obu_header_byte >> kObuExtensionFlagBitShift) & kObuExtensionFlagBitMask;
  obu_header->has_size_field =
      (obu_header_byte >> kObuHasSizeFieldBitShift) & kObuHasSizeFieldBitMask;
#endif  // CONFIG_NEW_OBU_HEADER
  return true;
}

bool ParseObuExtensionHeader(uint8_t ext_header_byte, ObuHeader *obu_header) {
#if CONFIG_NEW_OBU_HEADER
  obu_header->obu_mlayer_id =
      (ext_header_byte >> kObuExtMlayerIdBitsShift) & kObuExtMlayerIdBitsMask;
  obu_header->obu_xlayer_id =
      (ext_header_byte >> kObuExtXlayerIdBitsShift) & kObuExtXlayerIdBitsMask;
#else
  obu_header->temporal_layer_id =
      (ext_header_byte >> kObuExtTemporalIdBitsShift) &
      kObuExtTemporalIdBitsMask;
  obu_header->spatial_layer_id =
      (ext_header_byte >> kObuExtSpatialIdBitsShift) & kObuExtSpatialIdBitsMask;
#endif  // CONFIG_NEW_OBU_HEADER

  return true;
}

void PrintObuHeader(const ObuHeader *header) {
#if CONFIG_NEW_OBU_HEADER
  printf(
      "  OBU extension: %s\n"
      "      type:      %s\n"
      "      tlayer_id: %d\n",
      header->obu_extension_flag ? "yes" : "no",
      aom_obu_type_to_string(static_cast<OBU_TYPE>(header->type)),
      header->obu_tlayer_id);
  if (header->obu_extension_flag) {
    printf(
        "      mlayer_id: %d\n"
        "      xlayer_id: %d\n",
        header->obu_mlayer_id, header->obu_xlayer_id);
  }
#else
  printf(
      "  OBU type:        %s\n"
      "      extension:   %s\n",
      aom_obu_type_to_string(static_cast<OBU_TYPE>(header->type)),
      header->has_extension ? "yes" : "no");
  if (header->has_extension) {
    printf(
        "      temporal_id: %d\n"
        "      spatial_id:  %d\n",
        header->temporal_layer_id, header->temporal_layer_id);
  }
#endif  // CONFIG_NEW_OBU_HEADER
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

#if CONFIG_NEW_OBU_HEADER
    uint64_t obu_size = 0;
    size_t length_field_size = 0;
    if (aom_uleb_decode(data + consumed, remaining, &obu_size,
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
#else
    int obu_header_size = 0;
#endif  // CONFIG_NEW_OBU_HEADER

    memset(&obu_header, 0, sizeof(obu_header));
#if CONFIG_NEW_OBU_HEADER
    const uint8_t obu_header_byte = *(data + consumed + length_field_size);
#else
    const uint8_t obu_header_byte = *(data + consumed);
#endif  // CONFIG_NEW_OBU_HEADER
    if (!ParseObuHeader(obu_header_byte, &obu_header)) {
#if CONFIG_NEW_OBU_HEADER
      fprintf(stderr, "OBU parsing failed at offset %d.\n",
              static_cast<int>(consumed + length_field_size));
#else
      fprintf(stderr, "OBU parsing failed at offset %d.\n", consumed);
#endif  // CONFIG_NEW_OBU_HEADER
      return false;
    }

    ++obu_overhead;
#if !CONFIG_NEW_OBU_HEADER
    ++obu_header_size;
#endif  // !CONFIG_NEW_OBU_HEADER

#if CONFIG_NEW_OBU_HEADER
    if (obu_header.obu_extension_flag) {
      if (current_obu_length < kObuHeaderSizeBytes + 1) {
        fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
        return false;
      }
      const uint8_t obu_ext_header_byte =
          *(data + consumed + length_field_size + kObuHeaderSizeBytes);
#else
    if (obu_header.has_extension) {
      const uint8_t obu_ext_header_byte =
          *(data + consumed + kObuHeaderSizeBytes);
#endif  // CONFIG_NEW_OBU_HEADER
      if (!ParseObuExtensionHeader(obu_ext_header_byte, &obu_header)) {
        fprintf(stderr, "OBU extension parsing failed at offset %d.\n",
#if CONFIG_NEW_OBU_HEADER
                static_cast<int>(consumed + length_field_size +
                                 kObuHeaderSizeBytes));
#else
                consumed + kObuHeaderSizeBytes);
#endif  // CONFIG_NEW_OBU_HEADER
        return false;
      }

      ++obu_overhead;
#if !CONFIG_NEW_OBU_HEADER
      ++obu_header_size;
#endif  // !CONFIG_NEW_OBU_HEADER
    }

    PrintObuHeader(&obu_header);

#if CONFIG_NEW_OBU_HEADER
    consumed += static_cast<int>(length_field_size) + current_obu_length;
    printf("      length:    %d\n",
           static_cast<int>(length_field_size + current_obu_length));
#else
    uint64_t obu_size = 0;
    size_t length_field_size = 0;
    if (aom_uleb_decode(data + consumed + obu_header_size,
                        remaining - obu_header_size, &obu_size,
                        &length_field_size) != 0) {
      fprintf(stderr, "OBU size parsing failed at offset %d.\n",
              consumed + obu_header_size);
      return false;
    }
    int current_obu_length = static_cast<int>(obu_size);
    if (obu_header_size + static_cast<int>(length_field_size) +
            current_obu_length >
        remaining) {
      fprintf(stderr, "OBU parsing failed: not enough OBU data.\n");
      return false;
    }
    consumed += obu_header_size + static_cast<int>(length_field_size) +
                current_obu_length;
    printf("      length:      %d\n",
           static_cast<int>(obu_header_size + length_field_size +
                            current_obu_length));
#endif  // CONFIG_NEW_OBU_HEADER
  }

  if (obu_overhead_bytes != nullptr) *obu_overhead_bytes = obu_overhead;
  printf("  TU size: %d\n", consumed);

  return true;
}

}  // namespace aom_tools
