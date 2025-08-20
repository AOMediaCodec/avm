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
#include <stdlib.h>

#include "config/aom_config.h"

#include "aom_scale/yv12config.h"
#include "av1/common/common.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/extend.h"
#include "av1/encoder/lookahead.h"

/* Return the buffer at the given absolute index and increment the index */
static struct lookahead_entry *pop(struct lookahead_ctx *ctx, int *idx) {
  int index = *idx;
  struct lookahead_entry *buf = ctx->buf + index;

  assert(index < ctx->max_sz);
  if (++index >= ctx->max_sz) index -= ctx->max_sz;
  *idx = index;
  return buf;
}

void av1_lookahead_destroy(struct lookahead_ctx *ctx) {
  if (ctx) {
    if (ctx->buf) {
      int i;

      for (i = 0; i < ctx->max_sz; i++) aom_free_frame_buffer(&ctx->buf[i].img);
      free(ctx->buf);
    }
    free(ctx);
  }
}

struct lookahead_ctx *av1_lookahead_init(int width, int height,
                                         int subsampling_x, int subsampling_y,
                                         int depth, const int border_in_pixels,
                                         int byte_alignment,
                                         int num_lap_buffers,
#if CONFIG_BRU  // BRU need extra look ahead buffer to store the src of
                // reference frame (org pixels)
                                         int num_extra_buffers,
#endif  // CONFIG_BRU
                                         bool alloc_pyramid) {
  struct lookahead_ctx *ctx = NULL;
  int lag_in_frames = AOMMAX(1, depth);

  // Add the lags to depth and clamp
  depth += num_lap_buffers;
  depth = clamp(depth, 1, MAX_TOTAL_BUFFERS);

  // Allocate memory to keep previous source frames available.
  depth += MAX_PRE_FRAMES;
#if CONFIG_BRU
  depth += num_extra_buffers;
#endif  // CONFIG_BRU
  // Allocate the lookahead structures
  ctx = calloc(1, sizeof(*ctx));
  if (ctx) {
    ctx->max_sz = depth;
#if CONFIG_BRU
    ctx->read_ctxs[ENCODE_STAGE].pop_sz =
        ctx->max_sz - MAX_PRE_FRAMES - num_extra_buffers;
    ctx->extra_sz = num_extra_buffers;
    ctx->updated_idx = 0;
#else
    ctx->read_ctxs[ENCODE_STAGE].pop_sz = ctx->max_sz - MAX_PRE_FRAMES;
#endif  // CONFIG_BRU
    ctx->read_ctxs[ENCODE_STAGE].valid = 1;
    if (num_lap_buffers) {
      ctx->read_ctxs[LAP_STAGE].pop_sz = lag_in_frames;
      ctx->read_ctxs[LAP_STAGE].valid = 1;
    }
    ctx->buf = calloc(depth, sizeof(*ctx->buf));
    if (!ctx->buf) goto fail;
    for (int i = 0; i < depth; i++) {
      aom_free_frame_buffer(&ctx->buf[i].img);
      if (aom_realloc_frame_buffer(&ctx->buf[i].img, width, height,
                                   subsampling_x, subsampling_y,
                                   border_in_pixels, byte_alignment, NULL, NULL,
                                   NULL, alloc_pyramid))
        goto fail;
    }
  }
  return ctx;
fail:
  av1_lookahead_destroy(ctx);
  return NULL;
}

