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

#ifndef AVM_COMMON_VIDEO_READER_H_
#define AVM_COMMON_VIDEO_READER_H_

#include "common/video_common.h"

// The following code is work in progress. It is going to  support transparent
// reading of input files. Right now only IVF format is supported for
// simplicity. The main goal the API is to be simple and easy to use in example
// code and in avmenc/avmdec later. All low-level details like memory
// buffer management are hidden from API users.
struct AvxVideoReaderStruct;
typedef struct AvxVideoReaderStruct AvxVideoReader;

#ifdef __cplusplus
extern "C" {
#endif

// Opens the input file for reading and inspects it to determine file type.
// Returns an opaque AvxVideoReader* upon success, or NULL upon failure.
// Right now only IVF format is supported.
AvxVideoReader *avm_video_reader_open(const char *filename);

// Frees all resources associated with AvxVideoReader* returned from
// avm_video_reader_open() call.
void avm_video_reader_close(AvxVideoReader *reader);

// Reads frame from the file and stores it in internal buffer.
int avm_video_reader_read_frame(AvxVideoReader *reader);

// Returns the pointer to memory buffer with frame data read by last call to
// avm_video_reader_read_frame().
const uint8_t *avm_video_reader_get_frame(AvxVideoReader *reader, size_t *size);

// Returns the pts of the frame.
int64_t avm_video_reader_get_frame_pts(AvxVideoReader *reader);
// Return the reader file.
FILE *avm_video_reader_get_file(AvxVideoReader *reader);

// Fills AvxVideoInfo with information from opened video file.
const AvxVideoInfo *avm_video_reader_get_info(AvxVideoReader *reader);

// Set fourcc.
void avm_video_reader_set_fourcc(AvxVideoReader *reader, uint32_t fourcc);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_VIDEO_READER_H_
