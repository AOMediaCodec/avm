/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <math.h>
#include <string.h>

#include "av2/decoder/annexD.h"

#include "av2/common/enums.h"
#include "av2/common/resize.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/mem.h"

// BT.709 limited-range RGB (8-bit) to Y'CbCr at the given bit depth.
static void rgb_to_yuv_bt709_limited(int r, int g, int b, int bd, uint16_t *y,
                                     uint16_t *u, uint16_t *v) {
  const double yf = 0.2126 * r + 0.7152 * g + 0.0722 * b;    // [0,255]
  const double uf = -0.114572 * r - 0.385428 * g + 0.5 * b;  // [-128,128]
  const double vf = 0.5 * r - 0.454153 * g - 0.045847 * b;   // [-128,128]
  const double scale = (double)(1 << (bd - 8));
  const int maxv = (1 << bd) - 1;
  *y = (uint16_t)clamp((int)lround((16.0 + yf * 219.0 / 255.0) * scale), 0,
                       maxv);
  *u = (uint16_t)clamp((int)lround((128.0 + uf * 224.0 / 255.0) * scale), 0,
                       maxv);
  *v = (uint16_t)clamp((int)lround((128.0 + vf * 224.0 / 255.0) * scale), 0,
                       maxv);
}

// Scale a sample value from `src_bitdepth` to `dst_bitdepth` bits: upconvert is
// a left shift (zero fill), downconvert is Round2, identity when equal.
static AVM_INLINE int scale_sample(int val, int src_bitdepth,
                                   int dst_bitdepth) {
  if (src_bitdepth == dst_bitdepth) return val;
  if (dst_bitdepth > src_bitdepth) return val << (dst_bitdepth - src_bitdepth);
  return (int)ROUND_POWER_OF_TWO_64((int64_t)val, src_bitdepth - dst_bitdepth);
}

static void fill_plane(uint16_t *buf, int stride, int w, int h, uint16_t val) {
  for (int y = 0; y < h; ++y) avm_memset16(buf + y * stride, val, w);
}

// Copy a plane region, scaling each sample from `src_bitdepth` to
// `dst_bitdepth` bits (memcpy fast-path when the depths are equal).
static void copy_plane_region(const uint16_t *src, int src_stride,
                              uint16_t *dst, int dst_stride, int w, int h,
                              int src_bitdepth, int dst_bitdepth) {
  if (src_bitdepth == dst_bitdepth) {
    for (int y = 0; y < h; ++y)
      memcpy(dst + y * dst_stride, src + y * src_stride,
             (size_t)w * sizeof(*dst));
  } else {
    for (int y = 0; y < h; ++y) {
      const uint16_t *srow = src + y * src_stride;
      uint16_t *drow = dst + y * dst_stride;
      for (int x = 0; x < w; ++x)
        drow[x] = (uint16_t)scale_sample(srow[x], src_bitdepth, dst_bitdepth);
    }
  }
}

// A segment's canvas rectangle (luma coordinates).
typedef struct {
  int x, y, w, h;
} SegRect;

static AVM_INLINE SegRect seg_rect(const AtlasBasicInfo *bi, int i) {
  SegRect r = { bi->ats_segment_top_left_pos_x[i],
                bi->ats_segment_top_left_pos_y[i], bi->ats_segment_width[i],
                bi->ats_segment_height[i] };
  return r;
}

// True if `src` matches `canvas`'s chroma subsampling and monochrome/color
// layout. Chroma-format conversion is not implemented, so a mismatch is
// rejected.
static AVM_INLINE int chroma_format_matches(const YV12_BUFFER_CONFIG *src,
                                            const YV12_BUFFER_CONFIG *canvas) {
  return src->subsampling_x == canvas->subsampling_x &&
         src->subsampling_y == canvas->subsampling_y &&
         (!!src->monochrome) == (!!canvas->monochrome);
}

