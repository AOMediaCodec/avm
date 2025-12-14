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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "avm_ports/mem_ops.h"
#include "common/ivfdec.h"
#include "common/obudec.h"
#include "common/tools_common.h"
#include "common/video_reader.h"
#include "common/webmdec.h"

struct AvxVideoReaderStruct {
  AvxVideoInfo info;
  struct AvxInputContext input_ctx;
  struct ObuDecInputContext obu_ctx;
  struct WebmInputContext webm_ctx;
  uint8_t *buffer;
  size_t buffer_size;
  size_t frame_size;
  avm_codec_pts_t pts;
};

AvxVideoReader *avm_video_reader_open(const char *filename) {
  AvxVideoReader *reader = NULL;
  FILE *const file = fopen(filename, "rb");
  if (!file) return NULL;  // Can't open file

  reader = (AvxVideoReader *)calloc(1, sizeof(*reader));
  if (!reader) {
    fclose(file);
    return NULL;  // Can't allocate AvxVideoReader
  }

  reader->input_ctx.filename = filename;
  reader->input_ctx.file = file;
  reader->obu_ctx.avx_ctx = &reader->input_ctx;
  if (file_is_ivf(&reader->input_ctx)) {
    reader->input_ctx.file_type = FILE_TYPE_IVF;
    reader->info.codec_fourcc = reader->input_ctx.fourcc;
    reader->info.frame_width = reader->input_ctx.width;
    reader->info.frame_height = reader->input_ctx.height;
#if CONFIG_WEBM_IO
  } else if (file_is_webm(&reader->webm_ctx, &reader->input_ctx)) {
    reader->input_ctx.file_type = FILE_TYPE_WEBM;
    reader->info.codec_fourcc = reader->input_ctx.fourcc;
    reader->info.frame_width = reader->input_ctx.width;
    reader->info.frame_height = reader->input_ctx.height;
#endif
  } else if (file_is_obu(&reader->obu_ctx)) {
    reader->input_ctx.file_type = FILE_TYPE_OBU;
    // assume AV2
    reader->info.codec_fourcc = AV2_FOURCC;
  } else {
    fclose(file);
    free(reader);
    return NULL;  // Unknown file type
  }

  return reader;
}

void avm_video_reader_close(AvxVideoReader *reader) {
  if (reader) {
    fclose(reader->input_ctx.file);
    if (reader->input_ctx.file_type == FILE_TYPE_OBU) {
      obudec_free(&reader->obu_ctx);
    }
    free(reader->buffer);
    free(reader);
  }
}

int avm_video_reader_read_frame(AvxVideoReader *reader) {
  if (reader->input_ctx.file_type == FILE_TYPE_IVF) {
    return !ivf_read_frame(reader->input_ctx.file, &reader->buffer,
                           &reader->frame_size, &reader->buffer_size,
                           &reader->pts);
  } else if (reader->input_ctx.file_type == FILE_TYPE_OBU) {
    return !obudec_read_temporal_unit(&reader->obu_ctx, &reader->buffer,
                                      &reader->frame_size,
                                      &reader->buffer_size);
#if CONFIG_WEBM_IO
  } else if (reader->input_ctx.file_type == FILE_TYPE_WEBM) {
    return !webm_read_frame(&reader->webm_ctx, &reader->buffer,
                            &reader->frame_size, &reader->buffer_size);
#endif
  } else {
    assert(0);
    return 0;
  }
}

const uint8_t *avm_video_reader_get_frame(AvxVideoReader *reader,
                                          size_t *size) {
  if (size) *size = reader->frame_size;

  return reader->buffer;
}

int64_t avm_video_reader_get_frame_pts(AvxVideoReader *reader) {
  return (int64_t)reader->pts;
}

FILE *avm_video_reader_get_file(AvxVideoReader *reader) {
  return reader->input_ctx.file;
}

const AvxVideoInfo *avm_video_reader_get_info(AvxVideoReader *reader) {
  return &reader->info;
}

void avm_video_reader_set_fourcc(AvxVideoReader *reader, uint32_t fourcc) {
  reader->info.codec_fourcc = fourcc;
}
