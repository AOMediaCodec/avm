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

/*!\file
 * \brief Describes look ahead buffer operations.
 */
#ifndef AOM_AV1_ENCODER_LOOKAHEAD_H_
#define AOM_AV1_ENCODER_LOOKAHEAD_H_

#include "aom_scale/yv12config.h"
#include "aom/aom_integer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!\cond */
#define MAX_LAG_BUFFERS 35
#define MAX_LAP_BUFFERS 35
#define MAX_TOTAL_BUFFERS (MAX_LAG_BUFFERS + MAX_LAP_BUFFERS)
#define LAP_LAG_IN_FRAMES 17

struct lookahead_entry {
  YV12_BUFFER_CONFIG img;
  int64_t ts_start;
  int64_t ts_end;
  aom_enc_frame_flags_t flags;
#if CONFIG_BRU
  int order_hint;
#endif  // CONFIG_BRU
};

// The max of past frames we want to keep in the queue.
#define MAX_PRE_FRAMES 1

enum { ENCODE_STAGE, LAP_STAGE, MAX_STAGES } UENUM1BYTE(COMPRESSOR_STAGE);

struct read_ctx {
  int sz;       /* Number of buffers currently in the queue */
  int read_idx; /* Read index */
  int pop_sz;   /* Size to check for pop condition */
  int valid;    /* Is this ctx valid? */
};

struct lookahead_ctx {
  int max_sz;                            /* Absolute size of the queue */
  int write_idx;                         /* Write index */
  struct read_ctx read_ctxs[MAX_STAGES]; /* Read context */
  struct lookahead_entry *buf;           /* Buffer list */
#if CONFIG_BRU
  int extra_sz;    /* Num of extra buffer used by bru*/
  int updated_idx; /* idx of active buffer used by bru*/
#endif             // CONFIG_BRU
};
/*!\endcond */

/**\brief Initializes the lookahead stage
 *
 * The lookahead stage is a queue of frame buffers on which some analysis
 * may be done when buffers are enqueued.
 */
struct lookahead_ctx *av1_lookahead_init(
    unsigned int width, unsigned int height, unsigned int subsampling_x,
    unsigned int subsampling_y, unsigned int depth, const int border_in_pixels,
    int byte_alignment, int num_lap_buffers,
#if CONFIG_BRU
    int num_extra_buffers,
#endif  // CONFIG_BRU
    bool alloc_pyramid);

/**\brief Destroys the lookahead stage
 */
void av1_lookahead_destroy(struct lookahead_ctx *ctx);

/**\brief Enqueue a source buffer
 *
 * This function will copy the source image into a new framebuffer with
 * the expected stride/border.
 *
 * If active_map is non-NULL and there is only one frame in the queue, then copy
 * only active macroblocks.
 *
 * \param[in] ctx               Pointer to the lookahead context
 * \param[in] src               Pointer to the image to enqueue
 * \param[in] ts_start          Timestamp for the start of this frame
 * \param[in] ts_end            Timestamp for the end of this frame
 * \param[in] order_hint        Order hint of this frame [CONFIG_BRU 1 only]
 * \param[in] flags             Flags set on this frame
 * \param[in] alloc_pyramid     Whether to allocate a downsampling pyramid
 *                              for each frame buffer
 */
int av1_lookahead_push(struct lookahead_ctx *ctx, const YV12_BUFFER_CONFIG *src,
                       int64_t ts_start, int64_t ts_end,
#if CONFIG_BRU
                       int order_hint,
#endif  // CONFIG_BRU
                       aom_enc_frame_flags_t flags, bool alloc_pyramid);

/**\brief Get the next source buffer to encode
 *
 * \param[in] ctx       Pointer to the lookahead context
 * \param[in] drain     Flag indicating the buffer should be drained
 *                      (return a buffer regardless of the current queue depth)
 * \param[in] stage     Encoder stage
 *
 * \retval Return NULL, if drain set and queue is empty, or if drain not set and
 * queue not of the configured depth.
 */
struct lookahead_entry *av1_lookahead_pop(struct lookahead_ctx *ctx, int drain,
                                          COMPRESSOR_STAGE stage);

/**\brief Get a future source buffer to encode
 *
 * \param[in] ctx       Pointer to the lookahead context
 * \param[in] index     Index of the frame to be returned, 0 == next frame
 * \param[in] stage     Encoder stage
 *
 * \retval Return NULL, if no buffer exists at the specified index
 */
struct lookahead_entry *av1_lookahead_peek(struct lookahead_ctx *ctx, int index,
                                           COMPRESSOR_STAGE stage);

#if CONFIG_BRU
/**\brief One entry leave the queue
 *
 * \param[in] ctx               Pointer to the lookahead context
 * \param[in] left_order_hint   which frame need to leave
 * \param[in] stage             Encoder stage
 *
 * \retval Return NULL, if no buffer exists at the specified index
 */
struct lookahead_entry *av1_lookahead_leave(struct lookahead_ctx *ctx,
                                            int left_order_hint,
                                            COMPRESSOR_STAGE stage);

/*!\cond */
void bru_lookahead_buf_refresh(struct lookahead_ctx *ctx,
                               int refresh_frame_flags,
                               RefCntBuffer *const ref_frame_map[REF_FRAMES],
                               COMPRESSOR_STAGE stage);
/*!\endcond */
#endif  // CONFIG_BRU

/**\brief Get the number of frames currently in the lookahead queue
 */
unsigned int av1_lookahead_depth(struct lookahead_ctx *ctx,
                                 COMPRESSOR_STAGE stage);

/**\brief Get pop_sz value
 */
int av1_lookahead_pop_sz(struct lookahead_ctx *ctx, COMPRESSOR_STAGE stage);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_LOOKAHEAD_H_