// Returns `src` resampled to (w,h) when its size differs, else `src` itself.
// On resample, *tmp holds the new buffer and the return value == tmp; the
// caller frees it iff the return value == tmp. Returns NULL on allocation
// failure. The chroma subsampling of `tmp` follows `src`; `num_planes` controls
// how many planes are resized (pass 1 to resample only luma, e.g. an alpha
// matte).
static const YV12_BUFFER_CONFIG *resample_to_segment(
    const YV12_BUFFER_CONFIG *src, int w, int h, int num_planes,
    YV12_BUFFER_CONFIG *tmp) {
  // Return the src pointer if it is already the correct size
  if (src->y_crop_width == w && src->y_crop_height == h) return src;

  // Allocate frame memory for the resize buffer
  memset(tmp, 0, sizeof(*tmp));
  if (avm_alloc_frame_buffer(tmp, w, h, src->subsampling_x, src->subsampling_y,
                             0, 0, false))
    return NULL;

  // Perform the resizing
  tmp->bit_depth = src->bit_depth;
  av2_resize_and_extend_frame_nonnormative(src, tmp, (int)src->bit_depth,
                                           num_planes);
  return tmp;
}

// Annex D spatial mapping process: place `src` (resampled to the segment size)
// opaquely into the canvas at segment `i`, scaling to the canvas bit depth.
// Returns 0 on success, -1 on a format mismatch or allocation failure.
static int compose_segment(const AtlasBasicInfo *bi, int segIdx,
                           const YV12_BUFFER_CONFIG *src,
                           YV12_BUFFER_CONFIG *canvas) {
  // Chroma format must match the canvas; bit depth may differ (scaled below).
  if (!chroma_format_matches(src, canvas)) return -1;

  // Convenience variables
  const int subX = canvas->subsampling_x;
  const int subY = canvas->subsampling_y;
  const int num_planes = canvas->monochrome ? 1 : 3;

  // Segment placement rectangle on the canvas (luma coordinates).
  const int seg_x = bi->ats_segment_top_left_pos_x[segIdx];
  const int seg_y = bi->ats_segment_top_left_pos_y[segIdx];
  const int seg_w = bi->ats_segment_width[segIdx];
  const int seg_h = bi->ats_segment_height[segIdx];

  // Empty or fully off-canvas segment: nothing to draw (not an error).
  if (seg_w <= 0 || seg_h <= 0) return 0;
  if (seg_x >= canvas->y_crop_width || seg_y >= canvas->y_crop_height) return 0;

  // Resample the source to the segment size (returns `src` unchanged when the
  // size already matches; `tmp` owns any allocation, freed at the end).
  YV12_BUFFER_CONFIG tmp;
  const YV12_BUFFER_CONFIG *src_resized =
      resample_to_segment(src, seg_w, seg_h, num_planes, &tmp);
  if (src_resized == NULL) return -1;

  // Clamp the copy extent to the canvas edge (segments may overhang).
  const int w = AVMMIN(seg_w, canvas->y_crop_width - seg_x);
  const int h = AVMMIN(seg_h, canvas->y_crop_height - seg_y);
  const int src_bd = (int)src->bit_depth;
  const int dst_bd = (int)canvas->bit_depth;

  // Copy the luma plane into the canvas
  copy_plane_region(src_resized->y_buffer, src_resized->y_stride,
                    canvas->y_buffer + seg_y * canvas->y_stride + seg_x,
                    canvas->y_stride, w, h, src_bd, dst_bd);

  // Copy the chroma planes into the canvas (accounting for sub-sampling)
  if (num_planes > 1) {
    const int cw = w >> subX, ch = h >> subY;
    if (cw > 0 && ch > 0) {  // chroma may vanish for tiny segments
      const int off = (seg_y >> subY) * canvas->uv_stride + (seg_x >> subX);
      copy_plane_region(src_resized->u_buffer, src_resized->uv_stride,
                        canvas->u_buffer + off, canvas->uv_stride, cw, ch,
                        src_bd, dst_bd);
      copy_plane_region(src_resized->v_buffer, src_resized->uv_stride,
                        canvas->v_buffer + off, canvas->uv_stride, cw, ch,
                        src_bd, dst_bd);
    }
  }

  if (src_resized == &tmp) avm_free_frame_buffer(&tmp);
  return 0;
}

