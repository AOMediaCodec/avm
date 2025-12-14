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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/avm_config.h"

#include "avm/avm_image.h"
#include "avm/avm_integer.h"
#include "avm/internal/avm_image_internal.h"
#include "avm_mem/avm_mem.h"

static INLINE unsigned int align_image_dimension(unsigned int d,
                                                 unsigned int subsampling,
                                                 unsigned int size_align) {
  unsigned int align;

  align = (1 << subsampling) - 1;
  align = (size_align - 1 > align) ? (size_align - 1) : align;
  return ((d + align) & ~align);
}

static avm_image_t *img_alloc_helper(
    avm_image_t *img, avm_img_fmt_t fmt, unsigned int d_w, unsigned int d_h,
    unsigned int buf_align, unsigned int stride_align, unsigned int size_align,
    unsigned int border, unsigned char *img_data,
    avm_alloc_img_data_cb_fn_t alloc_cb, void *cb_priv) {
  /* NOTE: In this function, bit_depth is either 8 or 16 (if
   * AVM_IMG_FMT_HIGHBITDEPTH is set), never 10 or 12.
   */
  unsigned int h, w, s, xcs, ycs, bps, bit_depth;
  unsigned int stride_in_bytes;

  /* Treat align==0 like align==1 */
  if (!buf_align) buf_align = 1;

  /* Validate alignment (must be power of 2) */
  if (buf_align & (buf_align - 1)) goto fail;

  /* Treat align==0 like align==1 */
  if (!stride_align) stride_align = 1;

  /* Validate alignment (must be power of 2) */
  if (stride_align & (stride_align - 1)) goto fail;

  /* Treat align==0 like align==1 */
  if (!size_align) size_align = 1;

  /* Validate alignment (must be power of 2) */
  if (size_align & (size_align - 1)) goto fail;

  /* Get sample size for this format */
  switch (fmt) {
    case AVM_IMG_FMT_I420:
    case AVM_IMG_FMT_YV12:
    case AVM_IMG_FMT_AVMI420:
    case AVM_IMG_FMT_AVMYV12: bps = 12; break;
    case AVM_IMG_FMT_I422: bps = 16; break;
    case AVM_IMG_FMT_I444: bps = 24; break;
    case AVM_IMG_FMT_YV1216:
    case AVM_IMG_FMT_I42016: bps = 24; break;
    case AVM_IMG_FMT_I42216: bps = 32; break;
    case AVM_IMG_FMT_I44416: bps = 48; break;
    default: bps = 16; break;
  }

  bit_depth = (fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 16 : 8;

  /* Get chroma shift values for this format */
  switch (fmt) {
    case AVM_IMG_FMT_I420:
    case AVM_IMG_FMT_YV12:
    case AVM_IMG_FMT_AVMI420:
    case AVM_IMG_FMT_AVMYV12:
    case AVM_IMG_FMT_I422:
    case AVM_IMG_FMT_I42016:
    case AVM_IMG_FMT_YV1216:
    case AVM_IMG_FMT_I42216: xcs = 1; break;
    default: xcs = 0; break;
  }

  switch (fmt) {
    case AVM_IMG_FMT_I420:
    case AVM_IMG_FMT_YV12:
    case AVM_IMG_FMT_AVMI420:
    case AVM_IMG_FMT_AVMYV12:
    case AVM_IMG_FMT_YV1216:
    case AVM_IMG_FMT_I42016: ycs = 1; break;
    default: ycs = 0; break;
  }

  /* Calculate storage sizes given the chroma subsampling */
  w = align_image_dimension(d_w, xcs, size_align);
  h = align_image_dimension(d_h, ycs, size_align);

  s = (fmt & AVM_IMG_FMT_PLANAR) ? w : bps * w / bit_depth;
  s = (s + 2 * border + stride_align - 1) & ~(stride_align - 1);
  stride_in_bytes = s * bit_depth / 8;

  /* Allocate the new image */
  if (!img) {
    img = (avm_image_t *)calloc(1, sizeof(avm_image_t));

    if (!img) goto fail;

    img->self_allocd = 1;
  } else {
    memset(img, 0, sizeof(avm_image_t));
  }

  img->img_data = img_data;

  if (!img_data) {
    const uint64_t alloc_size =
        (fmt & AVM_IMG_FMT_PLANAR)
            ? (uint64_t)(h + 2 * border) * stride_in_bytes * bps / bit_depth
            : (uint64_t)(h + 2 * border) * stride_in_bytes;

    if (alloc_size != (size_t)alloc_size) goto fail;

    if (alloc_cb) {
      const size_t padded_alloc_size = (size_t)alloc_size + buf_align - 1;
      img->img_data = (uint8_t *)alloc_cb(cb_priv, padded_alloc_size);
      if (img->img_data) {
        img->img_data = (uint8_t *)avm_align_addr(img->img_data, buf_align);
      }
      img->img_data_owner = 0;
    } else {
      img->img_data = (uint8_t *)avm_memalign(buf_align, (size_t)alloc_size);
      img->img_data_owner = 1;
    }
    img->sz = (size_t)alloc_size;
  }

  if (!img->img_data) goto fail;

  img->fmt = fmt;
  img->bit_depth = bit_depth;
  // aligned width and aligned height
  img->w = w;
  img->h = h;
  img->x_chroma_shift = xcs;
  img->y_chroma_shift = ycs;
  img->bps = bps;

  /* Calculate strides */
  img->stride[AVM_PLANE_Y] = stride_in_bytes;
  img->stride[AVM_PLANE_U] = img->stride[AVM_PLANE_V] = stride_in_bytes >> xcs;

  /* Default viewport to entire image. (This avm_img_set_rect call always
   * succeeds.) */
  avm_img_set_rect(img, 0, 0, d_w, d_h, border);
  return img;

fail:
  avm_img_free(img);
  return NULL;
}

avm_image_t *avm_img_alloc(avm_image_t *img, avm_img_fmt_t fmt,
                           unsigned int d_w, unsigned int d_h,
                           unsigned int align) {
  return img_alloc_helper(img, fmt, d_w, d_h, align, align, 1, 0, NULL, NULL,
                          NULL);
}

avm_image_t *avm_img_alloc_with_cb(avm_image_t *img, avm_img_fmt_t fmt,
                                   unsigned int d_w, unsigned int d_h,
                                   unsigned int align,
                                   avm_alloc_img_data_cb_fn_t alloc_cb,
                                   void *cb_priv) {
  return img_alloc_helper(img, fmt, d_w, d_h, align, align, 1, 0, NULL,
                          alloc_cb, cb_priv);
}

avm_image_t *avm_img_wrap(avm_image_t *img, avm_img_fmt_t fmt, unsigned int d_w,
                          unsigned int d_h, unsigned int stride_align,
                          unsigned char *img_data) {
  /* Set buf_align = 1. It is ignored by img_alloc_helper because img_data is
   * not NULL. */
  return img_alloc_helper(img, fmt, d_w, d_h, 1, stride_align, 1, 0, img_data,
                          NULL, NULL);
}

avm_image_t *avm_img_alloc_with_border(avm_image_t *img, avm_img_fmt_t fmt,
                                       unsigned int d_w, unsigned int d_h,
                                       unsigned int align,
                                       unsigned int size_align,
                                       unsigned int border) {
  return img_alloc_helper(img, fmt, d_w, d_h, align, align, size_align, border,
                          NULL, NULL, NULL);
}

int avm_img_set_rect(avm_image_t *img, unsigned int x, unsigned int y,
                     unsigned int w, unsigned int h, unsigned int border) {
  unsigned char *data;

  if (x + w <= img->w && y + h <= img->h) {
    img->d_w = w;
    img->d_h = h;

    x += border;
    y += border;

    /* Calculate plane pointers */
    if (!(img->fmt & AVM_IMG_FMT_PLANAR)) {
      img->planes[AVM_PLANE_PACKED] =
          img->img_data + x * img->bps / 8 + y * img->stride[AVM_PLANE_PACKED];
    } else {
      const int bytes_per_sample =
          (img->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;
      data = img->img_data;

      img->planes[AVM_PLANE_Y] =
          data + x * bytes_per_sample + y * img->stride[AVM_PLANE_Y];
      data += (img->h + 2 * border) * img->stride[AVM_PLANE_Y];

      unsigned int uv_border_h = border >> img->y_chroma_shift;
      unsigned int uv_x = x >> img->x_chroma_shift;
      unsigned int uv_y = y >> img->y_chroma_shift;
      if (!(img->fmt & AVM_IMG_FMT_UV_FLIP)) {
        img->planes[AVM_PLANE_U] =
            data + uv_x * bytes_per_sample + uv_y * img->stride[AVM_PLANE_U];
        data += ((img->h >> img->y_chroma_shift) + 2 * uv_border_h) *
                img->stride[AVM_PLANE_U];
        img->planes[AVM_PLANE_V] =
            data + uv_x * bytes_per_sample + uv_y * img->stride[AVM_PLANE_V];
      } else {
        img->planes[AVM_PLANE_V] =
            data + uv_x * bytes_per_sample + uv_y * img->stride[AVM_PLANE_V];
        data += ((img->h >> img->y_chroma_shift) + 2 * uv_border_h) *
                img->stride[AVM_PLANE_V];
        img->planes[AVM_PLANE_U] =
            data + uv_x * bytes_per_sample + uv_y * img->stride[AVM_PLANE_U];
      }
    }
    return 0;
  }
  return -1;
}

void avm_img_flip(avm_image_t *img) {
  /* Note: In the calculation pointer adjustment calculation, we want the
   * rhs to be promoted to a signed type. Section 6.3.1.8 of the ISO C99
   * standard indicates that if the adjustment parameter is unsigned, the
   * stride parameter will be promoted to unsigned, causing errors when
   * the lhs is a larger type than the rhs.
   */
  img->planes[AVM_PLANE_Y] += (signed)(img->d_h - 1) * img->stride[AVM_PLANE_Y];
  img->stride[AVM_PLANE_Y] = -img->stride[AVM_PLANE_Y];

  img->planes[AVM_PLANE_U] += (signed)((img->d_h >> img->y_chroma_shift) - 1) *
                              img->stride[AVM_PLANE_U];
  img->stride[AVM_PLANE_U] = -img->stride[AVM_PLANE_U];

  img->planes[AVM_PLANE_V] += (signed)((img->d_h >> img->y_chroma_shift) - 1) *
                              img->stride[AVM_PLANE_V];
  img->stride[AVM_PLANE_V] = -img->stride[AVM_PLANE_V];
}

void avm_img_free(avm_image_t *img) {
  if (img) {
    avm_img_remove_metadata(img);
    if (img->img_data && img->img_data_owner) avm_free(img->img_data);

    if (img->self_allocd) free(img);
  }
}

int avm_img_plane_width(const avm_image_t *img, int plane) {
  if (plane > 0 && img->x_chroma_shift > 0)
    return (img->d_w + 1) >> img->x_chroma_shift;
  else
    return img->d_w;
}

int avm_img_plane_height(const avm_image_t *img, int plane) {
  if (plane > 0 && img->y_chroma_shift > 0)
    return (img->d_h + 1) >> img->y_chroma_shift;
  else
    return img->d_h;
}

avm_metadata_t *avm_img_metadata_alloc(
    uint32_t type, const uint8_t *data, size_t sz,
    avm_metadata_insert_flags_t insert_flag) {
  if (!data || sz == 0) return NULL;

  avm_metadata_t *metadata = (avm_metadata_t *)malloc(sizeof(avm_metadata_t));
  if (!metadata) return NULL;

  metadata->type = type;
  metadata->payload = (uint8_t *)malloc(sz);
  if (!metadata->payload) {
    free(metadata);
    return NULL;
  }
  memcpy(metadata->payload, data, sz);
  metadata->sz = sz;
  metadata->insert_flag = insert_flag;

#if CONFIG_METADATA
  metadata->is_suffix = 0;
  metadata->necessity_idc = AVM_NECESSITY_UNDEFINED;
  metadata->application_id = AVM_APPID_UNDEFINED;
  metadata->cancel_flag = 0;
  metadata->priority = 0;
  metadata->persistence_idc = AVM_GLOBAL_PERSISTENCE;
  metadata->layer_idc = AVM_LAYER_UNSPECIFIED;
  metadata->xlayer_map = 0;
  memset(metadata->mlayer_map, 0, sizeof(metadata->mlayer_map));
#endif  // CONFIG_METADATA
  return metadata;
}

void avm_img_metadata_free(avm_metadata_t *metadata) {
  if (metadata) {
    if (metadata->payload) free(metadata->payload);
    free(metadata);
  }
}

avm_metadata_array_t *avm_img_metadata_array_alloc(size_t sz) {
  avm_metadata_array_t *arr =
      (avm_metadata_array_t *)calloc(1, sizeof(avm_metadata_array_t));
  if (!arr) return NULL;
  if (sz > 0) {
    arr->metadata_array =
        (avm_metadata_t **)calloc(sz, sizeof(avm_metadata_t *));
    if (!arr->metadata_array) {
      avm_img_metadata_array_free(arr);
      return NULL;
    }
    arr->sz = sz;
  }
  return arr;
}

void avm_img_metadata_array_free(avm_metadata_array_t *arr) {
  if (arr) {
    if (arr->metadata_array) {
      for (size_t i = 0; i < arr->sz; i++) {
        avm_img_metadata_free(arr->metadata_array[i]);
      }
      free(arr->metadata_array);
    }
    free(arr);
  }
}

int avm_img_add_metadata(avm_image_t *img, uint32_t type, const uint8_t *data,
                         size_t sz, avm_metadata_insert_flags_t insert_flag) {
  if (!img) return -1;
  if (!img->metadata) {
    img->metadata = avm_img_metadata_array_alloc(0);
    if (!img->metadata) return -1;
  }
  avm_metadata_t *metadata =
      avm_img_metadata_alloc(type, data, sz, insert_flag);
  if (!metadata) return -1;
  avm_metadata_t **metadata_array =
      (avm_metadata_t **)realloc(img->metadata->metadata_array,
                                 (img->metadata->sz + 1) * sizeof(metadata));
  if (!metadata_array) {
    avm_img_metadata_free(metadata);
    return -1;
  }
  img->metadata->metadata_array = metadata_array;
  img->metadata->metadata_array[img->metadata->sz] = metadata;
  img->metadata->sz++;
  return 0;
}

void avm_img_remove_metadata(avm_image_t *img) {
  if (img && img->metadata) {
    avm_img_metadata_array_free(img->metadata);
    img->metadata = NULL;
  }
}

const avm_metadata_t *avm_img_get_metadata(const avm_image_t *img,
                                           size_t index) {
  if (!img) return NULL;
  const avm_metadata_array_t *array = img->metadata;
  if (array && index < array->sz) {
    return array->metadata_array[index];
  }
  return NULL;
}

size_t avm_img_num_metadata(const avm_image_t *img) {
  if (!img || !img->metadata) return 0;
  return img->metadata->sz;
}

#define LOG_ERROR(label)               \
  do {                                 \
    const char *l = label;             \
    va_list ap;                        \
    va_start(ap, fmt);                 \
    if (l) fprintf(stderr, "%s: ", l); \
    vfprintf(stderr, fmt, ap);         \
    fprintf(stderr, "\n");             \
    va_end(ap);                        \
  } while (0)

#if defined(__GNUC__)
#define AVM_NO_RETURN __attribute__((noreturn))
#else
#define AVM_NO_RETURN
#endif

AVM_NO_RETURN static void fatal(const char *fmt, ...) {
  LOG_ERROR("Fatal");
  exit(EXIT_FAILURE);
}

static void highbd_img_upshift(avm_image_t *dst, const avm_image_t *src,
                               int input_shift) {
  const int offset = 0;
  int plane;
  if (dst->d_w != src->d_w || dst->d_h != src->d_h ||
      dst->x_chroma_shift != src->x_chroma_shift ||
      dst->y_chroma_shift != src->y_chroma_shift || dst->fmt != src->fmt ||
      input_shift < 0) {
    fatal("Unsupported image conversion");
  }
  switch (src->fmt) {
    case AVM_IMG_FMT_I42016:
    case AVM_IMG_FMT_I42216:
    case AVM_IMG_FMT_I44416: break;
    default: fatal("Unsupported image conversion"); break;
  }
  for (plane = 0; plane < (dst->monochrome ? 1 : 3); plane++) {
    int w = src->d_w;
    int h = src->d_h;
    int x, y;
    if (plane) {
      w = (w + src->x_chroma_shift) >> src->x_chroma_shift;
      h = (h + src->y_chroma_shift) >> src->y_chroma_shift;
      if (src->monochrome) {
        // Fill destination UV planes with a plain value.
        for (y = 0; y < h; y++) {
          uint16_t *p_dst =
              (uint16_t *)(dst->planes[plane] + y * dst->stride[plane]);
          for (x = 0; x < w; x++) *p_dst++ = offset;
        }
        continue;
      }
    }
    for (y = 0; y < h; y++) {
      const uint16_t *p_src =
          (const uint16_t *)(src->planes[plane] + y * src->stride[plane]);
      uint16_t *p_dst =
          (uint16_t *)(dst->planes[plane] + y * dst->stride[plane]);
      for (x = 0; x < w; x++) *p_dst++ = (*p_src++ << input_shift) + offset;
    }
  }
}

static void lowbd_img_upshift(avm_image_t *dst, const avm_image_t *src,
                              int input_shift) {
  const int offset = 0;
  int plane;
  if (dst->d_w != src->d_w || dst->d_h != src->d_h ||
      dst->x_chroma_shift != src->x_chroma_shift ||
      dst->y_chroma_shift != src->y_chroma_shift ||
      dst->fmt != src->fmt + AVM_IMG_FMT_HIGHBITDEPTH || input_shift < 0) {
    fatal("Unsupported image conversion");
  }
  switch (src->fmt) {
    case AVM_IMG_FMT_YV12:
    case AVM_IMG_FMT_I420:
    case AVM_IMG_FMT_I422:
    case AVM_IMG_FMT_I444: break;
    default: fatal("Unsupported image conversion"); break;
  }
  for (plane = 0; plane < (dst->monochrome ? 1 : 3); plane++) {
    int w = src->d_w;
    int h = src->d_h;
    int x, y;
    if (plane) {
      w = (w + src->x_chroma_shift) >> src->x_chroma_shift;
      h = (h + src->y_chroma_shift) >> src->y_chroma_shift;
      if (src->monochrome) {
        // Fill destination UV planes with a plain value.
        for (y = 0; y < h; y++) {
          uint16_t *p_dst =
              (uint16_t *)(dst->planes[plane] + y * dst->stride[plane]);
          for (x = 0; x < w; x++) *p_dst++ = offset;
        }
        continue;
      }
    }
    for (y = 0; y < h; y++) {
      const uint8_t *p_src = src->planes[plane] + y * src->stride[plane];
      uint16_t *p_dst =
          (uint16_t *)(dst->planes[plane] + y * dst->stride[plane]);
      for (x = 0; x < w; x++) {
        *p_dst++ = (*p_src++ << input_shift) + offset;
      }
    }
  }
}

void avm_img_upshift(avm_image_t *dst, const avm_image_t *src,
                     int input_shift) {
  if (src->fmt & AVM_IMG_FMT_HIGHBITDEPTH) {
    highbd_img_upshift(dst, src, input_shift);
  } else {
    lowbd_img_upshift(dst, src, input_shift);
  }
}
