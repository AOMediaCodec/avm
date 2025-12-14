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
#ifndef AVM_COMMON_STREAM_ITER_H_
#define AVM_COMMON_STREAM_ITER_H_

#include "avm/avm_image.h"
#include "common/tools_common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct StreamIter;

// The input to each stream iterator can either be an AvxInputContext (when
// reading from a file or a pipe like STDIN) or another stream iterator.
union StreamInput {
  struct AvxInputContext *avx;
  struct StreamIter *stream;
};

typedef struct StreamIter {
  union StreamInput input;
  // For simplicity, all streams have two additional numbers associated
  // with them, e.g., for counting the number of frames returned and the
  // number of frames that should be skipped. Some iterators, like the copy
  // iterator, do not use either field.
  int current;
  int n;
  // Pointer to the function that performs the read. Returns whether a frame
  // is available. If a frame is returned, it is written into *raw.
  int (*reader)(struct StreamIter *iter, avm_image_t *raw);
} StreamIter;

// Iterator that simply copies the data from the AvxInputContext.
void copy_stream_iter_init(StreamIter *iter, struct AvxInputContext *input);

// Iterator that skips the first N frames. Takes another stream as input.
void skip_stream_iter_init(StreamIter *iter, StreamIter *input, int num_skip);

// Iterator that only returns every N-th frame. Takes another stream as input.
void step_stream_iter_init(StreamIter *iter, StreamIter *input, int step_size);

// Iterator that stops returning frames after the N-th. Takes another stream
// as input.
void limit_stream_iter_init(StreamIter *iter, StreamIter *input, int limit);

// Invokes the iterator's specialized read function to read data from the
// stream. Returns if a frame was read. If so, writes the data into *raw.
int read_stream_iter(StreamIter *iter, avm_image_t *raw);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_STREAM_ITER_H_
