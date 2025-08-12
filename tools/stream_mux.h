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

#ifndef STREAM_MUX_H_
#define STREAM_MUX_H_

#include <stdlib.h>
#include <string.h>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>

#include "config/aom_config.h"
#include "common/ivfdec.h"
#include "common/obudec.h"
#include "common/tools_common.h"
#include "common/webmdec.h"
#include "av1/common/obu_util.h"
#include "av1/common/blockd.h"
#include "aom_dsp/bitwriter_buffer.h"
#include "aom_dsp/bitreader_buffer.h"
#include "aom/aom_codec.h"

#define PRINT_TU_INFO 0

#define MAX_NUM_STREAMS 4

const size_t kInitialBufferSize = 100 * 1024;

/*!\brief OBU types. */

struct InputContext {
  InputContext() = default;
  ~InputContext() { free(unit_buffer); }

  void Init() {
    memset(avx_ctx, 0, sizeof(*avx_ctx));
    memset(obu_ctx, 0, sizeof(*obu_ctx));
    obu_ctx->avx_ctx = avx_ctx;
#if CONFIG_WEBM_IO
    memset(webm_ctx, 0, sizeof(*webm_ctx));
#endif
#if CONFIG_F159_OBUSIZE_ANNEXB
    obu_ctx->is_annexb = 1;
#endif  // CONFIG_F159_OBUSIZE_ANNEXB
  }

  AvxInputContext *avx_ctx = nullptr;
  ObuDecInputContext *obu_ctx = nullptr;
#if CONFIG_WEBM_IO
  WebmInputContext *webm_ctx = nullptr;
#endif
  uint8_t *unit_buffer = nullptr;
  size_t unit_buffer_size = 0;
};

VideoFileType GetFileType(InputContext *ctx) {
  if (file_is_ivf(ctx->avx_ctx)) return FILE_TYPE_IVF;
  if (file_is_obu(ctx->obu_ctx)) return FILE_TYPE_OBU;
#if CONFIG_WEBM_IO
  if (file_is_webm(ctx->webm_ctx, ctx->avx_ctx)) return FILE_TYPE_WEBM;
#endif
  return FILE_TYPE_RAW;
}

bool ReadTemporalUnit(InputContext *ctx, size_t *unit_size) {
  const VideoFileType file_type = ctx->avx_ctx->file_type;
  switch (file_type) {
    case FILE_TYPE_IVF: {
      if (ivf_read_frame(ctx->avx_ctx->file, &ctx->unit_buffer, unit_size,
                         &ctx->unit_buffer_size, NULL)) {
        return false;
      }
      break;
    }
    case FILE_TYPE_OBU: {
      if (obudec_read_temporal_unit(ctx->obu_ctx, &ctx->unit_buffer, unit_size,
                                    &ctx->unit_buffer_size)) {
        return false;
      }
      break;
    }
#if CONFIG_WEBM_IO
    case FILE_TYPE_WEBM: {
      if (webm_read_frame(ctx->webm_ctx, &ctx->unit_buffer, unit_size,
                          &ctx->unit_buffer_size)) {
        return false;
      }
      break;
    }
#endif
    default: fprintf(stderr, "Error: Unsupported file type.\n"); return false;
  }

  return true;
}

bool ValidObuType(int obu_type) {
  switch (obu_type) {
    case OBU_SEQUENCE_HEADER:
    case OBU_TEMPORAL_DELIMITER:
    case OBU_FRAME_HEADER:
    case OBU_TILE_GROUP:
    case OBU_METADATA:
    case OBU_FRAME:
    case OBU_REDUNDANT_FRAME_HEADER:
    case OBU_TILE_LIST:
    case OBU_MULTI_STREAM_HEADER:
    case OBU_PADDING: return true;
  }
  return false;
}

void PrintObuHeader(const ObuHeader *header) {
  printf(
      "      OBU type:  %s\n"
      "      obu_tlayer_id: %d\n"
      "      obu_mlayer_id: %d\n"
      "      obu_xlayer_id:  %d\n",
      aom_obu_type_to_string(static_cast<OBU_TYPE>(header->type)),
      header->obu_tlayer_id, header->obu_mlayer_id, header->obu_xlayer_id);
}

// Initialize a read bit buffer
struct aom_read_bit_buffer *init_read_bit_buffer(struct aom_read_bit_buffer *rb,
                                                 const uint8_t *data,
                                                 const uint8_t *data_end) {
  rb->bit_offset = 0;
  rb->error_handler = NULL;
  rb->error_handler_data = NULL;
  rb->bit_buffer = data;
  rb->bit_buffer_end = data_end;
  return rb;
}

