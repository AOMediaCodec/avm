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

#ifndef AVM_COMMON_VIDEO_WRITER_H_
#define AVM_COMMON_VIDEO_WRITER_H_

#include "common/video_common.h"

enum { kContainerIVF } UENUM1BYTE(AvxContainer);

struct AvxVideoWriterStruct;
typedef struct AvxVideoWriterStruct AvxVideoWriter;

#ifdef __cplusplus
extern "C" {
#endif

// Finds and opens writer for specified container format.
// Returns an opaque AvxVideoWriter* upon success, or NULL upon failure.
// Right now only IVF format is supported.
AvxVideoWriter *avm_video_writer_open(const char *filename,
                                      AvxContainer container,
                                      const AvxVideoInfo *info);

// Frees all resources associated with AvxVideoWriter* returned from
// avm_video_writer_open() call.
void avm_video_writer_close(AvxVideoWriter *writer);

// Writes frame bytes to the file.
int avm_video_writer_write_frame(AvxVideoWriter *writer, const uint8_t *buffer,
                                 size_t size, int64_t pts);
// Set fourcc.
void avm_video_writer_set_fourcc(AvxVideoWriter *writer, uint32_t fourcc);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_VIDEO_WRITER_H_
