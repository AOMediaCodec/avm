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

#include "avm/internal/avm_image_internal.h"
#include "avm_dsp/pyramid.h"
#include "avm_dsp/flow_estimation/corner_detect.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/mem.h"
#include "avm_scale/yv12config.h"
#include "av2/common/enums.h"

/****************************************************************************
 *  Exports
 ****************************************************************************/

/****************************************************************************
 *
 ****************************************************************************/

// TODO(jkoleszar): Maybe replace this with struct avm_image
int avm_free_frame_buffer(YV12_BUFFER_CONFIG *ybf) {
  if (ybf) {
    if (ybf->buffer_alloc_sz > 0) {
      avm_free(ybf->buffer_alloc);
    }
#if CONFIG_AV2_ENCODER
    if (ybf->y_pyramid) {
      avm_free_pyramid(ybf->y_pyramid);
    }
    if (ybf->corners) {
      av2_free_corner_list(ybf->corners);
    }
#endif  // CONFIG_AV2_ENCODER
    avm_remove_metadata_from_frame_buffer(ybf);
    /* buffer_alloc isn't accessed by most functions.  Rather y_buffer,
      u_buffer and v_buffer point to buffer_alloc and are used.  Clear out
      all of this so that a freed pointer isn't inadvertently used */
    memset(ybf, 0, sizeof(YV12_BUFFER_CONFIG));
    return 0;
  }

  return AVM_CODEC_MEM_ERROR;
}

