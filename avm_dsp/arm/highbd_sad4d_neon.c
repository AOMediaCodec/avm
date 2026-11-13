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

#include <arm_neon.h>

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/arm/sum_neon.h"

static INLINE void highbd_sad_x4d_shared_step(uint16x8_t acc[4], uint16x8_t s,
                                              uint16x8_t *fa, uint16x8_t na,
                                              uint16x8_t *fc, uint16x8_t nc,
                                              int pair2) {
  acc[0] = vabaq_u16(acc[0], s, *fa);
  acc[1] = vabaq_u16(acc[1], s, na);
  acc[2] = vabaq_u16(acc[2], s, *fc);
  acc[3] = vabaq_u16(acc[3], s, nc);
  *fa = na;
  if (pair2) *fc = nc;
}

static INLINE void highbd_sad_x4d_shared_rows(
    const uint16_t *src, int src_stride, const uint16_t *const p[4],
    int ref_stride, uint32x4_t sum[4], int w, int h, int d, int pair2, int sw) {
  const int max_rows = (sw == 8) ? 32 : 16;
  const int batch = h < max_rows ? h : max_rows;
  const int nv = sw / 8;
  for (int x = 0; x < w; x += sw) {
    const uint16_t *s = src + x;
    const uint16_t *a = p[0] + x;
    const uint16_t *c = p[2] + x;
    const uint16_t *e = p[3] + x;
    uint16x8_t fa[4][2], fc[4][2];
    for (int j = 0; j < d; j++) {
      for (int v = 0; v < nv; v++) {
        fa[j][v] = vld1q_u16(a + 8 * v);
        if (pair2) fc[j][v] = vld1q_u16(c + 8 * v);
      }
      a += ref_stride;
      if (pair2) c += ref_stride;
    }
    for (int y0 = 0; y0 < h; y0 += batch) {
      uint16x8_t acc0[4] = { vdupq_n_u16(0), vdupq_n_u16(0), vdupq_n_u16(0),
                             vdupq_n_u16(0) };
      uint16x8_t acc1[4] = { vdupq_n_u16(0), vdupq_n_u16(0), vdupq_n_u16(0),
                             vdupq_n_u16(0) };
      for (int y = 0; y < batch; y += d) {
        for (int j = 0; j < d; j++) {
          for (int v = 0; v < nv; v++) {
            const uint16x8_t sv = vld1q_u16(s + 8 * v);
            const uint16x8_t na = vld1q_u16(a + 8 * v);
            uint16x8_t nc;
            if (pair2) {
              nc = vld1q_u16(c + 8 * v);
            } else {
              fc[j][v] = vld1q_u16(c + 8 * v);
              nc = vld1q_u16(e + 8 * v);
            }
            const int odd = (sw == 8) ? (j & 1) : v;
            highbd_sad_x4d_shared_step(odd ? acc1 : acc0, sv, &fa[j][v], na,
                                       &fc[j][v], nc, pair2);
          }
          s += src_stride;
          a += ref_stride;
          c += ref_stride;
          if (!pair2) e += ref_stride;
        }
      }
      for (int k = 0; k < 4; k++) {
        sum[k] = vpadalq_u16(sum[k], acc0[k]);
        sum[k] = vpadalq_u16(sum[k], acc1[k]);
      }
    }
  }
}

#if defined(__GNUC__)
#define HIGHBD_SAD_X4D_NOINLINE __attribute__((noinline))
#else
#define HIGHBD_SAD_X4D_NOINLINE
#endif
#define HIGHBD_SAD_X4D_SHARED_FN(name, d, pair2, i1, i3)                       \
  static HIGHBD_SAD_X4D_NOINLINE void name(                                    \
      const uint16_t *src, int src_stride, const uint16_t *const ref[4],       \
      int ref_stride, uint32_t sad_array[4], int w, int h) {                   \
    const uint16_t *const p[4] = { ref[0], ref[i1], ref[2], ref[i3] };         \
    uint32x4_t sum[4] = { vdupq_n_u32(0), vdupq_n_u32(0), vdupq_n_u32(0),      \
                          vdupq_n_u32(0) };                                    \
    if (w == 8) {                                                              \
      highbd_sad_x4d_shared_rows(src, src_stride, p, ref_stride, sum, w, h, d, \
                                 pair2, 8);                                    \
    } else {                                                                   \
      highbd_sad_x4d_shared_rows(src, src_stride, p, ref_stride, sum, w, h, d, \
                                 pair2, 16);                                   \
    }                                                                          \
    const uint32x4_t out[4] = { sum[0], sum[i1], sum[2], sum[i3] };            \
    vst1q_u32(sad_array, horizontal_add_4d_u32x4(out));                        \
  }

