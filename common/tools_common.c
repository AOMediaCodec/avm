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
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avm_dsp/avm_dsp_common.h"
#include "common/tools_common.h"

#if CONFIG_AV2_ENCODER
#include "avm/avmcx.h"
#endif

#if CONFIG_AV2_DECODER
#include "avm/avmdx.h"
#endif

#if defined(_WIN32) || defined(__OS2__)
#include <io.h>
#include <fcntl.h>

#ifdef __OS2__
#define _setmode setmode
#define _fileno fileno
#define _O_BINARY O_BINARY
#endif
#endif

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

FILE *set_binary_mode(FILE *stream) {
  (void)stream;
#if defined(_WIN32) || defined(__OS2__)
  _setmode(_fileno(stream), _O_BINARY);
#endif
  return stream;
}

void die(const char *fmt, ...) {
  LOG_ERROR(NULL);
  usage_exit();
}

void fatal(const char *fmt, ...) {
  LOG_ERROR("Fatal");
  exit(EXIT_FAILURE);
}

void warn(const char *fmt, ...) { LOG_ERROR("Warning"); }

void die_codec(avm_codec_ctx_t *ctx, const char *s) {
  const char *detail = avm_codec_error_detail(ctx);

  printf("%s: %s\n", s, avm_codec_error(ctx));
  if (detail) printf("    %s\n", detail);
  exit(EXIT_FAILURE);
}