static int realloc_frame_buffer_aligned(
    YV12_BUFFER_CONFIG *ybf, int width, int height, int ss_x, int ss_y,
    int border, int byte_alignment, avm_codec_frame_buffer_t *fb,
    avm_get_frame_buffer_cb_fn_t cb, void *cb_priv, const int y_stride,
    const uint64_t yplane_size, const uint64_t uvplane_size,
    const int aligned_width, const int aligned_height, const int uv_width,
    const int uv_height, const int uv_stride, const int uv_border_w,
    const int uv_border_h, bool alloc_pyramid) {
  if (ybf) {
    const int avm_byte_align = (byte_alignment == 0) ? 1 : byte_alignment;
    const uint64_t frame_size = 2 * (yplane_size + 2 * uvplane_size);

    uint16_t *buf = NULL;

#if !CONFIG_AV2_ENCODER
    // We should only need an 8-bit version of the source frame if we are
    // encoding in non-realtime mode
    (void)alloc_pyramid;
    assert(!alloc_pyramid);
#endif  // !CONFIG_AV2_ENCODER

#if defined AVM_MAX_ALLOCABLE_MEMORY
    // The size of ybf->buffer_alloc.
    uint64_t alloc_size = frame_size;
#if CONFIG_AV2_ENCODER
    // The size of ybf->y_pyramid
    if (alloc_pyramid) {
      alloc_size += avm_get_pyramid_alloc_size(width, height);
      alloc_size += av2_get_corner_list_size();
    }
#endif  // CONFIG_AV2_ENCODER
    // The decoder may allocate REF_FRAMES frame buffers in the frame buffer
    // pool. Bound the total amount of allocated memory as if these REF_FRAMES
    // frame buffers were allocated in a single allocation.
    if (alloc_size > AVM_MAX_ALLOCABLE_MEMORY / REF_FRAMES)
      return AVM_CODEC_MEM_ERROR;
#endif

    if (cb != NULL) {
      const int align_addr_extra_size = 31;
      const uint64_t external_frame_size = frame_size + align_addr_extra_size;

      assert(fb != NULL);

      if (external_frame_size != (size_t)external_frame_size)
        return AVM_CODEC_MEM_ERROR;

      // Allocation to hold larger frame, or first allocation.
      if (cb(cb_priv, (size_t)external_frame_size, fb) < 0)
        return AVM_CODEC_MEM_ERROR;

      if (fb->data == NULL || fb->size < external_frame_size)
        return AVM_CODEC_MEM_ERROR;

      ybf->buffer_alloc = (uint8_t *)avm_align_addr(fb->data, 32);

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
      // This memset is needed for fixing the issue of using uninitialized
      // value in msan test. It will cause a perf loss, so only do this for
      // msan test.
      memset(ybf->buffer_alloc, 0, (size_t)frame_size);
#endif
#endif
    } else if (frame_size > ybf->buffer_alloc_sz) {
      // Allocation to hold larger frame, or first allocation.
      avm_free(ybf->buffer_alloc);
      ybf->buffer_alloc = NULL;
      ybf->buffer_alloc_sz = 0;

      if (frame_size != (size_t)frame_size) return AVM_CODEC_MEM_ERROR;

      ybf->buffer_alloc = (uint8_t *)avm_memalign(32, (size_t)frame_size);
      if (!ybf->buffer_alloc) return AVM_CODEC_MEM_ERROR;

      ybf->buffer_alloc_sz = (size_t)frame_size;

      // This memset is needed for fixing valgrind error from C loop filter
      // due to access uninitialized memory in frame border. It could be
      // removed if border is totally removed.
      memset(ybf->buffer_alloc, 0, ybf->buffer_alloc_sz);
    }

    ybf->y_crop_width = width;
    ybf->y_crop_height = height;
    ybf->y_width = aligned_width;
    ybf->y_height = aligned_height;
    ybf->y_stride = y_stride;

    ybf->uv_crop_width = (width + ss_x) >> ss_x;
    ybf->uv_crop_height = (height + ss_y) >> ss_y;
    ybf->uv_width = uv_width;
    ybf->uv_height = uv_height;
    ybf->uv_stride = uv_stride;

    ybf->border = border;
    ybf->frame_size = (size_t)frame_size;
    ybf->subsampling_x = ss_x;
    ybf->subsampling_y = ss_y;

    // Store uint16 addresses when using 16bit framebuffers
    buf = (uint16_t *)ybf->buffer_alloc;

    ybf->y_buffer = (uint16_t *)avm_align_addr(
        buf + (border * y_stride) + border, avm_byte_align);
    ybf->u_buffer = (uint16_t *)avm_align_addr(
        buf + yplane_size + (uv_border_h * uv_stride) + uv_border_w,
        avm_byte_align);
    ybf->v_buffer =
        (uint16_t *)avm_align_addr(buf + yplane_size + uvplane_size +
                                       (uv_border_h * uv_stride) + uv_border_w,
                                   avm_byte_align);

    ybf->use_external_reference_buffers = 0;

#if CONFIG_AV2_ENCODER
    if (ybf->y_pyramid) {
      avm_free_pyramid(ybf->y_pyramid);
      ybf->y_pyramid = NULL;
    }
    if (ybf->corners) {
      av2_free_corner_list(ybf->corners);
      ybf->corners = NULL;
    }
    if (alloc_pyramid) {
      ybf->y_pyramid = avm_alloc_pyramid(width, height);
      if (!ybf->y_pyramid) return AVM_CODEC_MEM_ERROR;
      ybf->corners = av2_alloc_corner_list();
      if (!ybf->corners) return AVM_CODEC_MEM_ERROR;
    }
#endif  // CONFIG_AV2_ENCODER

    ybf->corrupted = 0; /* assume not corrupted by errors */
    return 0;
  }
  return AVM_CODEC_MEM_ERROR;
}

static int calc_stride_and_planesize(const int ss_x, const int ss_y,
                                     const int aligned_width,
                                     const int aligned_height, const int border,
                                     const int byte_alignment, int *y_stride,
                                     int *uv_stride, uint64_t *yplane_size,
                                     uint64_t *uvplane_size,
                                     const int uv_height) {
  /* Only support allocating buffers that have a border that's a multiple
   * of 32. The border restriction is required to get 16-byte alignment of
   * the start of the chroma rows without introducing an arbitrary gap
   * between planes, which would break the semantics of things like
   * avm_img_set_rect(). */
  if (border & 0x1f) return AVM_CODEC_MEM_ERROR;
  *y_stride = ((aligned_width + 2 * border) + 31) & ~31;
  *yplane_size =
      (aligned_height + 2 * border) * (uint64_t)(*y_stride) + byte_alignment;

  *uv_stride = *y_stride >> ss_x;
  *uvplane_size = (uv_height + 2 * (border >> ss_y)) * (uint64_t)(*uv_stride) +
                  byte_alignment;
  return 0;
}