#if CONFIG_F159_OBU_HEADER
const uint32_t kObuTypeBitsMask = 0xF;
const uint32_t kObuTypeBitsShift = 4;
const uint32_t kObuTLayerIdBitsMask = 0x7;
const uint32_t kObuMLayerIdBitsMask = 0x7;
const uint32_t kObuMLayerIdBitsShift = 5;
const uint32_t kObuXLayerIdBitsMask = 0x1F;

bool ParseAV2ObuHeader(uint8_t obu_header_first_byte,
                       uint8_t obu_header_second_byte, ObuHeader *obu_header) {
  obu_header->type = static_cast<OBU_TYPE>(
      (obu_header_first_byte >> kObuTypeBitsShift) & kObuTypeBitsMask);
  if (!ValidObuType(obu_header->type)) {
    fprintf(stderr, "Invalid OBU type: %d.\n", obu_header->type);
    return false;
  }
  obu_header->obu_tlayer_id = obu_header_first_byte & kObuTLayerIdBitsMask;
  obu_header->obu_mlayer_id =
      (obu_header_second_byte >> kObuMLayerIdBitsShift) & kObuMLayerIdBitsMask;
  obu_header->obu_xlayer_id = obu_header_second_byte & kObuXLayerIdBitsMask;
  return true;
}
#else   // CONFIG_F159_OBU_HEADER
const uint32_t kObuForbiddenBitMask = 0x1;
const uint32_t kObuForbiddenBitShift = 7;
const uint32_t kObuTypeBitsMask = 0xF;
const uint32_t kObuTypeBitsShift = 3;
const uint32_t kObuExtensionFlagBitMask = 0x1;
const uint32_t kObuExtensionFlagBitShift = 2;
const uint32_t kObuHasSizeFieldBitMask = 0x1;
const uint32_t kObuHasSizeFieldBitShift = 1;
const uint32_t kObuExtTemporalIdBitsMask = 0x7;
const uint32_t kObuExtTemporalIdBitsShift = 5;
const uint32_t kObuExtSpatialIdBitsMask = 0x3;
const uint32_t kObuExtSpatialIdBitsShift = 3;

bool ParseAV2ObuHeader(uint8_t obu_header_byte, ObuHeader *obu_header) {
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
  return true;
}

bool ParseAV2ObuExtensionHeader(uint8_t ext_header_byte,
                                ObuHeader *obu_header) {
  obu_header->temporal_layer_id =
      (ext_header_byte >> kObuExtTemporalIdBitsShift) &
      kObuExtTemporalIdBitsMask;
  obu_header->spatial_layer_id =
      (ext_header_byte >> kObuExtSpatialIdBitsShift) & kObuExtSpatialIdBitsMask;
  obu_header->stream_id = 0;

  return true;
}
#endif  // CONFIG_F159_OBU_HEADER

#if CONFIG_F159_OBU_HEADER
static void write_obu_header_with_stream_id(uint8_t *const dst,
                                            ObuHeader *obu_header,
                                            int stream_id) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  aom_wb_write_literal(&wb, obu_header->type, 4);           // obu_type
  aom_wb_write_bit(&wb, 0);                                 // reserved bit
  aom_wb_write_literal(&wb, obu_header->obu_tlayer_id, 3);  // obu_temporal
  aom_wb_write_literal(&wb, stream_id, 3);                  // obu_mlayer
  aom_wb_write_literal(&wb, obu_header->obu_xlayer_id, 5);  // obu_xlayer
}
#else   // CONFIG_F159_OBU_HEADER
static void write_obu_header_with_stream_id(uint8_t *const dst,
                                            ObuHeader *obu_header,
                                            int stream_id) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  aom_wb_write_bit(&wb, 0);                        // forbidden bit.
  aom_wb_write_literal(&wb, obu_header->type, 4);  // obu type
  aom_wb_write_bit(&wb, 1);                        // extention flag
  aom_wb_write_bit(&wb,
                   obu_header->has_size_field);  // obu_has_payload_length_field
  aom_wb_write_bit(&wb, 0);                      // reserved
  aom_wb_write_literal(&wb, obu_header->temporal_layer_id,
                       3);  // temporal_layer_id
  aom_wb_write_literal(&wb, obu_header->spatial_layer_id,
                       2);                  // spatial_layer_id
  aom_wb_write_literal(&wb, stream_id, 3);  // stream_id
}
#endif  // CONFIG_F159_OBU_HEADER

#endif  // STREAM_MUX_H_
