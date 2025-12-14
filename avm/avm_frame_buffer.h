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

#ifndef AVM_AVM_AVM_FRAME_BUFFER_H_
#define AVM_AVM_AVM_FRAME_BUFFER_H_

/*!\file
 * \brief Describes the decoder external frame buffer interface.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "config/avm_config.h"
#include "avm/avm_integer.h"

/*!\brief The maximum number of sub-streams.
 */
#define AVM_MAX_NUM_STREAMS 4

/*!\brief The maximum number of work buffers used by libavm.
 *  Support maximum 4 threads to decode video in parallel.
 *  Each thread will use one work buffer.
 * TODO(hkuang): Add support to set number of worker threads dynamically.
 */
#define AVM_MAXIMUM_WORK_BUFFERS 8

/*!\brief The maximum number of reference buffers that a AV2 encoder may use.
 */
#define AVM_MAXIMUM_REF_BUFFERS 16
/*!\brief External frame buffer
 *
 * This structure holds allocated frame buffers used by the decoder.
 */
typedef struct avm_codec_frame_buffer {
  uint8_t *data; /**< Pointer to the data buffer */
  size_t size;   /**< Size of data in bytes */
  void *priv;    /**< Frame's private data */
} avm_codec_frame_buffer_t;

/*!\brief get frame buffer callback prototype
 *
 * This callback is invoked by the decoder to retrieve data for the frame
 * buffer in order for the decode call to complete. The callback must
 * allocate at least min_size in bytes and assign it to fb->data. The callback
 * must zero out all the data allocated. Then the callback must set fb->size
 * to the allocated size. The application does not need to align the allocated
 * data. The callback is triggered when the decoder needs a frame buffer to
 * decode a compressed image into. This function may be called more than once
 * for every call to avm_codec_decode. The application may set fb->priv to
 * some data which will be passed back in the avm_image_t and the release
 * function call. |fb| is guaranteed to not be NULL. On success the callback
 * must return 0. Any failure the callback must return a value less than 0.
 *
 * \param[in] priv         Callback's private data
 * \param[in] min_size     Minimum size in bytes needed by the buffer
 * \param[in,out] fb       Pointer to avm_codec_frame_buffer_t
 */
typedef int (*avm_get_frame_buffer_cb_fn_t)(void *priv, size_t min_size,
                                            avm_codec_frame_buffer_t *fb);

/*!\brief release frame buffer callback prototype
 *
 * This callback is invoked by the decoder when the frame buffer is not
 * referenced by any other buffers. |fb| is guaranteed to not be NULL. On
 * success the callback must return 0. Any failure the callback must return
 * a value less than 0.
 *
 * \param[in] priv         Callback's private data
 * \param[in] fb           Pointer to avm_codec_frame_buffer_t
 */
typedef int (*avm_release_frame_buffer_cb_fn_t)(void *priv,
                                                avm_codec_frame_buffer_t *fb);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_AVM_FRAME_BUFFER_H_