HIGHBD_SAD_X4D_SHARED_FN(highbd_sad_x4d_diamond2, 2, 0, 1, 3)
HIGHBD_SAD_X4D_SHARED_FN(highbd_sad_x4d_diamond4, 4, 0, 1, 3)
HIGHBD_SAD_X4D_SHARED_FN(highbd_sad_x4d_square2, 2, 1, 3, 1)
HIGHBD_SAD_X4D_SHARED_FN(highbd_sad_x4d_square4, 4, 1, 3, 1)

static INLINE int highbd_sad_x4d_shared_kind(const uint16_t *const ref[4],
                                             int ref_stride) {
  // Unsigned: the refs may be in separate allocations, where a signed
  // difference can overflow; equality modulo 2^N is still exact.
  const uintptr_t row = (uintptr_t)(intptr_t)ref_stride * sizeof(uint16_t);
  const uintptr_t d1 = (uintptr_t)ref[1] - (uintptr_t)ref[0];
  const uintptr_t d3 = (uintptr_t)ref[3] - (uintptr_t)ref[0];
  const uintptr_t d21 = (uintptr_t)ref[1] - (uintptr_t)ref[2];
  if (d3 == d21) {
    if (d3 == 2 * row) return 1;
    if (d3 == 4 * row) return 2;
  }
  if (d1 == 2 * row) return 3;
  if (d1 == 4 * row) return 4;
  return 0;
}

static INLINE void highbd_sad_x4d_neon_core(const uint16_t *src, int src_stride,
                                            const uint16_t *const ref[4],
                                            int ref_stride,
                                            uint32_t sad_array[4], int w,
                                            int h) {
  uint32x4_t sum[4] = { vdupq_n_u32(0), vdupq_n_u32(0), vdupq_n_u32(0),
                        vdupq_n_u32(0) };
  const uint16_t *r0 = ref[0];
  const uint16_t *r1 = ref[1];
  const uint16_t *r2 = ref[2];
  const uint16_t *r3 = ref[3];

  if (w == 4) {
    for (int y = 0; y < h; y++) {
      const uint16x4_t s = vld1_u16(src);
      sum[0] = vabal_u16(sum[0], s, vld1_u16(r0));
      sum[1] = vabal_u16(sum[1], s, vld1_u16(r1));
      sum[2] = vabal_u16(sum[2], s, vld1_u16(r2));
      sum[3] = vabal_u16(sum[3], s, vld1_u16(r3));
      src += src_stride;
      r0 += ref_stride;
      r1 += ref_stride;
      r2 += ref_stride;
      r3 += ref_stride;
    }
  } else {
    // Each uint16 lane gets at most 16 adds of 4095 before widening (exact).
    const int max_rows = (w == 8) ? 32 : 256 / w;
    const int batch = h < max_rows ? h : max_rows;
    for (int y = 0; y < h; y += batch) {
      uint16x8_t a0[4] = { vdupq_n_u16(0), vdupq_n_u16(0), vdupq_n_u16(0),
                           vdupq_n_u16(0) };
      uint16x8_t a1[4] = { vdupq_n_u16(0), vdupq_n_u16(0), vdupq_n_u16(0),
                           vdupq_n_u16(0) };
      if (w == 8) {
        for (int i = 0; i < batch; i += 2) {
          const uint16x8_t s0 = vld1q_u16(src);
          const uint16x8_t s1 = vld1q_u16(src + src_stride);
          a0[0] = vabaq_u16(a0[0], s0, vld1q_u16(r0));
          a0[1] = vabaq_u16(a0[1], s0, vld1q_u16(r1));
          a0[2] = vabaq_u16(a0[2], s0, vld1q_u16(r2));
          a0[3] = vabaq_u16(a0[3], s0, vld1q_u16(r3));
          a1[0] = vabaq_u16(a1[0], s1, vld1q_u16(r0 + ref_stride));
          a1[1] = vabaq_u16(a1[1], s1, vld1q_u16(r1 + ref_stride));
          a1[2] = vabaq_u16(a1[2], s1, vld1q_u16(r2 + ref_stride));
          a1[3] = vabaq_u16(a1[3], s1, vld1q_u16(r3 + ref_stride));
          src += 2 * src_stride;
          r0 += 2 * ref_stride;
          r1 += 2 * ref_stride;
          r2 += 2 * ref_stride;
          r3 += 2 * ref_stride;
        }
      } else {
        for (int i = 0; i < batch; i++) {
          for (int x = 0; x < w; x += 16) {
            const uint16x8_t s0 = vld1q_u16(src + x);
            const uint16x8_t s1 = vld1q_u16(src + x + 8);
            a0[0] = vabaq_u16(a0[0], s0, vld1q_u16(r0 + x));
            a0[1] = vabaq_u16(a0[1], s0, vld1q_u16(r1 + x));
            a0[2] = vabaq_u16(a0[2], s0, vld1q_u16(r2 + x));
            a0[3] = vabaq_u16(a0[3], s0, vld1q_u16(r3 + x));
            a1[0] = vabaq_u16(a1[0], s1, vld1q_u16(r0 + x + 8));
            a1[1] = vabaq_u16(a1[1], s1, vld1q_u16(r1 + x + 8));
            a1[2] = vabaq_u16(a1[2], s1, vld1q_u16(r2 + x + 8));
            a1[3] = vabaq_u16(a1[3], s1, vld1q_u16(r3 + x + 8));
          }
          src += src_stride;
          r0 += ref_stride;
          r1 += ref_stride;
          r2 += ref_stride;
          r3 += ref_stride;
        }
      }
      for (int k = 0; k < 4; k++) {
        sum[k] = vpadalq_u16(sum[k], a0[k]);
        sum[k] = vpadalq_u16(sum[k], a1[k]);
      }
    }
  }

  vst1q_u32(sad_array, horizontal_add_4d_u32x4(sum));
}