int av1_lookahead_push(struct lookahead_ctx *ctx, const YV12_BUFFER_CONFIG *src,
                       int64_t ts_start, int64_t ts_end,
#if CONFIG_BRU
                       int disp_order_hint,
#endif  // CONFIG_BRU
                       aom_enc_frame_flags_t flags, bool alloc_pyramid) {
  struct lookahead_entry *buf;
  int width = src->y_crop_width;
  int height = src->y_crop_height;
  int uv_width = src->uv_crop_width;
  int uv_height = src->uv_crop_height;
  int subsampling_x = src->subsampling_x;
  int subsampling_y = src->subsampling_y;
  int larger_dimensions, new_dimensions;

  assert(ctx->read_ctxs[ENCODE_STAGE].valid == 1);
  if (ctx->read_ctxs[ENCODE_STAGE].sz + 1 + MAX_PRE_FRAMES > ctx->max_sz)
    return 1;
  ctx->read_ctxs[ENCODE_STAGE].sz++;
  if (ctx->read_ctxs[LAP_STAGE].valid) {
    ctx->read_ctxs[LAP_STAGE].sz++;
  }
  buf = pop(ctx, &ctx->write_idx);
#if CONFIG_BRU
  if (++ctx->updated_idx >= ctx->max_sz) ctx->updated_idx -= ctx->max_sz;
  assert(ctx->updated_idx < ctx->max_sz);
#endif  // CONFIG_BRU
  new_dimensions = width != buf->img.y_crop_width ||
                   height != buf->img.y_crop_height ||
                   uv_width != buf->img.uv_crop_width ||
                   uv_height != buf->img.uv_crop_height;
  larger_dimensions = width > buf->img.y_width || height > buf->img.y_height ||
                      uv_width > buf->img.uv_width ||
                      uv_height > buf->img.uv_height;
  assert(!larger_dimensions || new_dimensions);

  if (larger_dimensions) {
    YV12_BUFFER_CONFIG new_img;
    memset(&new_img, 0, sizeof(new_img));
    if (aom_alloc_frame_buffer(&new_img, width, height, subsampling_x,
                               subsampling_y, AOM_BORDER_IN_PIXELS, 0,
                               alloc_pyramid))
      return 1;
    aom_free_frame_buffer(&buf->img);
    buf->img = new_img;
  } else if (new_dimensions) {
    buf->img.y_crop_width = src->y_crop_width;
    buf->img.y_crop_height = src->y_crop_height;
    buf->img.uv_crop_width = src->uv_crop_width;
    buf->img.uv_crop_height = src->uv_crop_height;
    buf->img.subsampling_x = src->subsampling_x;
    buf->img.subsampling_y = src->subsampling_y;
  }
  // Partial copy not implemented yet
  av1_copy_and_extend_frame(src, &buf->img);

  buf->ts_start = ts_start;
  buf->ts_end = ts_end;
  buf->flags = flags;
  aom_remove_metadata_from_frame_buffer(&buf->img);
  aom_copy_metadata_to_frame_buffer(&buf->img, src->metadata);
#if CONFIG_BRU
  buf->disp_order_hint = disp_order_hint;
#endif  // CONFIG_BRU
  return 0;
}

#if CONFIG_BRU
struct lookahead_entry *av1_lookahead_leave(struct lookahead_ctx *ctx,
                                            int left_disp_order_hint,
                                            COMPRESSOR_STAGE stage) {
  // order hint must be set so that the lookahead buffer can track which entry
  // todo: fix this use disp order
  (void)stage;
  struct lookahead_entry *buf = NULL;
  if (ctx) {
    assert(ctx->read_ctxs[stage].valid == 1);
    for (int i = 0; i < ctx->max_sz; i++) {
      if (ctx->buf[i].disp_order_hint == left_disp_order_hint) {
        break;
      }
    }
    ctx->read_ctxs[ENCODE_STAGE].read_idx = ctx->updated_idx;
    ctx->write_idx = ctx->updated_idx;
  }
  return buf;
}

