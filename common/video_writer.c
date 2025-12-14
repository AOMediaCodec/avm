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
#include "common/video_writer.h"

#include <stdlib.h>

#include "avm/avm_encoder.h"
#include "common/ivfenc.h"

struct AvxVideoWriterStruct {
  AvxVideoInfo info;
  FILE *file;
  int frame_count;
};

static void write_header(FILE *file, const AvxVideoInfo *info,
                         int frame_count) {
  struct avm_codec_enc_cfg cfg;
  cfg.g_w = info->frame_width;
  cfg.g_h = info->frame_height;
  cfg.g_timebase.num = info->time_base.numerator;
  cfg.g_timebase.den = info->time_base.denominator;

  ivf_write_file_header(file, &cfg, info->codec_fourcc, frame_count);
}

AvxVideoWriter *avm_video_writer_open(const char *filename,
                                      AvxContainer container,
                                      const AvxVideoInfo *info) {
  if (container == kContainerIVF) {
    AvxVideoWriter *writer = NULL;
    FILE *const file = fopen(filename, "wb");
    if (!file) return NULL;

    writer = malloc(sizeof(*writer));
    if (!writer) {
      fclose(file);
      return NULL;
    }
    writer->frame_count = 0;
    writer->info = *info;
    writer->file = file;

    write_header(writer->file, info, 0);

    return writer;
  }

  return NULL;
}

void avm_video_writer_close(AvxVideoWriter *writer) {
  if (writer) {
    // Rewriting frame header with real frame count
    rewind(writer->file);
    write_header(writer->file, &writer->info, writer->frame_count);

    fclose(writer->file);
    free(writer);
  }
}

int avm_video_writer_write_frame(AvxVideoWriter *writer, const uint8_t *buffer,
                                 size_t size, int64_t pts) {
  ivf_write_frame_header(writer->file, pts, size);
  if (fwrite(buffer, 1, size, writer->file) != size) return 0;

  ++writer->frame_count;

  return 1;
}

void avm_video_writer_set_fourcc(AvxVideoWriter *writer, uint32_t fourcc) {
  writer->info.codec_fourcc = fourcc;
}