int read_yuv_frame(struct AvxInputContext *input_ctx, avm_image_t *yuv_frame) {
  FILE *f = input_ctx->file;
  struct FileTypeDetectionBuffer *detect = &input_ctx->detect;
  int plane = 0;
  int shortread = 0;
  const int bytespp = (yuv_frame->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;

  for (plane = 0; plane < 3; ++plane) {
    uint8_t *ptr;
    const int w = avm_img_plane_width(yuv_frame, plane);
    const int h = avm_img_plane_height(yuv_frame, plane);
    int r;

    /* Determine the correct plane based on the image format. The for-loop
     * always counts in Y,U,V order, but this may not match the order of
     * the data on disk.
     */
    switch (plane) {
      case 1:
        ptr =
            yuv_frame->planes[yuv_frame->fmt == AVM_IMG_FMT_YV12 ? AVM_PLANE_V
                                                                 : AVM_PLANE_U];
        break;
      case 2:
        ptr =
            yuv_frame->planes[yuv_frame->fmt == AVM_IMG_FMT_YV12 ? AVM_PLANE_U
                                                                 : AVM_PLANE_V];
        break;
      default: ptr = yuv_frame->planes[plane];
    }

    for (r = 0; r < h; ++r) {
      size_t needed = w * bytespp;
      size_t buf_position = 0;
      const size_t left = detect->buf_read - detect->position;
      if (left > 0) {
        const size_t more = (left < needed) ? left : needed;
        memcpy(ptr, detect->buf + detect->position, more);
        buf_position = more;
        needed -= more;
        detect->position += more;
      }
      if (needed > 0) {
        shortread |= (fread(ptr + buf_position, 1, needed, f) < needed);
      }

      ptr += yuv_frame->stride[plane];
    }
  }

  return shortread;
}

struct CodecInfo {
  // Pointer to a function of zero arguments that returns an avm_codec_iface_t.
  avm_codec_iface_t *(*const interface)();
  char *short_name;
  uint32_t fourcc;
};

#if CONFIG_AV2_ENCODER
static const struct CodecInfo avm_encoders[] = {
  { &avm_codec_av2_cx, "av2", AV2_FOURCC },
};

int get_avm_encoder_count(void) {
  return sizeof(avm_encoders) / sizeof(avm_encoders[0]);
}

avm_codec_iface_t *get_avm_encoder_by_index(int i) {
  assert(i >= 0 && i < get_avm_encoder_count());
  return avm_encoders[i].interface();
}

avm_codec_iface_t *get_avm_encoder_by_short_name(const char *name) {
  for (int i = 0; i < get_avm_encoder_count(); ++i) {
    const struct CodecInfo *info = &avm_encoders[i];
    if (strcmp(info->short_name, name) == 0) return info->interface();
  }
  return NULL;
}

uint32_t get_fourcc_by_avm_encoder(avm_codec_iface_t *iface) {
  for (int i = 0; i < get_avm_encoder_count(); ++i) {
    const struct CodecInfo *info = &avm_encoders[i];
    if (info->interface() == iface) {
      return info->fourcc;
    }
  }
  return 0;
}

const char *get_short_name_by_avm_encoder(avm_codec_iface_t *iface) {
  for (int i = 0; i < get_avm_encoder_count(); ++i) {
    const struct CodecInfo *info = &avm_encoders[i];
    if (info->interface() == iface) {
      return info->short_name;
    }
  }
  return NULL;
}

#endif  // CONFIG_AV2_ENCODER

#if CONFIG_AV2_DECODER
static const struct CodecInfo avm_decoders[] = {
  { &avm_codec_av2_dx, "av2", AV2_FOURCC },
};

int get_avm_decoder_count(void) {
  return sizeof(avm_decoders) / sizeof(avm_decoders[0]);
}

avm_codec_iface_t *get_avm_decoder_by_index(int i) {
  assert(i >= 0 && i < get_avm_decoder_count());
  return avm_decoders[i].interface();
}

avm_codec_iface_t *get_avm_decoder_by_short_name(const char *name) {
  for (int i = 0; i < get_avm_decoder_count(); ++i) {
    const struct CodecInfo *info = &avm_decoders[i];
    if (strcmp(info->short_name, name) == 0) return info->interface();
  }
  return NULL;
}

avm_codec_iface_t *get_avm_decoder_by_fourcc(uint32_t fourcc) {
  for (int i = 0; i < get_avm_decoder_count(); ++i) {
    const struct CodecInfo *info = &avm_decoders[i];
    if (info->fourcc == fourcc) return info->interface();
  }
  return NULL;
}

const char *get_short_name_by_avm_decoder(avm_codec_iface_t *iface) {
  for (int i = 0; i < get_avm_decoder_count(); ++i) {
    const struct CodecInfo *info = &avm_decoders[i];
    if (info->interface() == iface) {
      return info->short_name;
    }
  }
  return NULL;
}

uint32_t get_fourcc_by_avm_decoder(avm_codec_iface_t *iface) {
  for (int i = 0; i < get_avm_decoder_count(); ++i) {
    const struct CodecInfo *info = &avm_decoders[i];
    if (info->interface() == iface) {
      return info->fourcc;
    }
  }
  return 0;
}

#endif  // CONFIG_AV2_DECODER

void avm_img_write(const avm_image_t *img, FILE *file) {
  int plane;

  for (plane = 0; plane < 3; ++plane) {
    const unsigned char *buf = img->planes[plane];
    const int stride = img->stride[plane];
    const int w = avm_img_plane_width(img, plane) *
                  ((img->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1);
    const int h = avm_img_plane_height(img, plane);
    int y;

    for (y = 0; y < h; ++y) {
      fwrite(buf, 1, w, file);
      buf += stride;
    }
  }
}

int avm_img_read(avm_image_t *img, FILE *file) {
  int plane;

  for (plane = 0; plane < 3; ++plane) {
    unsigned char *buf = img->planes[plane];
    const int stride = img->stride[plane];
    const int w = avm_img_plane_width(img, plane) *
                  ((img->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1);
    const int h = avm_img_plane_height(img, plane);
    int y;

    for (y = 0; y < h; ++y) {
      if (fread(buf, 1, w, file) != (size_t)w) return 0;
      buf += stride;
    }
  }

  return 1;
}

// TODO(dkovalev) change sse_to_psnr signature: double -> int64_t
double sse_to_psnr(double samples, double peak, double sse) {
  static const double kMaxPSNR = 100.0;

  if (sse > 0.0) {
    const double psnr = 10.0 * log10(samples * peak * peak / sse);
    return psnr > kMaxPSNR ? kMaxPSNR : psnr;
  } else {
    return kMaxPSNR;
  }
}

// TODO(debargha): Consolidate the functions below into a separate file.
void avm_img_truncate_16_to_8(avm_image_t *dst, const avm_image_t *src) {
  int plane;
  if (dst->fmt + AVM_IMG_FMT_HIGHBITDEPTH != src->fmt || dst->d_w != src->d_w ||
      dst->d_h != src->d_h || dst->x_chroma_shift != src->x_chroma_shift ||
      dst->y_chroma_shift != src->y_chroma_shift) {
    fatal("Unsupported image conversion");
  }
  switch (dst->fmt) {
    case AVM_IMG_FMT_I420:
    case AVM_IMG_FMT_I422:
    case AVM_IMG_FMT_I444: break;
    default: fatal("Unsupported image conversion"); break;
  }
  for (plane = 0; plane < 3; plane++) {
    int w = src->d_w;
    int h = src->d_h;
    int x, y;
    if (plane) {
      w = (w + src->x_chroma_shift) >> src->x_chroma_shift;
      h = (h + src->y_chroma_shift) >> src->y_chroma_shift;
    }
    for (y = 0; y < h; y++) {
      const uint16_t *p_src =
          (const uint16_t *)(src->planes[plane] + y * src->stride[plane]);
      uint8_t *p_dst = dst->planes[plane] + y * dst->stride[plane];
      for (x = 0; x < w; x++) {
        *p_dst++ = (uint8_t)(*p_src++);
      }
    }
  }
}

static void highbd_img_downshift(avm_image_t *dst, const avm_image_t *src,
                                 int down_shift, int out_bit_depth) {
  (void)out_bit_depth;
  int plane;
  if (dst->d_w != src->d_w || dst->d_h != src->d_h ||
      dst->x_chroma_shift != src->x_chroma_shift ||
      dst->y_chroma_shift != src->y_chroma_shift || dst->fmt != src->fmt ||
      down_shift < 0) {
    fatal("Unsupported image conversion");
  }
  switch (src->fmt) {
    case AVM_IMG_FMT_I42016:
    case AVM_IMG_FMT_I42216:
    case AVM_IMG_FMT_I44416: break;
    default: fatal("Unsupported image conversion"); break;
  }
  for (plane = 0; plane < 3; plane++) {
    int w = src->d_w;
    int h = src->d_h;
    int x, y;
    if (plane) {
      w = (w + src->x_chroma_shift) >> src->x_chroma_shift;
      h = (h + src->y_chroma_shift) >> src->y_chroma_shift;
    }
    for (y = 0; y < h; y++) {
      const uint16_t *p_src =
          (const uint16_t *)(src->planes[plane] + y * src->stride[plane]);
      uint16_t *p_dst =
          (uint16_t *)(dst->planes[plane] + y * dst->stride[plane]);
      for (x = 0; x < w; x++) {
        const uint16_t v = (uint16_t)ROUND_POWER_OF_TWO(*p_src, down_shift);
        *p_dst++ = (uint16_t)clip_pixel_highbd(v, out_bit_depth);
        p_src++;
      }
    }
  }
}

static void lowbd_img_downshift(avm_image_t *dst, const avm_image_t *src,
                                int down_shift) {
  int plane;
  if (dst->d_w != src->d_w || dst->d_h != src->d_h ||
      dst->x_chroma_shift != src->x_chroma_shift ||
      dst->y_chroma_shift != src->y_chroma_shift ||
      src->fmt != dst->fmt + AVM_IMG_FMT_HIGHBITDEPTH || down_shift < 0) {
    fatal("Unsupported image conversion");
  }
  switch (dst->fmt) {
    case AVM_IMG_FMT_I420:
    case AVM_IMG_FMT_I422:
    case AVM_IMG_FMT_I444: break;
    default: fatal("Unsupported image conversion"); break;
  }
  for (plane = 0; plane < 3; plane++) {
    int w = src->d_w;
    int h = src->d_h;
    int x, y;
    if (plane) {
      w = (w + src->x_chroma_shift) >> src->x_chroma_shift;
      h = (h + src->y_chroma_shift) >> src->y_chroma_shift;
    }
    for (y = 0; y < h; y++) {
      const uint16_t *p_src =
          (const uint16_t *)(src->planes[plane] + y * src->stride[plane]);
      uint8_t *p_dst = dst->planes[plane] + y * dst->stride[plane];
      for (x = 0; x < w; x++) {
        const uint16_t v = (uint16_t)ROUND_POWER_OF_TWO(*p_src, down_shift);
        *p_dst++ = (uint8_t)clip_pixel(v);
        p_src++;
      }
    }
  }
}

void avm_img_downshift(avm_image_t *dst, const avm_image_t *src, int down_shift,
                       int out_bit_depth) {
  if (dst->fmt & AVM_IMG_FMT_HIGHBITDEPTH) {
    highbd_img_downshift(dst, src, down_shift, out_bit_depth);
  } else {
    lowbd_img_downshift(dst, src, down_shift);
  }
}

static int img_shifted_realloc_required(const avm_image_t *img,
                                        const avm_image_t *shifted,
                                        avm_img_fmt_t required_fmt) {
  return img->d_w != shifted->d_w || img->d_h != shifted->d_h ||
         required_fmt != shifted->fmt;
}

void avm_shift_img(unsigned int output_bit_depth, avm_image_t **img_ptr,
                   avm_image_t **img_shifted_ptr) {
  avm_image_t *img = *img_ptr;
  avm_image_t *img_shifted = *img_shifted_ptr;

  const avm_img_fmt_t shifted_fmt = output_bit_depth == 8
                                        ? img->fmt & ~AVM_IMG_FMT_HIGHBITDEPTH
                                        : img->fmt | AVM_IMG_FMT_HIGHBITDEPTH;

  if (shifted_fmt != img->fmt || output_bit_depth != img->bit_depth) {
    if (img_shifted &&
        img_shifted_realloc_required(img, img_shifted, shifted_fmt)) {
      avm_img_free(img_shifted);
      img_shifted = NULL;
    }
    if (img_shifted) {
      img_shifted->monochrome = img->monochrome;
    }
    if (!img_shifted) {
      img_shifted = avm_img_alloc(NULL, shifted_fmt, img->d_w, img->d_h, 16);
      img_shifted->bit_depth = output_bit_depth;
      img_shifted->monochrome = img->monochrome;
      img_shifted->csp = img->csp;
    }
    img_shifted->w_conf_win_enabled_flag = img->w_conf_win_enabled_flag;
    if (img_shifted->w_conf_win_enabled_flag) {
      img_shifted->w_conf_win_left_offset = img->w_conf_win_left_offset;
      img_shifted->w_conf_win_right_offset = img->w_conf_win_right_offset;
      img_shifted->w_conf_win_top_offset = img->w_conf_win_top_offset;
      img_shifted->w_conf_win_bottom_offset = img->w_conf_win_bottom_offset;
      img_shifted->crop_width = img->crop_width;
      img_shifted->crop_height = img->crop_height;
      img_shifted->w = img->max_width;
      img_shifted->h = img->max_height;
      img_shifted->d_w = img->d_w;
      img_shifted->d_h = img->d_h;
      img_shifted->max_width = img->crop_width;
      img_shifted->max_height = img->crop_height;
      img_shifted->tlayer_id = img->tlayer_id;
      img_shifted->mlayer_id = img->mlayer_id;
      img_shifted->xlayer_id = img->xlayer_id;
    } else {
      // TODO: Does this change during rpr
      img_shifted->w_conf_win_left_offset = 0;
      img_shifted->w_conf_win_right_offset = 0;
      img_shifted->w_conf_win_top_offset = 0;
      img_shifted->w_conf_win_bottom_offset = 0;
      img_shifted->max_width = img->d_w;
      img_shifted->max_height = img->d_h;
      img_shifted->tlayer_id = img->tlayer_id;
      img_shifted->mlayer_id = img->mlayer_id;
      img_shifted->xlayer_id = img->xlayer_id;
    }

    // img_shifted->x_chroma_shift = img->x_chroma_shift;
    // img_shifted->y_chroma_shift = img->y_chroma_shift;
    if (output_bit_depth > img->bit_depth) {
      avm_img_upshift(img_shifted, img, output_bit_depth - img->bit_depth);
    } else {
      avm_img_downshift(img_shifted, img, img->bit_depth - output_bit_depth,
                        output_bit_depth);
    }
    *img_shifted_ptr = img_shifted;
    *img_ptr = img_shifted;
  }
}

// Related to I420, NV12 format has one luma "luminance" plane Y and one plane
// with U and V values interleaved.
void avm_img_write_nv12(const avm_image_t *img, FILE *file) {
  // Y plane
  const unsigned char *buf = img->planes[0];
  int stride = img->stride[0];
  int w = avm_img_plane_width(img, 0) *
          ((img->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1);
  int h = avm_img_plane_height(img, 0);
  int x, y;

  for (y = 0; y < h; ++y) {
    fwrite(buf, 1, w, file);
    buf += stride;
  }

  // Interleaved U and V plane
  const unsigned char *ubuf = img->planes[1];
  const unsigned char *vbuf = img->planes[2];
  const size_t size = (img->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;
  stride = img->stride[1];
  w = avm_img_plane_width(img, 1);
  h = avm_img_plane_height(img, 1);

  for (y = 0; y < h; ++y) {
    for (x = 0; x < w; ++x) {
      fwrite(ubuf, size, 1, file);
      fwrite(vbuf, size, 1, file);
      ubuf += size;
      vbuf += size;
    }
    ubuf += (stride - w * size);
    vbuf += (stride - w * size);
  }
}