// Annex D spatial-mapping-with-alpha blend for one plane. `src` and `dst` point
// at the plane's top-left within the segment; the alpha matte is sampled at the
// co-located luma position via the (sx,sy) chroma shift. Each source (texture)
// sample is scaled from `src_bitdepth` to `dst_bitdepth` before blending, so a
// texture coded at a different bit depth than the canvas is handled here (the
// matte stays at `alpha_bitdepth`, which drives alphaMax and the Round2 shift).
static void copy_and_blend_plane_region(const uint16_t *src, int src_stride,
                                        uint16_t *dst, int dst_stride, int w,
                                        int h, int sx, int sy, int posX,
                                        int posY, const uint16_t *alpha,
                                        int alpha_stride, const SegRect *a,
                                        int src_bitdepth, int dst_bitdepth,
                                        int alpha_bitdepth) {
  const int64_t alphaMax = (int64_t)1 << alpha_bitdepth;
  for (int y = 0; y < h; ++y) {
    const uint16_t *src_row = src + y * src_stride;
    uint16_t *dst_row = dst + y * dst_stride;
    for (int x = 0; x < w; ++x) {
      const int alpha_x = ((x << sx) + posX) - a->x;
      const int alpha_y = ((y << sy) + posY) - a->y;
      const int64_t alpha_val =
          (alpha_x >= 0 && alpha_x < a->w && alpha_y >= 0 && alpha_y < a->h)
              ? alpha[alpha_y * alpha_stride + alpha_x]
              : alphaMax;
      const int64_t blend_val =
          (alphaMax - alpha_val) * dst_row[x] +
          alpha_val * scale_sample(src_row[x], src_bitdepth, dst_bitdepth);
      dst_row[x] = (uint16_t)ROUND_POWER_OF_TWO_64(blend_val, alpha_bitdepth);
    }
  }
}

// Annex D spatial mapping with alpha: blend texture segment `segIdx` onto the
// canvas using the alpha matte (luma plane) of segment `segIdxAlpha`.
// Returns 0 on success, -1 on a format mismatch or allocation failure.
static int compose_segment_with_alpha(const AtlasBasicInfo *bi, int segIdx,
                                      const YV12_BUFFER_CONFIG *src,
                                      int segIdxAlpha,
                                      const YV12_BUFFER_CONFIG *alpha,
                                      YV12_BUFFER_CONFIG *canvas) {
  const int subX = canvas->subsampling_x;
  const int subY = canvas->subsampling_y;
  const int dst_bd = (int)canvas->bit_depth;
  const int num_planes = canvas->monochrome ? 1 : 3;

  if (!chroma_format_matches(src, canvas)) return -1;
  const SegRect t = seg_rect(bi, segIdx);
  if (t.w <= 0 || t.h <= 0) return 0;
  if (t.x >= canvas->y_crop_width || t.y >= canvas->y_crop_height) return 0;
  const SegRect a = seg_rect(bi, segIdxAlpha);
  if (a.w <= 0 || a.h <= 0) return -1;

  const int src_bd = (int)src->bit_depth;
  const int bd_alpha = (int)alpha->bit_depth;

  YV12_BUFFER_CONFIG ttmp, atmp;
  const YV12_BUFFER_CONFIG *rt =
      resample_to_segment(src, t.w, t.h, num_planes, &ttmp);
  if (rt == NULL) return -1;
  const YV12_BUFFER_CONFIG *ra = resample_to_segment(alpha, a.w, a.h, 1, &atmp);
  if (ra == NULL) {
    if (rt == &ttmp) avm_free_frame_buffer(&ttmp);
    return -1;
  }

  const int w = AVMMIN(t.w, canvas->y_crop_width - t.x);
  const int h = AVMMIN(t.h, canvas->y_crop_height - t.y);
  uint16_t *cbuf[3] = { canvas->y_buffer, canvas->u_buffer, canvas->v_buffer };
  const uint16_t *tbuf[3] = { rt->y_buffer, rt->u_buffer, rt->v_buffer };
  const int cstr[3] = { canvas->y_stride, canvas->uv_stride,
                        canvas->uv_stride };
  const int tstr[3] = { rt->y_stride, rt->uv_stride, rt->uv_stride };
  for (int p = 0; p < num_planes; ++p) {
    const int sx = p ? subX : 0, sy = p ? subY : 0;
    const int pw = w >> sx, ph = h >> sy;
    if (pw <= 0 || ph <= 0) continue;
    copy_and_blend_plane_region(tbuf[p], tstr[p],
                                cbuf[p] + (t.y >> sy) * cstr[p] + (t.x >> sx),
                                cstr[p], pw, ph, sx, sy, t.x, t.y, ra->y_buffer,
                                ra->y_stride, &a, src_bd, dst_bd, bd_alpha);
  }

  if (rt == &ttmp) avm_free_frame_buffer(&ttmp);
  if (ra == &atmp) avm_free_frame_buffer(&atmp);
  return 0;
}

