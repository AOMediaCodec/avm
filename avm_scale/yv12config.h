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

#ifndef AVM_AVM_SCALE_YV12CONFIG_H_
#define AVM_AVM_SCALE_YV12CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "config/avm_config.h"

#include "avm/avm_codec.h"
#include "avm/avm_frame_buffer.h"
#include "avm/avm_integer.h"
#include "avm/internal/avm_image_internal.h"

/*!\cond */

#define AVM_INTERP_EXTEND 4
#define AVMINNERBORDERINPIXELS \
  ALIGN_POWER_OF_TWO(256 + 2 * AVM_INTERP_EXTEND, 5)
#define AVM_BORDER_IN_PIXELS \
  ALIGN_POWER_OF_TWO(2 * 256 + 2 * AVM_INTERP_EXTEND, 5)
#define AVM_ENC_NO_SCALE_BORDER \
  ALIGN_POWER_OF_TWO(256 + 2 * AVM_INTERP_EXTEND, 5)
#define AVM_DEC_BORDER_IN_PIXELS (256 >> 1)

#if CONFIG_AV2_ENCODER
struct image_pyramid;
struct corner_list;
#endif  // CONFIG_AV2_ENCODER

/*!\endcond */
/*!
 * \brief YV12 frame buffer data structure
 */
typedef struct yv12_buffer_config {
  /*!\cond */
  union {
    struct {
      int y_width;
      int uv_width;
    };
    int widths[2];
  };
  union {
    struct {
      int y_height;
      int uv_height;
    };
    int heights[2];
  };
  union {
    struct {
      int y_crop_width;
      int uv_crop_width;
    };
    int crop_widths[2];
  };
  union {
    struct {
      int y_crop_height;
      int uv_crop_height;
    };
    int crop_heights[2];
  };
  union {
    struct {
      int y_stride;
      int uv_stride;
    };
    int strides[2];
  };
  union {
    struct {
      uint16_t *y_buffer;
      uint16_t *u_buffer;
      uint16_t *v_buffer;
    };
    uint16_t *buffers[3];
  };

  // Indicate whether y_buffer, u_buffer, and v_buffer points to the internally
  // allocated memory or external buffers.
  int use_external_reference_buffers;
  // This is needed to store y_buffer, u_buffer, and v_buffer when set reference
  // uses an external refernece, and restore those buffer pointers after the
  // external reference frame is no longer used.
  uint16_t *store_buf_adr[3];

  // Global motion search data
#if CONFIG_AV2_ENCODER
  // 8-bit downsampling pyramid for the Y plane
  struct image_pyramid *y_pyramid;
  struct corner_list *corners;
#endif  // CONFIG_AV2_ENCODER

  uint8_t *buffer_alloc;
  size_t buffer_alloc_sz;
  int border;
  size_t frame_size;
  int subsampling_x;
  int subsampling_y;
  unsigned int bit_depth;
  avm_color_primaries_t color_primaries;
  avm_transfer_characteristics_t transfer_characteristics;
  avm_matrix_coefficients_t matrix_coefficients;
  uint8_t monochrome;
  avm_chroma_sample_position_t chroma_sample_position;
  avm_color_range_t color_range;
  int render_width;
  int render_height;

  int corrupted;
  int flags;
  avm_metadata_array_t *metadata;

#if CONFIG_CROP_WIN_CWG_F220
  /* Cropping dimensions */
  int w_conf_win_enabled_flag;
  int w_win_left_offset;
  int w_win_right_offset;
  int w_win_top_offset;
  int w_win_bottom_offset;
  int max_width;
  int max_height;
  int crop_height;
  int crop_width;
#endif  // CONFIG_CROP_WIN_CWG_F220
  /*!\endcond */
} YV12_BUFFER_CONFIG;

/*!\cond */

// Allocate a frame buffer
//
// If ybf currently contains an image, all associated memory will be freed and
// then reallocated. In contrast, avm_realloc_frame_buffer() will reuse any
// existing allocations where possible. So, if ybf is likely to already be
// set up, please consider avm_realloc_frame_buffer() instead.
//
// See avm_realloc_frame_buffer() for the meanings of the arguments, and
// available return values.
int avm_alloc_frame_buffer(YV12_BUFFER_CONFIG *ybf, int width, int height,
                           int ss_x, int ss_y, int border, int byte_alignment,
                           bool alloc_pyramid);

// Updates the yv12 buffer config with the frame buffer. |byte_alignment| must
// be a power of 2, from 32 to 1024. 0 sets legacy alignment. If cb is not
// NULL, then libavm is using the frame buffer callbacks to handle memory.
// If cb is not NULL, libavm will call cb with minimum size in bytes needed
// to decode the current frame. If cb is NULL, libavm will allocate memory
// internally to decode the current frame.
//
// If alloc_pyramid is true, then an image pyramid will be allocated
// for use in global motion estimation. This is only needed if this frame
// buffer will be used to store a source frame or a reference frame in
// the encoder. Any other framebuffers (eg, intermediates for filtering,
// or any buffer in the decoder) can set alloc_pyramid = false.
//
// Returns 0 on success. Returns < 0  on failure.
int avm_realloc_frame_buffer(YV12_BUFFER_CONFIG *ybf, int width, int height,
                             int ss_x, int ss_y, int border, int byte_alignment,
                             avm_codec_frame_buffer_t *fb,
                             avm_get_frame_buffer_cb_fn_t cb, void *cb_priv,
                             bool alloc_pyramid);

int avm_free_frame_buffer(YV12_BUFFER_CONFIG *ybf);

/*!\endcond */
/*!\brief Removes metadata from YUV_BUFFER_CONFIG struct.
 *
 * Frees metadata in frame buffer.
 * Frame buffer metadata pointer will be set to NULL.
 *
 * \param[in]    ybf       Frame buffer struct pointer
 */
void avm_remove_metadata_from_frame_buffer(YV12_BUFFER_CONFIG *ybf);

/*!\brief Copy metadata to YUV_BUFFER_CONFIG struct.
 *
 * Copies metadata to frame buffer.
 * Frame buffer will clear any previous metadata and will reallocate the
 * metadata array to the new metadata size. Then, it will copy the new metadata
 * array into it.
 * If arr metadata pointer points to the same address as current metadata in the
 * frame buffer, function will do nothing and return 0.
 * Returns 0 on success or -1 on failure.
 *
 * \param[in]    ybf       Frame buffer struct pointer
 * \param[in]    arr       Metadata array struct pointer
 */
int avm_copy_metadata_to_frame_buffer(YV12_BUFFER_CONFIG *ybf,
                                      const avm_metadata_array_t *arr);

#ifdef __cplusplus
}
#endif

#endif  // AVM_AVM_SCALE_YV12CONFIG_H_