void bru_lookahead_buf_refresh(struct lookahead_ctx *ctx,
                               int refresh_frame_flags,
                               RefCntBuffer *const ref_frame_map[REF_FRAMES],
                               COMPRESSOR_STAGE stage) {
  if (ctx) {
    assert(ctx->read_ctxs[stage].valid == 1);
    int last_idx = ctx->updated_idx - 1;
    if (last_idx < 0) last_idx += ctx->max_sz;
    for (int ref_frame = 0; ref_frame < REF_FRAMES; ref_frame++) {
      if (((refresh_frame_flags >> ref_frame) & 1) == 1 &&
          ref_frame_map[ref_frame]) {
        int target_disp_order = ref_frame_map[ref_frame]->display_order_hint;
        bool use_free_fb = false;
        for (int j = 0; j < ref_frame; j++) {
          if (ref_frame_map[j] &&
              target_disp_order == (int)ref_frame_map[j]->display_order_hint) {
            use_free_fb = true;
          }
        }
        if (use_free_fb) {
          return;
        }
        int found_idx = -1;
        for (int k = 0; k < last_idx; k++) {
          if ((int)ctx->buf[k].disp_order_hint == target_disp_order) {
            found_idx = k;
            break;
          }
        }
        if (found_idx >= 0) {
          // swap last with found
          struct lookahead_entry temp = ctx->buf[found_idx];
          ctx->buf[found_idx] = ctx->buf[last_idx];
          ctx->buf[last_idx] = temp;
          ctx->updated_idx--;
          last_idx--;
        }
      }
    }
    ctx->updated_idx = last_idx + 1;
    if (ctx->updated_idx >= ctx->max_sz) ctx->updated_idx -= ctx->max_sz;
    ctx->read_ctxs[stage].read_idx = ctx->updated_idx;
    ctx->write_idx = ctx->updated_idx;
  }
}
#endif  // CONFIG_BRU

struct lookahead_entry *av1_lookahead_pop(struct lookahead_ctx *ctx, int drain,
                                          COMPRESSOR_STAGE stage) {
  struct lookahead_entry *buf = NULL;
  if (ctx) {
    struct read_ctx *read_ctx = &ctx->read_ctxs[stage];
    assert(read_ctx->valid == 1);
    if (read_ctx->sz && (drain || read_ctx->sz == read_ctx->pop_sz)) {
      buf = pop(ctx, &read_ctx->read_idx);
      read_ctx->sz--;
    }
  }
  return buf;
}

struct lookahead_entry *av1_lookahead_peek(struct lookahead_ctx *ctx, int index,
                                           COMPRESSOR_STAGE stage) {
  struct lookahead_entry *buf = NULL;
  struct read_ctx *read_ctx = NULL;
  if (ctx == NULL) {
    return buf;
  }

  read_ctx = &ctx->read_ctxs[stage];
  assert(read_ctx->valid == 1);
  if (index >= 0) {
    // Forward peek
#if CONFIG_BRU
    if (index < read_ctx->sz + ctx->extra_sz) {
#else
    if (index < read_ctx->sz) {
#endif  // CONFIG_BRU
      index += read_ctx->read_idx;
      if (index >= ctx->max_sz) index -= ctx->max_sz;
      buf = ctx->buf + index;
    }
  } else if (index < 0) {
    // Backward peek
#if CONFIG_BRU
    if (-index <= MAX_PRE_FRAMES + ctx->extra_sz) {
#else
    if (-index <= MAX_PRE_FRAMES) {
#endif  // CONFIG_BRU
      index += (int)(read_ctx->read_idx);
      if (index < 0) index += (int)(ctx->max_sz);
      buf = ctx->buf + index;
    }
  }

  return buf;
}

int av1_lookahead_depth(struct lookahead_ctx *ctx, COMPRESSOR_STAGE stage) {
  struct read_ctx *read_ctx = NULL;
  assert(ctx != NULL);

  read_ctx = &ctx->read_ctxs[stage];
  assert(read_ctx->valid == 1);
  return read_ctx->sz;
}

int av1_lookahead_pop_sz(struct lookahead_ctx *ctx, COMPRESSOR_STAGE stage) {
  struct read_ctx *read_ctx = NULL;
  assert(ctx != NULL);

  read_ctx = &ctx->read_ctxs[stage];
  assert(read_ctx->valid == 1);
  return read_ctx->pop_sz;
}