#define HIGHBD_SAD_X4D_NEON(W, H)                                             \
  void avm_highbd_sad##W##x##H##x4d_neon(                                     \
      const uint16_t *src_ptr, int src_stride,                                \
      const uint16_t *const ref_ptr[], int ref_stride, uint32_t *sad_array) { \
    if (W >= 8 && W <= 128 && H >= 8) {                                       \
      switch (highbd_sad_x4d_shared_kind(ref_ptr, ref_stride)) {              \
        case 1:                                                               \
          highbd_sad_x4d_square2(src_ptr, src_stride, ref_ptr, ref_stride,    \
                                 sad_array, W, H);                            \
          return;                                                             \
        case 2:                                                               \
          highbd_sad_x4d_square4(src_ptr, src_stride, ref_ptr, ref_stride,    \
                                 sad_array, W, H);                            \
          return;                                                             \
        case 3:                                                               \
          highbd_sad_x4d_diamond2(src_ptr, src_stride, ref_ptr, ref_stride,   \
                                  sad_array, W, H);                           \
          return;                                                             \
        case 4:                                                               \
          highbd_sad_x4d_diamond4(src_ptr, src_stride, ref_ptr, ref_stride,   \
                                  sad_array, W, H);                           \
          return;                                                             \
        default: break;                                                       \
      }                                                                       \
    }                                                                         \
    highbd_sad_x4d_neon_core(src_ptr, src_stride, ref_ptr, ref_stride,        \
                             sad_array, W, H);                                \
  }

HIGHBD_SAD_X4D_NEON(256, 256)
HIGHBD_SAD_X4D_NEON(256, 128)
HIGHBD_SAD_X4D_NEON(128, 256)
HIGHBD_SAD_X4D_NEON(128, 128)
HIGHBD_SAD_X4D_NEON(128, 64)
HIGHBD_SAD_X4D_NEON(64, 128)
HIGHBD_SAD_X4D_NEON(64, 64)
HIGHBD_SAD_X4D_NEON(64, 32)
HIGHBD_SAD_X4D_NEON(32, 64)
HIGHBD_SAD_X4D_NEON(32, 32)
HIGHBD_SAD_X4D_NEON(32, 16)
HIGHBD_SAD_X4D_NEON(16, 32)
HIGHBD_SAD_X4D_NEON(16, 16)
HIGHBD_SAD_X4D_NEON(16, 8)
HIGHBD_SAD_X4D_NEON(8, 16)
HIGHBD_SAD_X4D_NEON(8, 8)
HIGHBD_SAD_X4D_NEON(8, 4)
HIGHBD_SAD_X4D_NEON(4, 8)
HIGHBD_SAD_X4D_NEON(4, 4)

HIGHBD_SAD_X4D_NEON(4, 16)
HIGHBD_SAD_X4D_NEON(16, 4)
HIGHBD_SAD_X4D_NEON(8, 32)
HIGHBD_SAD_X4D_NEON(32, 8)
HIGHBD_SAD_X4D_NEON(16, 64)
HIGHBD_SAD_X4D_NEON(64, 16)
HIGHBD_SAD_X4D_NEON(4, 32)
HIGHBD_SAD_X4D_NEON(32, 4)
HIGHBD_SAD_X4D_NEON(8, 64)
HIGHBD_SAD_X4D_NEON(64, 8)
HIGHBD_SAD_X4D_NEON(4, 64)
HIGHBD_SAD_X4D_NEON(64, 4)