// The main composition loop.  Perform the operations described in Annex D
// to update the canvas using the MULTISTREAM_ATLAS or MULTISTREAM_ALPHA_ATLAS
// messages.
int av2_annex_d_compose_tu(const AtlasSegmentInfo *atlas,
                           YV12_BUFFER_CONFIG *const *seg_src, int num_segments,
                           YV12_BUFFER_CONFIG *canvas) {
  // Error checking
  if (atlas == NULL || atlas->ats_basic_info == NULL || seg_src == NULL ||
      canvas == NULL) {
    return -1;
  }
  if (atlas->atlas_segment_mode_idc != MULTISTREAM_ATLAS &&
      atlas->atlas_segment_mode_idc != MULTISTREAM_ALPHA_ATLAS) {
    return -1;
  }
  if (num_segments < 0 || num_segments > MAX_NUM_ATLAS_SEGMENTS) return -1;

  // Convenience variables
  const AtlasBasicInfo *const bi = atlas->ats_basic_info;
  const int num_planes = canvas->monochrome ? 1 : 3;

  // Construction process
  //
  // Step 1. Determine the background color.  We currently assume a BT.709
  //         canvas in this implementation.  Default to limited-range black
  //         when no background is signaled.
  uint16_t bgY, bgU, bgV;
  if (bi->ats_background_info_present_flag) {
    rgb_to_yuv_bt709_limited(
        bi->ats_background_red_value, bi->ats_background_green_value,
        bi->ats_background_blue_value, canvas->bit_depth, &bgY, &bgU, &bgV);
  } else {
    const int scale = 1 << (canvas->bit_depth - 8);
    bgY = (uint16_t)(16 * scale);
    bgU = bgV = (uint16_t)(128 * scale);
  }

  // Step 2. Initialize the canvas to the background color.
  fill_plane(canvas->y_buffer, canvas->y_stride, canvas->y_crop_width,
             canvas->y_crop_height, bgY);
  if (num_planes > 1) {
    fill_plane(canvas->u_buffer, canvas->uv_stride, canvas->uv_crop_width,
               canvas->uv_crop_height, bgU);
    fill_plane(canvas->v_buffer, canvas->uv_stride, canvas->uv_crop_width,
               canvas->uv_crop_height, bgV);
  }

  // Step 3. Place each segment in order; later segments overwrite earlier ones.
  //         In alpha mode an alpha segment (ats_alpha_segment_flag == 1)
  //         carries the matte and is paired with the following texture segment.
  switch (atlas->atlas_segment_mode_idc) {
    case MULTISTREAM_ATLAS:
      for (int i = 0; i < num_segments; ++i) {
        const YV12_BUFFER_CONFIG *src = seg_src[i];
        if (src == NULL)
          continue;  // No frame for this stream: leave background.
        if (compose_segment(bi, i, src, canvas) != 0) {
          return -1;
        }
      }
      break;

    case MULTISTREAM_ALPHA_ATLAS:
      for (int i = 0; i < num_segments; ++i) {
        YV12_BUFFER_CONFIG *alpha = NULL;

        if (bi->ats_alpha_segment_flag[i] == 1) {
          // Alpha matte: must be paired with the following texture segment. A
          // last segment flagged as an alpha segment is malformed (the syntax
          // never codes an alpha flag for the last segment) -> hard error.
          if (i + 1 >= num_segments) return -1;

          // Assign the alpha channel
          alpha = seg_src[i];

          // Advance to the texture channel
          ++i;

          // No alpha frame for this stream: leave canvas as is.
          if (alpha == NULL) continue;
        }

        const YV12_BUFFER_CONFIG *tex = seg_src[i];
        if (tex == NULL)
          continue;  // No frame for this stream: leave canvas as is.

        int rc = 0;
        rc = alpha != NULL
                 ? compose_segment_with_alpha(bi, i, tex, i - 1, alpha, canvas)
                 : compose_segment(bi, i, tex, canvas);
        if (rc != 0) return -1;
      }
      break;

    default: return -1;
  }

  return 0;
}