int avm_realloc_frame_buffer(YV12_BUFFER_CONFIG *ybf, int width, int height,
                             int ss_x, int ss_y, int border, int byte_alignment,
                             avm_codec_frame_buffer_t *fb,
                             avm_get_frame_buffer_cb_fn_t cb, void *cb_priv,
                             bool alloc_pyramid) {
#if CONFIG_SIZE_LIMIT
  if (width > DECODE_WIDTH_LIMIT || height > DECODE_HEIGHT_LIMIT)
    return AVM_CODEC_MEM_ERROR;
#endif

  if (ybf) {
    int y_stride = 0;
    int uv_stride = 0;
    uint64_t yplane_size = 0;
    uint64_t uvplane_size = 0;
    const int aligned_width = (width + 7) & ~7;
    const int aligned_height = (height + 7) & ~7;
    const int uv_width = aligned_width >> ss_x;
    const int uv_height = aligned_height >> ss_y;
    const int uv_border_w = border >> ss_x;
    const int uv_border_h = border >> ss_y;

    int error = calc_stride_and_planesize(
        ss_x, ss_y, aligned_width, aligned_height, border, byte_alignment,
        &y_stride, &uv_stride, &yplane_size, &uvplane_size, uv_height);
    if (error) return error;
    return realloc_frame_buffer_aligned(
        ybf, width, height, ss_x, ss_y, border, byte_alignment, fb, cb, cb_priv,
        y_stride, yplane_size, uvplane_size, aligned_width, aligned_height,
        uv_width, uv_height, uv_stride, uv_border_w, uv_border_h,
        alloc_pyramid);
  }
  return AVM_CODEC_MEM_ERROR;
}

int avm_alloc_frame_buffer(YV12_BUFFER_CONFIG *ybf, int width, int height,
                           int ss_x, int ss_y, int border, int byte_alignment,
                           bool alloc_pyramid) {
  if (ybf) {
    avm_free_frame_buffer(ybf);
    return avm_realloc_frame_buffer(ybf, width, height, ss_x, ss_y, border,
                                    byte_alignment, NULL, NULL, NULL,
                                    alloc_pyramid);
  }
  return AVM_CODEC_MEM_ERROR;
}

void avm_remove_metadata_from_frame_buffer(YV12_BUFFER_CONFIG *ybf) {
  if (ybf && ybf->metadata) {
    avm_img_metadata_array_free(ybf->metadata);
    ybf->metadata = NULL;
  }
}

int avm_copy_metadata_to_frame_buffer(YV12_BUFFER_CONFIG *ybf,
                                      const avm_metadata_array_t *arr) {
  if (!ybf || !arr || !arr->metadata_array) return -1;
  if (ybf->metadata == arr) return 0;
  avm_remove_metadata_from_frame_buffer(ybf);
  ybf->metadata = avm_img_metadata_array_alloc(arr->sz);
  if (!ybf->metadata) return -1;
  for (size_t i = 0; i < ybf->metadata->sz; i++) {
    ybf->metadata->metadata_array[i] = avm_img_metadata_alloc(
        arr->metadata_array[i]->type, arr->metadata_array[i]->payload,
        arr->metadata_array[i]->sz, arr->metadata_array[i]->insert_flag);
    if (ybf->metadata->metadata_array[i] == NULL) {
      avm_img_metadata_array_free(ybf->metadata);
      ybf->metadata = NULL;
      return -1;
    }
  }
  ybf->metadata->sz = arr->sz;
  return 0;
}
