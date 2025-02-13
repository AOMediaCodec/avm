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

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "common/stream_iter.h"
#include "common/y4minput.h"

static int copy_reader(StreamIter *iter, aom_image_t *img) {
#if CONFIG_MULTIVIEW_CORE
  const int view_id = iter->view_id;
#endif
  struct AvxInputContext *input_ctx = iter->input.avx;
#if CONFIG_MULTIVIEW_CORE
  FILE *f = input_ctx->file[view_id];
  y4m_input *y4m = &input_ctx->y4m[view_id];
#else
  FILE *f = input_ctx->file;
  y4m_input *y4m = &input_ctx->y4m;
#endif
  int shortread = 0;

#if CONFIG_MULTIVIEW_CORE
  if (input_ctx->file_type[view_id] == FILE_TYPE_Y4M) {
#else
  if (input_ctx->file_type == FILE_TYPE_Y4M) {
#endif
    if (y4m_input_fetch_frame(y4m, f, img) < 1) return 0;
  } else {
#if CONFIG_MULTIVIEW_CORE
    shortread = read_yuv_frame(input_ctx, img, view_id);
#else
    shortread = read_yuv_frame(input_ctx, img);
#endif
  }
  return !shortread;
}

void copy_stream_iter_init(StreamIter *iter, struct AvxInputContext *input) {
  iter->input.avx = input;
  iter->reader = copy_reader;
#if CONFIG_MULTIVIEW_CORE
  iter->view_id = 0;
#endif
}

static int skip_reader(StreamIter *iter, aom_image_t *raw) {
  // While we haven't skipped enough frames, read from the underlying
  // stream and throw the result away. If no frame is available, early
  // exit.
  while (iter->current < iter->n) {
    ++iter->current;
    int frame_avail = read_stream_iter(iter->input.stream, raw);
    if (!frame_avail) {
      return frame_avail;
    }
  }
  // If we're past the skip region, copy the remaining output.
  return read_stream_iter(iter->input.stream, raw);
}

void skip_stream_iter_init(StreamIter *iter, StreamIter *input, int num_skip) {
  assert(num_skip >= 0);
  iter->input.stream = input;
  iter->current = 0;
  iter->n = num_skip;
  iter->reader = skip_reader;
}

static int step_reader(StreamIter *iter, aom_image_t *raw) {
  while (true) {
    int frame_avail = read_stream_iter(iter->input.stream, raw);
    // If at end of stream, no need to read further.
    if (!frame_avail) {
      return frame_avail;
    }

    bool should_encode = iter->current == 0;
    iter->current = (iter->current + 1) % (iter->n);
    if (should_encode) {
      return frame_avail;
    }
  }
}

void step_stream_iter_init(StreamIter *iter, StreamIter *input, int step_size) {
  assert(step_size > 0);
  iter->input.stream = input;
  iter->current = 0;
  iter->n = step_size;
  iter->reader = step_reader;
}

static int limit_reader(StreamIter *iter, aom_image_t *raw) {
  // limit of 0 is a special case meaning "no limit".
  if (iter->n != 0 && iter->current >= iter->n) {
    return 0;
  }
  ++iter->current;
  return read_stream_iter(iter->input.stream, raw);
}

void limit_stream_iter_init(StreamIter *iter, StreamIter *input, int limit) {
  assert(limit >= 0);
  iter->input.stream = input;
  iter->current = 0;
#if CONFIG_MULTIVIEW_CORE
  iter->view_id = 0;
#endif
  iter->n = limit;
  iter->reader = limit_reader;
}

int read_stream_iter(StreamIter *iter, aom_image_t *raw) {
  return iter->reader(iter, raw);
}
