/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.
 * If the Alliance for Open Media Patent License 1.0 was not distributed with
 * this source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <arm_neon.h>
#include <assert.h>

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "avm_dsp/arm/transpose_neon.h"
#include "avm_dsp/avm_dsp_common.h"
#include "av2/common/reconintra.h"
#include "avm_ports/mem.h"

// ---------- Zone 1 IDIF: top-right (4-tap) ----------

// Mask loads read kZ1PastMask[Z1_PAST_K - 1 - max_base_x + b (+ c) + lane],
// which stays in [0, 61].
#define Z1_PAST_K 31
static const uint16_t kZ1PastMask[2 * Z1_PAST_K] = {
  0,      0,      0,      0,      0,      0,      0,      0,      0,
  0,      0,      0,      0,      0,      0,      0,      0,      0,
  0,      0,      0,      0,      0,      0,      0,      0,      0,
  0,      0,      0,      0,      0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
  0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
};

// Z1 lanes past max_base_x read beyond the C reference but take the fill;
// reads stay within above[142]; callers provide 160 samples past above[0].

static AVM_FORCE_INLINE uint16x8_t z1_filt8_neon(const uint16_t *p, int16x4_t f,
                                                 uint16x8_t hi8) {
  const int16x8_t s0 = vreinterpretq_s16_u16(vld1q_u16(p));
  const int16x8_t s1 = vreinterpretq_s16_u16(vld1q_u16(p + 1));
  const int16x8_t s2 = vreinterpretq_s16_u16(vld1q_u16(p + 2));
  const int16x8_t s3 = vreinterpretq_s16_u16(vld1q_u16(p + 3));
  int32x4_t lo = vmull_lane_s16(vget_low_s16(s0), f, 0);
  lo = vmlal_lane_s16(lo, vget_low_s16(s1), f, 1);
  lo = vmlal_lane_s16(lo, vget_low_s16(s2), f, 2);
  lo = vmlal_lane_s16(lo, vget_low_s16(s3), f, 3);
  int32x4_t hi = vmull_lane_s16(vget_high_s16(s0), f, 0);
  hi = vmlal_lane_s16(hi, vget_high_s16(s1), f, 1);
  hi = vmlal_lane_s16(hi, vget_high_s16(s2), f, 2);
  hi = vmlal_lane_s16(hi, vget_high_s16(s3), f, 3);
  return vminq_u16(vcombine_u16(vqrshrun_n_s32(lo, POWER_DR_INTERP_FILTER),
                                vqrshrun_n_s32(hi, POWER_DR_INTERP_FILTER)),
                   hi8);
}

// bd == 8 in int16: f0*s0 + f3*s3 + f1*s1 lies in [-6120, 32640]; the
// saturating add of f2*s2 only saturates sums that round and clip to 255.
static AVM_FORCE_INLINE uint16x8_t z1_filt8_lbd_neon(const uint16_t *p,
                                                     int16x4_t f) {
  const int16x8_t s0 = vreinterpretq_s16_u16(vld1q_u16(p));
  const int16x8_t s1 = vreinterpretq_s16_u16(vld1q_u16(p + 1));
  const int16x8_t s2 = vreinterpretq_s16_u16(vld1q_u16(p + 2));
  const int16x8_t s3 = vreinterpretq_s16_u16(vld1q_u16(p + 3));
  int16x8_t acc = vmulq_lane_s16(s0, f, 0);
  acc = vmlaq_lane_s16(acc, s3, f, 3);
  acc = vmlaq_lane_s16(acc, s1, f, 1);
  acc = vqaddq_s16(acc, vmulq_lane_s16(s2, f, 2));
  return vmovl_u8(vqrshrun_n_s16(acc, POWER_DR_INTERP_FILTER));
}

static AVM_FORCE_INLINE uint16x4_t z1_filt4_neon(const uint16_t *p, int16x4_t f,
                                                 uint16x4_t hi4) {
  int32x4_t acc = vmull_lane_s16(vreinterpret_s16_u16(vld1_u16(p)), f, 0);
  acc = vmlal_lane_s16(acc, vreinterpret_s16_u16(vld1_u16(p + 1)), f, 1);
  acc = vmlal_lane_s16(acc, vreinterpret_s16_u16(vld1_u16(p + 2)), f, 2);
  acc = vmlal_lane_s16(acc, vreinterpret_s16_u16(vld1_u16(p + 3)), f, 3);
  return vmin_u16(vqrshrun_n_s32(acc, POWER_DR_INTERP_FILTER), hi4);
}

static AVM_FORCE_INLINE uint16x8_t z1_row8_neon(const uint16_t *above, int x,
                                                int max_base_x,
                                                uint16x8_t fill8,
                                                uint16x8_t hi8) {
  const int b = AVMMIN(x >> 6, max_base_x + 1);
  const uint16x8_t res = z1_filt8_neon(
      above + b - 1, vld1_s16(av2_dr_interp_filter[(x & 0x3F) >> 1]), hi8);
  const uint16x8_t past =
      vld1q_u16(kZ1PastMask + Z1_PAST_K - 1 - max_base_x + b);
  return vbslq_u16(past, fill8, res);
}

static AVM_FORCE_INLINE uint16x4_t z1_row4_neon(const uint16_t *above, int x,
                                                int max_base_x,
                                                uint16x4_t fill4,
                                                uint16x4_t hi4) {
  const int b = AVMMIN(x >> 6, max_base_x + 1);
  const uint16x4_t res = z1_filt4_neon(
      above + b - 1, vld1_s16(av2_dr_interp_filter[(x & 0x3F) >> 1]), hi4);
  const uint16x4_t past =
      vld1_u16(kZ1PastMask + Z1_PAST_K - 1 - max_base_x + b);
  return vbsl_u16(past, fill4, res);
}

static AVM_FORCE_INLINE void z1_small_neon(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above, int dx, int x,
                                           int max_base_x, int bd) {
  const uint16x8_t hi8 = vdupq_n_u16((1 << bd) - 1);
  assert(max_base_x <= Z1_PAST_K - 10);
  if (x + (bh - 1) * dx < 64) {
    if (bw == 4) {
      const int16x4_t s0 = vreinterpret_s16_u16(vld1_u16(above - 1));
      const int16x4_t s1 = vreinterpret_s16_u16(vld1_u16(above));
      const int16x4_t s2 = vreinterpret_s16_u16(vld1_u16(above + 1));
      const int16x4_t s3 = vreinterpret_s16_u16(vld1_u16(above + 2));
      for (int r = 0; r < bh; ++r, x += dx) {
        const int16x4_t f = vld1_s16(av2_dr_interp_filter[x >> 1]);
        int32x4_t acc = vmull_lane_s16(s0, f, 0);
        acc = vmlal_lane_s16(acc, s1, f, 1);
        acc = vmlal_lane_s16(acc, s2, f, 2);
        acc = vmlal_lane_s16(acc, s3, f, 3);
        vst1_u16(dst + r * stride,
                 vmin_u16(vqrshrun_n_s32(acc, POWER_DR_INTERP_FILTER),
                          vget_low_u16(hi8)));
      }
    } else {
      const int16x8_t s0 = vreinterpretq_s16_u16(vld1q_u16(above - 1));
      const int16x8_t s1 = vreinterpretq_s16_u16(vld1q_u16(above));
      const int16x8_t s2 = vreinterpretq_s16_u16(vld1q_u16(above + 1));
      const int16x8_t s3 = vreinterpretq_s16_u16(vld1q_u16(above + 2));
      for (int r = 0; r < bh; ++r, x += dx) {
        const int16x4_t f = vld1_s16(av2_dr_interp_filter[x >> 1]);
        int32x4_t lo = vmull_lane_s16(vget_low_s16(s0), f, 0);
        lo = vmlal_lane_s16(lo, vget_low_s16(s1), f, 1);
        lo = vmlal_lane_s16(lo, vget_low_s16(s2), f, 2);
        lo = vmlal_lane_s16(lo, vget_low_s16(s3), f, 3);
        int32x4_t hi = vmull_lane_s16(vget_high_s16(s0), f, 0);
        hi = vmlal_lane_s16(hi, vget_high_s16(s1), f, 1);
        hi = vmlal_lane_s16(hi, vget_high_s16(s2), f, 2);
        hi = vmlal_lane_s16(hi, vget_high_s16(s3), f, 3);
        vst1q_u16(
            dst + r * stride,
            vminq_u16(vcombine_u16(vqrshrun_n_s32(lo, POWER_DR_INTERP_FILTER),
                                   vqrshrun_n_s32(hi, POWER_DR_INTERP_FILTER)),
                      hi8));
      }
    }
    return;
  }
  const uint16x8_t fill8 = vdupq_n_u16(above[max_base_x]);
  if (bw == 4) {
    for (int r = 0; r < bh; ++r, x += dx) {
      vst1_u16(dst + r * stride,
               z1_row4_neon(above, x, max_base_x, vget_low_u16(fill8),
                            vget_low_u16(hi8)));
    }
  } else {
    for (int r = 0; r < bh; ++r, x += dx) {
      vst1q_u16(dst + r * stride,
                z1_row8_neon(above, x, max_base_x, fill8, hi8));
    }
  }
}

// Unmasked rows read only above[-1 .. max_base_x + 2], as the C reference.
static AVM_FORCE_INLINE void z1_unmasked_rows_neon(
    uint16_t **dst, ptrdiff_t stride, int bw, const uint16_t *above, int *x,
    int x_end, int dx, int max_base_x, int bd, uint16x8_t hi8) {
  uint16_t *d = *dst;
  int xx = *x;
  const int xa_end = AVMMIN(x_end, (max_base_x - bw + 2) << 6);
  if (bd == 8) {
    for (; xx < xa_end; d += stride, xx += dx) {
      const uint16_t *p = above + (xx >> 6) - 1;
      const int16x4_t f = vld1_s16(av2_dr_interp_filter[(xx & 0x3F) >> 1]);
      for (int c = 0; c < bw; c += 8) {
        vst1q_u16(d + c, z1_filt8_lbd_neon(p + c, f));
      }
    }
  } else {
    for (; xx < xa_end; d += stride, xx += dx) {
      const uint16_t *p = above + (xx >> 6) - 1;
      const int16x4_t f = vld1_s16(av2_dr_interp_filter[(xx & 0x3F) >> 1]);
      for (int c = 0; c < bw; c += 8) {
        vst1q_u16(d + c, z1_filt8_neon(p + c, f, hi8));
      }
    }
  }
  *dst = d;
  *x = xx;
}

static AVM_FORCE_INLINE void z1_large_neon(uint16_t *dst, ptrdiff_t stride,
                                           int bw, const uint16_t *above, int x,
                                           int x_end, int dx, int max_base_x,
                                           int bd, uint16x8_t hi8,
                                           uint16x8_t fill8) {
  z1_unmasked_rows_neon(&dst, stride, bw, above, &x, x_end, dx, max_base_x, bd,
                        hi8);
  const int xb_end = AVMMIN(x_end, (max_base_x + 1) << 6);
  for (; x < xb_end; dst += stride, x += dx) {
    const int base = x >> 6;
    const int16x4_t f = vld1_s16(av2_dr_interp_filter[(x & 0x3F) >> 1]);
    const uint16_t *pm = kZ1PastMask + Z1_PAST_K - 1 - max_base_x + base;
    for (int c = 0; c < bw; c += 8) {
      const uint16x8_t res =
          z1_filt8_neon(above + AVMMIN(base + c, max_base_x) - 1, f, hi8);
      vst1q_u16(dst + c, vbslq_u16(vld1q_u16(pm + c), fill8, res));
    }
  }
  for (; x < x_end; dst += stride, x += dx) {
    for (int c = 0; c < bw; c += 8) vst1q_u16(dst + c, fill8);
  }
}

// dx == 64: lane c is clip(above[base + c]), base + c <= max_base_x.
static AVM_FORCE_INLINE void z1_dx64_neon(uint16_t *dst, ptrdiff_t stride,
                                          int bw, int bh, const uint16_t *src,
                                          uint16x8_t hi8) {
  int r = 0;
  do {
    for (int i = 0; i < 4; ++i) {
      if (bw == 4) {
        vst1_u16(dst + i * stride,
                 vmin_u16(vld1_u16(src + i), vget_low_u16(hi8)));
      } else {
        for (int c = 0; c < bw; c += 8) {
          vst1q_u16(dst + i * stride + c,
                    vminq_u16(vld1q_u16(src + i + c), hi8));
        }
      }
    }
    dst += 4 * stride;
    src += 4;
    r += 4;
  } while (r < bh);
}

void av2_highbd_dr_prediction_z1_idif_neon(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  (void)left;
  (void)dy;
  assert(dy == 1);
  assert(dx > 0);

  const int max_base_x = (bw + bh) - 1 + (mrl_index << 1);
  int x = dx * (1 + mrl_index);

  if (dx == 64) {
    const uint16x8_t hi8 = vdupq_n_u16((1 << bd) - 1);
    const uint16_t *src = above + 1 + mrl_index;
    switch (bw) {
      case 4: z1_dx64_neon(dst, stride, 4, bh, src, hi8); break;
      case 8: z1_dx64_neon(dst, stride, 8, bh, src, hi8); break;
      case 16: z1_dx64_neon(dst, stride, 16, bh, src, hi8); break;
      default:
        do {
          z1_dx64_neon(dst, stride, 32, bh, src, hi8);
          dst += 32;
          src += 32;
          bw -= 32;
        } while (bw > 0);
        break;
    }
    return;
  }

  // bw and bh are powers of two, so both are <= 8 exactly when bw | bh < 16.
  if ((bw | bh) < 16) {
    if (bw == 4) {
      if (bh == 4) {
        z1_small_neon(dst, stride, 4, 4, above, dx, x, max_base_x, bd);
      } else {
        z1_small_neon(dst, stride, 4, 8, above, dx, x, max_base_x, bd);
      }
    } else {
      if (bh == 4) {
        z1_small_neon(dst, stride, 8, 4, above, dx, x, max_base_x, bd);
      } else {
        z1_small_neon(dst, stride, 8, 8, above, dx, x, max_base_x, bd);
      }
    }
    return;
  }

  const uint16x8_t hi8 = vdupq_n_u16((1 << bd) - 1);
  const uint16x8_t fill8 = vdupq_n_u16(above[max_base_x]);
  const int x_end = x + bh * dx;
  // bw 16 is the most common width (23.5% of calls), so it is tested first.
  if (LIKELY(bw == 16)) {
    z1_large_neon(dst, stride, 16, above, x, x_end, dx, max_base_x, bd, hi8,
                  fill8);
    return;
  }
  if (bw == 4) {
    const int xa_end = AVMMIN(x_end, (max_base_x - 2) << 6);
    for (; x < xa_end; dst += stride, x += dx) {
      const int16x4_t f = vld1_s16(av2_dr_interp_filter[(x & 0x3F) >> 1]);
      vst1_u16(dst, z1_filt4_neon(above + (x >> 6) - 1, f, vget_low_u16(hi8)));
    }
    const int xb_end = AVMMIN(x_end, (max_base_x + 1) << 6);
    for (; x < xb_end; dst += stride, x += dx) {
      vst1_u16(dst, z1_row4_neon(above, x, max_base_x, vget_low_u16(fill8),
                                 vget_low_u16(hi8)));
    }
    for (; x < x_end; dst += stride, x += dx) {
      vst1_u16(dst, vget_low_u16(fill8));
    }
    return;
  }
  switch (bw) {
    case 8:
      z1_large_neon(dst, stride, 8, above, x, x_end, dx, max_base_x, bd, hi8,
                    fill8);
      break;
    default: {
      // bw 64 is two 32-wide halves: above + 32, max_base_x - 32, same fill.
      int mbx = max_base_x;
      do {
        z1_large_neon(dst, stride, 32, above, x, x_end, dx, mbx, bd, hi8,
                      fill8);
        dst += 32;
        above += 32;
        mbx -= 32;
        bw -= 32;
      } while (bw > 0);
    }
  }
}

// ---------- Zone 3 IDIF: bottom-left (4-tap) ----------

#if defined(_MSC_VER)
#define Z3_NOINLINE __declspec(noinline)
#define Z3_ALIGN16_FN
#else
#define Z3_NOINLINE __attribute__((noinline))
#define Z3_ALIGN16_FN __attribute__((aligned(16)))
#endif

static const int16_t kZ3LaneIdx[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

// Z3 lanes past max_base_y read beyond the C reference but take the fill;
// reads stay within left[142]; callers provide 160 samples past left[0].

static AVM_FORCE_INLINE uint16x8_t
z3_col8_bf_neon(const uint16_t *left, int base, int16x4_t f, int max_base_y,
                int16x8_t idx, uint16x8_t fill8, uint16x8_t hi8) {
  const uint16_t *p = left + AVMMIN(base, max_base_y) - 1;
  const int16x8_t s0 = vreinterpretq_s16_u16(vld1q_u16(p));
  const int16x8_t s1 = vreinterpretq_s16_u16(vld1q_u16(p + 1));
  const int16x8_t s2 = vreinterpretq_s16_u16(vld1q_u16(p + 2));
  const int16x8_t s3 = vreinterpretq_s16_u16(vld1q_u16(p + 3));
  int32x4_t lo = vmull_lane_s16(vget_low_s16(s0), f, 0);
  lo = vmlal_lane_s16(lo, vget_low_s16(s1), f, 1);
  lo = vmlal_lane_s16(lo, vget_low_s16(s2), f, 2);
  lo = vmlal_lane_s16(lo, vget_low_s16(s3), f, 3);
  int32x4_t hi = vmull_lane_s16(vget_high_s16(s0), f, 0);
  hi = vmlal_lane_s16(hi, vget_high_s16(s1), f, 1);
  hi = vmlal_lane_s16(hi, vget_high_s16(s2), f, 2);
  hi = vmlal_lane_s16(hi, vget_high_s16(s3), f, 3);
  const uint16x8_t res =
      vminq_u16(vcombine_u16(vqrshrun_n_s32(lo, POWER_DR_INTERP_FILTER),
                             vqrshrun_n_s32(hi, POWER_DR_INTERP_FILTER)),
                hi8);
  const uint16x8_t past =
      vcgtq_s16(idx, vdupq_n_s16((int16_t)(max_base_y - base)));
  return vbslq_u16(past, fill8, res);
}

// bd == 8 in int16: same no-wrap bounds as z1_filt8_lbd_neon.
static AVM_FORCE_INLINE int16x8_t z3_acc8_lbd_neon(const uint16_t *p,
                                                   int16x4_t f) {
  const int16x8_t s0 = vreinterpretq_s16_u16(vld1q_u16(p));
  const int16x8_t s1 = vreinterpretq_s16_u16(vld1q_u16(p + 1));
  const int16x8_t s2 = vreinterpretq_s16_u16(vld1q_u16(p + 2));
  const int16x8_t s3 = vreinterpretq_s16_u16(vld1q_u16(p + 3));
  int16x8_t acc = vmulq_lane_s16(s0, f, 0);
  acc = vmlaq_lane_s16(acc, s3, f, 3);
  acc = vmlaq_lane_s16(acc, s1, f, 1);
  return vqaddq_s16(acc, vmulq_lane_s16(s2, f, 2));
}

// Lanes past max_base_y may wrap; fill128 = left[max_base_y] << 7 rounds
// back to left[max_base_y] exactly.
static AVM_FORCE_INLINE int16x8_t z3_col8_lbd_neon(const uint16_t *left,
                                                   int base, int16x4_t f,
                                                   int max_base_y,
                                                   int16x8_t idx,
                                                   int16x8_t fill128) {
  const int16x8_t acc =
      z3_acc8_lbd_neon(left + AVMMIN(base, max_base_y) - 1, f);
  const uint16x8_t past =
      vcgtq_s16(idx, vdupq_n_s16((int16_t)(max_base_y - base)));
  return vbslq_s16(past, fill128, acc);
}

static AVM_FORCE_INLINE uint16x8_t z3_col8_neon(const uint16_t *left, int pos,
                                                int max_base_y, int16x8_t idx,
                                                uint16x8_t fill8,
                                                uint16x8_t hi8) {
  return z3_col8_bf_neon(left, pos >> 6,
                         vld1_s16(av2_dr_interp_filter[(pos & 0x3F) >> 1]),
                         max_base_y, idx, fill8, hi8);
}

static AVM_FORCE_INLINE uint16x4_t z3_filt4_neon(const uint16_t *left, int pos,
                                                 int max_base_y) {
  const int16x4_t f = vld1_s16(av2_dr_interp_filter[(pos & 0x3F) >> 1]);
  const uint16_t *p = left + AVMMIN(pos >> 6, max_base_y) - 1;
  int32x4_t acc = vmull_lane_s16(vreinterpret_s16_u16(vld1_u16(p)), f, 0);
  acc = vmlal_lane_s16(acc, vreinterpret_s16_u16(vld1_u16(p + 1)), f, 1);
  acc = vmlal_lane_s16(acc, vreinterpret_s16_u16(vld1_u16(p + 2)), f, 2);
  acc = vmlal_lane_s16(acc, vreinterpret_s16_u16(vld1_u16(p + 3)), f, 3);
  return vqrshrun_n_s32(acc, POWER_DR_INTERP_FILTER);
}

static AVM_FORCE_INLINE void z3_filt4x4_neon(uint16x4_t a[4],
                                             const uint16_t *left, int pos,
                                             int dy, int max_base_y) {
  a[0] = z3_filt4_neon(left, pos, max_base_y);
  a[1] = z3_filt4_neon(left, pos + dy, max_base_y);
  a[2] = z3_filt4_neon(left, pos + 2 * dy, max_base_y);
  a[3] = z3_filt4_neon(left, pos + 3 * dy, max_base_y);
}

static AVM_FORCE_INLINE int16x4_t z3_lim4_neon(int pos, int dy,
                                               int max_base_y) {
  int16x4_t lim = vdup_n_s16((int16_t)(max_base_y - (pos >> 6)));
  lim = vset_lane_s16((int16_t)(max_base_y - ((pos + dy) >> 6)), lim, 1);
  lim = vset_lane_s16((int16_t)(max_base_y - ((pos + 2 * dy) >> 6)), lim, 2);
  lim = vset_lane_s16((int16_t)(max_base_y - ((pos + 3 * dy) >> 6)), lim, 3);
  return lim;
}

// Positions reach 67 * 4096, so they stay in 32-bit lanes.
static AVM_FORCE_INLINE int16x8_t z3_lim8_neon(int32x4_t pos_lo,
                                               int32x4_t pos_hi,
                                               int16x8_t max8) {
  return vsubq_s16(
      max8, vcombine_s16(vshrn_n_s32(pos_lo, 6), vshrn_n_s32(pos_hi, 6)));
}

static AVM_FORCE_INLINE void z3_rows8x4_neon(uint16_t *dst, ptrdiff_t stride,
                                             const uint16x4_t a[4],
                                             const uint16x4_t b[4],
                                             int16x8_t lim, uint16x8_t fill8,
                                             uint16x8_t hi8) {
  uint16x8_t t[4];
  for (int j = 0; j < 4; ++j) t[j] = vminq_u16(vcombine_u16(a[j], b[j]), hi8);
  transpose_array_inplace_u16_4x8(t);
  for (int i = 0; i < 4; ++i) {
    const uint16x8_t past = vcgtq_s16(vdupq_n_s16((int16_t)i), lim);
    vst1q_u16(dst + i * stride, vbslq_u16(past, fill8, t[i]));
  }
}

static INLINE void z3_small_neon(uint16_t *dst, ptrdiff_t stride, int bw,
                                 int bh, const uint16_t *left, int dy,
                                 int max_base_y, int pos, uint16x8_t hi8) {
  const uint16x8_t fill8 = vdupq_n_u16(left[max_base_y]);
  const int16x8_t idx = vld1q_s16(kZ3LaneIdx);
  if (bh == 4) {
    uint16x4_t a[4];
    z3_filt4x4_neon(a, left, pos, dy, max_base_y);
    if (bw == 4) {
      const uint16x4_t fill4 = vget_low_u16(fill8);
      const uint16x4_t hi4 = vget_low_u16(hi8);
      const int16x4_t lim = z3_lim4_neon(pos, dy, max_base_y);
      transpose_array_inplace_u16_4x4(a);
      for (int i = 0; i < 4; ++i) {
        const uint16x4_t past = vcgt_s16(vdup_n_s16((int16_t)i), lim);
        vst1_u16(dst + i * stride, vbsl_u16(past, fill4, vmin_u16(a[i], hi4)));
      }
      return;
    }
    uint16x4_t b[4];
    z3_filt4x4_neon(b, left, pos + 4 * dy, dy, max_base_y);
    const int32x4_t pos_lo = vmlaq_s32(
        vdupq_n_s32(pos), vmovl_s16(vget_low_s16(idx)), vdupq_n_s32(dy));
    const int16x8_t lim =
        z3_lim8_neon(pos_lo, vaddq_s32(pos_lo, vdupq_n_s32(4 * dy)),
                     vdupq_n_s16((int16_t)max_base_y));
    z3_rows8x4_neon(dst, stride, a, b, lim, fill8, hi8);
    return;
  }
  uint16x8_t t[8];
  t[0] = z3_col8_neon(left, pos, max_base_y, idx, fill8, hi8);
  pos += dy;
  t[1] = z3_col8_neon(left, pos, max_base_y, idx, fill8, hi8);
  pos += dy;
  t[2] = z3_col8_neon(left, pos, max_base_y, idx, fill8, hi8);
  pos += dy;
  t[3] = z3_col8_neon(left, pos, max_base_y, idx, fill8, hi8);
  if (bw == 4) {
    transpose_array_inplace_u16_4x8(t);
    vst1_u16(dst + 0 * stride, vget_low_u16(t[0]));
    vst1_u16(dst + 1 * stride, vget_low_u16(t[1]));
    vst1_u16(dst + 2 * stride, vget_low_u16(t[2]));
    vst1_u16(dst + 3 * stride, vget_low_u16(t[3]));
    vst1_u16(dst + 4 * stride, vget_high_u16(t[0]));
    vst1_u16(dst + 5 * stride, vget_high_u16(t[1]));
    vst1_u16(dst + 6 * stride, vget_high_u16(t[2]));
    vst1_u16(dst + 7 * stride, vget_high_u16(t[3]));
    return;
  }
  pos += dy;
  t[4] = z3_col8_neon(left, pos, max_base_y, idx, fill8, hi8);
  pos += dy;
  t[5] = z3_col8_neon(left, pos, max_base_y, idx, fill8, hi8);
  pos += dy;
  t[6] = z3_col8_neon(left, pos, max_base_y, idx, fill8, hi8);
  pos += dy;
  t[7] = z3_col8_neon(left, pos, max_base_y, idx, fill8, hi8);
  transpose_elems_inplace_u16_8x8(&t[0], &t[1], &t[2], &t[3], &t[4], &t[5],
                                  &t[6], &t[7]);
  vst1q_u16(dst + 0 * stride, t[0]);
  vst1q_u16(dst + 1 * stride, t[1]);
  vst1q_u16(dst + 2 * stride, t[2]);
  vst1q_u16(dst + 3 * stride, t[3]);
  vst1q_u16(dst + 4 * stride, t[4]);
  vst1q_u16(dst + 5 * stride, t[5]);
  vst1q_u16(dst + 6 * stride, t[6]);
  vst1q_u16(dst + 7 * stride, t[7]);
}

static AVM_FORCE_INLINE void z3_store_tile8_neon(uint16_t *d, ptrdiff_t stride,
                                                 uint16x8_t t0, uint16x8_t t1,
                                                 uint16x8_t t2, uint16x8_t t3,
                                                 uint16x8_t t4, uint16x8_t t5,
                                                 uint16x8_t t6, uint16x8_t t7) {
  transpose_elems_inplace_u16_8x8(&t0, &t1, &t2, &t3, &t4, &t5, &t6, &t7);
  vst1q_u16(d + 0 * stride, t0);
  vst1q_u16(d + 1 * stride, t1);
  vst1q_u16(d + 2 * stride, t2);
  vst1q_u16(d + 3 * stride, t3);
  vst1q_u16(d + 4 * stride, t4);
  vst1q_u16(d + 5 * stride, t5);
  vst1q_u16(d + 6 * stride, t6);
  vst1q_u16(d + 7 * stride, t7);
}

#if AVM_ARCH_AARCH64
DECLARE_ALIGNED(16, static const uint8_t, kZ3TileTbl[32]) = {
  0, 8,  16, 24, 1, 9,  17, 25, 2, 10, 18, 26, 3, 11, 19, 27,
  4, 12, 20, 28, 5, 13, 21, 29, 6, 14, 22, 30, 7, 15, 23, 31,
};
#endif  // AVM_ARCH_AARCH64

// Narrowing before the transpose gives the same bytes as after it.
static AVM_FORCE_INLINE void z3_store_tile8_lbd_neon(
    uint16_t *d, ptrdiff_t stride, int16x8_t a0, int16x8_t a1, int16x8_t a2,
    int16x8_t a3, int16x8_t a4, int16x8_t a5, int16x8_t a6, int16x8_t a7) {
#if AVM_ARCH_AARCH64
  // AArch64 only (vqtbl2q): 12 lookups replace 12 permutes and 8 widens; the
  // out-of-range 0x80 high-byte index reads 0, so the lookup also widens.
  const uint8x16x2_t k_rows = vld1q_u8_x2(kZ3TileTbl);
  const uint8x16_t k_rows0 = k_rows.val[0];
  const uint8x16_t k_rows4 = k_rows.val[1];
  const uint8x16_t k_row0 = vcombine_u8(vcreate_u8(0x8003800280018000ULL),
                                        vcreate_u8(0x8013801280118010ULL));
  const uint8x16_t k_row1 = vaddq_u8(k_row0, vdupq_n_u8(4));
  const uint8x16_t k_row2 = vaddq_u8(k_row0, vdupq_n_u8(8));
  const uint8x16_t k_row3 = vaddq_u8(k_row0, vdupq_n_u8(12));
  uint8x16x2_t q;
  q.val[0] = vcombine_u8(vqrshrun_n_s16(a0, POWER_DR_INTERP_FILTER),
                         vqrshrun_n_s16(a1, POWER_DR_INTERP_FILTER));
  q.val[1] = vcombine_u8(vqrshrun_n_s16(a2, POWER_DR_INTERP_FILTER),
                         vqrshrun_n_s16(a3, POWER_DR_INTERP_FILTER));
  uint8x16x2_t lo, hi;
  lo.val[0] = vqtbl2q_u8(q, k_rows0);
  hi.val[0] = vqtbl2q_u8(q, k_rows4);
  q.val[0] = vcombine_u8(vqrshrun_n_s16(a4, POWER_DR_INTERP_FILTER),
                         vqrshrun_n_s16(a5, POWER_DR_INTERP_FILTER));
  q.val[1] = vcombine_u8(vqrshrun_n_s16(a6, POWER_DR_INTERP_FILTER),
                         vqrshrun_n_s16(a7, POWER_DR_INTERP_FILTER));
  lo.val[1] = vqtbl2q_u8(q, k_rows0);
  hi.val[1] = vqtbl2q_u8(q, k_rows4);
  vst1q_u16(d + 0 * stride, vreinterpretq_u16_u8(vqtbl2q_u8(lo, k_row0)));
  vst1q_u16(d + 1 * stride, vreinterpretq_u16_u8(vqtbl2q_u8(lo, k_row1)));
  vst1q_u16(d + 2 * stride, vreinterpretq_u16_u8(vqtbl2q_u8(lo, k_row2)));
  vst1q_u16(d + 3 * stride, vreinterpretq_u16_u8(vqtbl2q_u8(lo, k_row3)));
  vst1q_u16(d + 4 * stride, vreinterpretq_u16_u8(vqtbl2q_u8(hi, k_row0)));
  vst1q_u16(d + 5 * stride, vreinterpretq_u16_u8(vqtbl2q_u8(hi, k_row1)));
  vst1q_u16(d + 6 * stride, vreinterpretq_u16_u8(vqtbl2q_u8(hi, k_row2)));
  vst1q_u16(d + 7 * stride, vreinterpretq_u16_u8(vqtbl2q_u8(hi, k_row3)));
#else
  // ARMv7 has no vqtbl2q: narrow, transpose the bytes and widen.
  uint8x8_t r0 = vqrshrun_n_s16(a0, POWER_DR_INTERP_FILTER);
  uint8x8_t r1 = vqrshrun_n_s16(a1, POWER_DR_INTERP_FILTER);
  uint8x8_t r2 = vqrshrun_n_s16(a2, POWER_DR_INTERP_FILTER);
  uint8x8_t r3 = vqrshrun_n_s16(a3, POWER_DR_INTERP_FILTER);
  uint8x8_t r4 = vqrshrun_n_s16(a4, POWER_DR_INTERP_FILTER);
  uint8x8_t r5 = vqrshrun_n_s16(a5, POWER_DR_INTERP_FILTER);
  uint8x8_t r6 = vqrshrun_n_s16(a6, POWER_DR_INTERP_FILTER);
  uint8x8_t r7 = vqrshrun_n_s16(a7, POWER_DR_INTERP_FILTER);
  transpose_elems_inplace_u8_8x8(&r0, &r1, &r2, &r3, &r4, &r5, &r6, &r7);
  vst1q_u16(d + 0 * stride, vmovl_u8(r0));
  vst1q_u16(d + 1 * stride, vmovl_u8(r1));
  vst1q_u16(d + 2 * stride, vmovl_u8(r2));
  vst1q_u16(d + 3 * stride, vmovl_u8(r3));
  vst1q_u16(d + 4 * stride, vmovl_u8(r4));
  vst1q_u16(d + 5 * stride, vmovl_u8(r5));
  vst1q_u16(d + 6 * stride, vmovl_u8(r6));
  vst1q_u16(d + 7 * stride, vmovl_u8(r7));
#endif  // AVM_ARCH_AARCH64
}

// Bands with b[7] + r + 7 <= max_base_y read at most left[max_base_y + 2]
// (as the C reference), so they skip the clamp and mask.
static AVM_FORCE_INLINE void z3_tiles8_body_neon(
    uint16_t *dst, ptrdiff_t stride, int bw, int bh, const uint16_t *left,
    int dy, int max_base_y, int pos, uint16x8_t hi8, int lbd) {
  const uint16x8_t fill8 = vdupq_n_u16(left[max_base_y]);
  const int16x8_t fill128 = vdupq_n_s16((int16_t)(left[max_base_y] << 7));
  const int16x8_t idx = vld1q_s16(kZ3LaneIdx);
  for (int c = 0; c < bw; c += 8, dst += 8) {
    int b[8];
    int16x4_t f[8];
    for (int j = 0; j < 8; ++j, pos += dy) {
      b[j] = pos >> 6;
      f[j] = vld1_s16(av2_dr_interp_filter[(pos & 0x3F) >> 1]);
    }
    uint16_t *d = dst;
    const int fast_lim = max_base_y - 7 - b[7];
    int r = 0;
    for (; r < bh && r <= fast_lim; r += 8, d += 8 * stride) {
      const uint16_t *p = left + r - 1;
      if (lbd) {
        z3_store_tile8_lbd_neon(
            d, stride, z3_acc8_lbd_neon(p + b[0], f[0]),
            z3_acc8_lbd_neon(p + b[1], f[1]), z3_acc8_lbd_neon(p + b[2], f[2]),
            z3_acc8_lbd_neon(p + b[3], f[3]), z3_acc8_lbd_neon(p + b[4], f[4]),
            z3_acc8_lbd_neon(p + b[5], f[5]), z3_acc8_lbd_neon(p + b[6], f[6]),
            z3_acc8_lbd_neon(p + b[7], f[7]));
      } else {
        z3_store_tile8_neon(d, stride, z1_filt8_neon(p + b[0], f[0], hi8),
                            z1_filt8_neon(p + b[1], f[1], hi8),
                            z1_filt8_neon(p + b[2], f[2], hi8),
                            z1_filt8_neon(p + b[3], f[3], hi8),
                            z1_filt8_neon(p + b[4], f[4], hi8),
                            z1_filt8_neon(p + b[5], f[5], hi8),
                            z1_filt8_neon(p + b[6], f[6], hi8),
                            z1_filt8_neon(p + b[7], f[7], hi8));
      }
    }
    for (; r < bh; r += 8, d += 8 * stride) {
      if (lbd) {
        z3_store_tile8_lbd_neon(
            d, stride,
            z3_col8_lbd_neon(left, b[0] + r, f[0], max_base_y, idx, fill128),
            z3_col8_lbd_neon(left, b[1] + r, f[1], max_base_y, idx, fill128),
            z3_col8_lbd_neon(left, b[2] + r, f[2], max_base_y, idx, fill128),
            z3_col8_lbd_neon(left, b[3] + r, f[3], max_base_y, idx, fill128),
            z3_col8_lbd_neon(left, b[4] + r, f[4], max_base_y, idx, fill128),
            z3_col8_lbd_neon(left, b[5] + r, f[5], max_base_y, idx, fill128),
            z3_col8_lbd_neon(left, b[6] + r, f[6], max_base_y, idx, fill128),
            z3_col8_lbd_neon(left, b[7] + r, f[7], max_base_y, idx, fill128));
      } else {
        z3_store_tile8_neon(
            d, stride,
            z3_col8_bf_neon(left, b[0] + r, f[0], max_base_y, idx, fill8, hi8),
            z3_col8_bf_neon(left, b[1] + r, f[1], max_base_y, idx, fill8, hi8),
            z3_col8_bf_neon(left, b[2] + r, f[2], max_base_y, idx, fill8, hi8),
            z3_col8_bf_neon(left, b[3] + r, f[3], max_base_y, idx, fill8, hi8),
            z3_col8_bf_neon(left, b[4] + r, f[4], max_base_y, idx, fill8, hi8),
            z3_col8_bf_neon(left, b[5] + r, f[5], max_base_y, idx, fill8, hi8),
            z3_col8_bf_neon(left, b[6] + r, f[6], max_base_y, idx, fill8, hi8),
            z3_col8_bf_neon(left, b[7] + r, f[7], max_base_y, idx, fill8, hi8));
      }
    }
  }
}

static Z3_NOINLINE void z3_tiles8_neon(uint16_t *dst, ptrdiff_t stride, int bw,
                                       int bh, const uint16_t *left, int dy,
                                       int max_base_y, int pos,
                                       uint16x8_t hi8) {
  z3_tiles8_body_neon(dst, stride, bw, bh, left, dy, max_base_y, pos, hi8, 0);
}

static Z3_NOINLINE void z3_tiles8_lbd_neon(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh, const uint16_t *left,
                                           int dy, int max_base_y, int pos) {
  z3_tiles8_body_neon(dst, stride, bw, bh, left, dy, max_base_y, pos,
                      vdupq_n_u16(255), 1);
}

// 16-byte aligned so that size changes of the code before it do not move it.
static Z3_NOINLINE Z3_ALIGN16_FN void z3_h4_neon(uint16_t *dst,
                                                 ptrdiff_t stride, int bw,
                                                 const uint16_t *left, int dy,
                                                 int max_base_y, int pos,
                                                 uint16x8_t hi8) {
  const uint16x8_t fill8 = vdupq_n_u16(left[max_base_y]);
  const int16x8_t max8 = vdupq_n_s16((int16_t)max_base_y);
  const int16x8_t idx = vld1q_s16(kZ3LaneIdx);
  const int32x4_t dy4 = vdupq_n_s32(dy);
  const int32x4_t dy8 = vdupq_n_s32(8 * dy);
  int32x4_t pos_lo =
      vmlaq_s32(vdupq_n_s32(pos), vmovl_s16(vget_low_s16(idx)), dy4);
  int32x4_t pos_hi =
      vmlaq_s32(vdupq_n_s32(pos), vmovl_s16(vget_high_s16(idx)), dy4);
  for (int c = 0; c < bw; c += 8, dst += 8, pos += 8 * dy) {
    uint16x4_t a[4], b[4];
    z3_filt4x4_neon(a, left, pos, dy, max_base_y);
    z3_filt4x4_neon(b, left, pos + 4 * dy, dy, max_base_y);
    z3_rows8x4_neon(dst, stride, a, b, z3_lim8_neon(pos_lo, pos_hi, max8),
                    fill8, hi8);
    pos_lo = vaddq_s32(pos_lo, dy8);
    pos_hi = vaddq_s32(pos_hi, dy8);
  }
}

static Z3_NOINLINE void z3_w4_neon(uint16_t *dst, ptrdiff_t stride, int bh,
                                   const uint16_t *left, int dy, int max_base_y,
                                   int pos, uint16x8_t hi8) {
  const uint16x8_t fill8 = vdupq_n_u16(left[max_base_y]);
  const int16x8_t idx = vld1q_s16(kZ3LaneIdx);
  int b[4];
  int16x4_t f[4];
  for (int j = 0; j < 4; ++j, pos += dy) {
    b[j] = pos >> 6;
    f[j] = vld1_s16(av2_dr_interp_filter[(pos & 0x3F) >> 1]);
  }
  for (int r = 0; r < bh; r += 8, dst += 8 * stride) {
    uint16x8_t t[4];
    t[0] = z3_col8_bf_neon(left, b[0] + r, f[0], max_base_y, idx, fill8, hi8);
    t[1] = z3_col8_bf_neon(left, b[1] + r, f[1], max_base_y, idx, fill8, hi8);
    t[2] = z3_col8_bf_neon(left, b[2] + r, f[2], max_base_y, idx, fill8, hi8);
    t[3] = z3_col8_bf_neon(left, b[3] + r, f[3], max_base_y, idx, fill8, hi8);
    transpose_array_inplace_u16_4x8(t);
    vst1_u16(dst + 0 * stride, vget_low_u16(t[0]));
    vst1_u16(dst + 1 * stride, vget_low_u16(t[1]));
    vst1_u16(dst + 2 * stride, vget_low_u16(t[2]));
    vst1_u16(dst + 3 * stride, vget_low_u16(t[3]));
    vst1_u16(dst + 4 * stride, vget_high_u16(t[0]));
    vst1_u16(dst + 5 * stride, vget_high_u16(t[1]));
    vst1_u16(dst + 6 * stride, vget_high_u16(t[2]));
    vst1_u16(dst + 7 * stride, vget_high_u16(t[3]));
  }
}

void av2_highbd_dr_prediction_z3_idif_neon(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  (void)above;
  (void)dx;
  assert(dx == 1);
  assert(dy > 0);

  const int max_base_y = (bw + bh) - 1 + (mrl_index << 1);
  const uint16x8_t hi8 = vdupq_n_u16((1 << bd) - 1);
  const int y0 = dy * (1 + mrl_index);

  if (bw <= 8 && bh <= 8) {
    z3_small_neon(dst, stride, bw, bh, left, dy, max_base_y, y0, hi8);
    return;
  }
  if (bw >= 8 && bh >= 8) {
    if (bd == 8) {
      z3_tiles8_lbd_neon(dst, stride, bw, bh, left, dy, max_base_y, y0);
    } else {
      z3_tiles8_neon(dst, stride, bw, bh, left, dy, max_base_y, y0, hi8);
    }
    return;
  }
  if (bh == 4) {
    z3_h4_neon(dst, stride, bw, left, dy, max_base_y, y0, hi8);
  } else {
    z3_w4_neon(dst, stride, bh, left, dy, max_base_y, y0, hi8);
  }
}

// ---------- Zone 2 IDIF: mixed (4-tap) ----------

// Lanes reading below lo = -2 - mrl belong to the other path (don't-care).
// Loads clamp at lo - Z2_UNDER = -9 - mrl >= -12; the edge buffers start
// at least 16 samples before index 0 (reconintra.c: 32).
#define Z2_UNDER 7

// bd == 8 in int16: bounds as z1_filt8_lbd_neon; don't-care lanes may wrap
// and are blended out.
static AVM_FORCE_INLINE uint16x8_t z2_filt8x_neon(const uint16_t *p,
                                                  int16x4_t f, uint16x8_t hi8,
                                                  int lbd) {
  return lbd ? z1_filt8_lbd_neon(p, f) : z1_filt8_neon(p, f, hi8);
}

static AVM_FORCE_INLINE uint16x8_t z2_left_col8_neon(const uint16_t *left,
                                                     int pos, int lo,
                                                     uint16x8_t hi8, int lbd) {
  const int start = AVMMAX(pos >> 6, lo - Z2_UNDER);
  const int16x4_t f = vld1_s16(av2_dr_interp_filter[(pos & 0x3F) >> 1]);
  return z2_filt8x_neon(left + start, f, hi8, lbd);
}

static INLINE uint16x4_t z2_left_col4_neon(const uint16_t *left, int pos,
                                           int lo, uint16x4_t hi4) {
  const int start = AVMMAX(pos >> 6, lo - Z2_UNDER);
  const int16x4_t f = vld1_s16(av2_dr_interp_filter[(pos & 0x3F) >> 1]);
  return z1_filt4_neon(left + start, f, hi4);
}

static INLINE int z2_c_above(int r, int bw, int dx, int mrl_index) {
  const int thresh = (r + 1 + mrl_index) * dx - ((1 + mrl_index) << 6);
  const int c_above = (thresh + 63) >> 6;
  return AVMMAX(0, AVMMIN(c_above, bw));
}

typedef struct {
  int ob;
  int16x4_t f;
} Z2Row;

static INLINE Z2Row z2_row(int r, int mrl_index, int dx) {
  Z2Row row;
  const int x = -(r + 1 + mrl_index) * dx;
  row.ob = x >> 6;
  row.f = vld1_s16(av2_dr_interp_filter[(x & 0x3F) >> 1]);
  return row;
}

// s0 >= -4352 (dx <= 4096, r + 1 + mrl <= 67), so the s16 compare is exact.
static AVM_FORCE_INLINE uint16x8_t z2_mix8_neon(const uint16_t *above, int pos,
                                                int lo, uint16x8_t l,
                                                uint16x8_t hi8, int lbd) {
  const int s0 = pos >> 6;
  const int start = AVMMAX(s0, lo - Z2_UNDER);
  const int16x4_t f = vld1_s16(av2_dr_interp_filter[(pos & 0x3F) >> 1]);
  const uint16x8_t a = z2_filt8x_neon(above + start, f, hi8, lbd);
  const int16x8_t lo_k =
      vsubq_s16(vdupq_n_s16((int16_t)lo), vld1q_s16(kZ3LaneIdx));
  const uint16x8_t m = vcltq_s16(vdupq_n_s16((int16_t)s0), lo_k);
  return vbslq_u16(m, l, a);
}

static INLINE uint16x4_t z2_mix4_neon(const uint16_t *above, int pos, int lo,
                                      uint16x4_t l, uint16x4_t hi4) {
  const int s0 = pos >> 6;
  const int start = AVMMAX(s0, lo - Z2_UNDER);
  const int16x4_t f = vld1_s16(av2_dr_interp_filter[(pos & 0x3F) >> 1]);
  const uint16x4_t a = z1_filt4_neon(above + start, f, hi4);
  const int16x4_t lo_k =
      vsub_s16(vdup_n_s16((int16_t)lo), vld1_s16(kZ3LaneIdx));
  const uint16x4_t m = vclt_s16(vdup_n_s16((int16_t)s0), lo_k);
  return vbsl_u16(m, l, a);
}

// Written out per column or row: loops over k stay rolled and spill t[].

// A product per column: a running position would spill as induction vars.
static AVM_FORCE_INLINE uint16x8_t z2_band_col8_neon(const uint16_t *left,
                                                     int r0, int c, int dy,
                                                     int mrl_index,
                                                     uint16x8_t hi8, int lbd) {
  const int y = -(c + 1 + mrl_index) * dy;
  const int start = AVMMAX(r0 + (y >> 6) - 1, -2 - mrl_index - Z2_UNDER);
  const int16x4_t f = vld1_s16(av2_dr_interp_filter[(y & 0x3F) >> 1]);
  return z2_filt8x_neon(left + start, f, hi8, lbd);
}

static AVM_FORCE_INLINE void z2_left_tile8_neon(const uint16_t *left, int r0,
                                                int j, int dy, int mrl_index,
                                                uint16x8_t hi8, int lbd,
                                                uint16x8_t *t) {
  t[0] = z2_band_col8_neon(left, r0, j + 0, dy, mrl_index, hi8, lbd);
  t[1] = z2_band_col8_neon(left, r0, j + 1, dy, mrl_index, hi8, lbd);
  t[2] = z2_band_col8_neon(left, r0, j + 2, dy, mrl_index, hi8, lbd);
  t[3] = z2_band_col8_neon(left, r0, j + 3, dy, mrl_index, hi8, lbd);
  t[4] = z2_band_col8_neon(left, r0, j + 4, dy, mrl_index, hi8, lbd);
  t[5] = z2_band_col8_neon(left, r0, j + 5, dy, mrl_index, hi8, lbd);
  t[6] = z2_band_col8_neon(left, r0, j + 6, dy, mrl_index, hi8, lbd);
  t[7] = z2_band_col8_neon(left, r0, j + 7, dy, mrl_index, hi8, lbd);
  transpose_elems_inplace_u16_8x8(&t[0], &t[1], &t[2], &t[3], &t[4], &t[5],
                                  &t[6], &t[7]);
}

static INLINE void z2_w4_rows_neon(uint16_t *dst, ptrdiff_t stride,
                                   const uint16_t *above, int pos, int dx4,
                                   int lo, uint16x8_t l, uint16x4_t hi4) {
  vst1_u16(dst, z2_mix4_neon(above, pos, lo, vget_low_u16(l), hi4));
  vst1_u16(dst + 4 * stride,
           z2_mix4_neon(above, pos - dx4, lo, vget_high_u16(l), hi4));
}

static INLINE void z2_w4_band_neon(uint16_t *dst, ptrdiff_t stride,
                                   const uint16_t *above, const uint16_t *left,
                                   int r0, int dx, int dy, int mrl_index,
                                   uint16x8_t hi8) {
  const uint16x4_t hi4 = vget_low_u16(hi8);
  const int lo = -2 - mrl_index;
  int lpos = -(1 + mrl_index) * dy + ((r0 << 6) - 64);
  uint16x8_t t[4];
  t[0] = z2_left_col8_neon(left, lpos, lo, hi8, 0);
  lpos -= dy;
  t[1] = z2_left_col8_neon(left, lpos, lo, hi8, 0);
  lpos -= dy;
  t[2] = z2_left_col8_neon(left, lpos, lo, hi8, 0);
  lpos -= dy;
  t[3] = z2_left_col8_neon(left, lpos, lo, hi8, 0);
  transpose_array_inplace_u16_4x8(t);
  const int dx4 = 4 * dx;
  int apos = -(r0 + 1 + mrl_index) * dx - 64;
  z2_w4_rows_neon(dst + 0 * stride, stride, above, apos, dx4, lo, t[0], hi4);
  apos -= dx;
  z2_w4_rows_neon(dst + 1 * stride, stride, above, apos, dx4, lo, t[1], hi4);
  apos -= dx;
  z2_w4_rows_neon(dst + 2 * stride, stride, above, apos, dx4, lo, t[2], hi4);
  apos -= dx;
  z2_w4_rows_neon(dst + 3 * stride, stride, above, apos, dx4, lo, t[3], hi4);
}

static INLINE void z2_h4_chunk_neon(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int j, int dx, int dy, int mrl_index,
                                    uint16x8_t hi8) {
  const uint16x4_t hi4 = vget_low_u16(hi8);
  const int lo = -2 - mrl_index;
  const int dy4 = 4 * dy;
  int lpos = -(j + 1 + mrl_index) * dy - 64;
  uint16x4_t t0[4];
  uint16x4_t t1[4];
  for (int k = 0; k < 4; ++k, lpos -= dy) {
    t0[k] = z2_left_col4_neon(left, lpos, lo, hi4);
    t1[k] = z2_left_col4_neon(left, lpos - dy4, lo, hi4);
  }
  transpose_array_inplace_u16_4x4(t0);
  transpose_array_inplace_u16_4x4(t1);
  int apos = -(1 + mrl_index) * dx + ((j << 6) - 64);
  for (int k = 0; k < 4; ++k, apos -= dx) {
    const uint16x8_t l = vcombine_u16(t0[k], t1[k]);
    vst1q_u16(dst + k * stride + j, z2_mix8_neon(above, apos, lo, l, hi8, 0));
  }
}

#if defined(_MSC_VER)
#define Z2_NOINLINE __declspec(noinline)
#else
#define Z2_NOINLINE __attribute__((noinline))
#endif

static AVM_FORCE_INLINE void z2_tile8_neon(uint16_t *dst, ptrdiff_t stride,
                                           const uint16_t *above,
                                           const uint16_t *left, int lpos,
                                           int apos, int dx, int dy, int lo,
                                           uint16x8_t hi8, int lbd) {
  uint16x8_t t[8];
  t[0] = z2_left_col8_neon(left, lpos, lo, hi8, lbd);
  lpos -= dy;
  t[1] = z2_left_col8_neon(left, lpos, lo, hi8, lbd);
  lpos -= dy;
  t[2] = z2_left_col8_neon(left, lpos, lo, hi8, lbd);
  lpos -= dy;
  t[3] = z2_left_col8_neon(left, lpos, lo, hi8, lbd);
  lpos -= dy;
  t[4] = z2_left_col8_neon(left, lpos, lo, hi8, lbd);
  lpos -= dy;
  t[5] = z2_left_col8_neon(left, lpos, lo, hi8, lbd);
  lpos -= dy;
  t[6] = z2_left_col8_neon(left, lpos, lo, hi8, lbd);
  lpos -= dy;
  t[7] = z2_left_col8_neon(left, lpos, lo, hi8, lbd);
  transpose_elems_inplace_u16_8x8(&t[0], &t[1], &t[2], &t[3], &t[4], &t[5],
                                  &t[6], &t[7]);
  vst1q_u16(dst + 0 * stride, z2_mix8_neon(above, apos, lo, t[0], hi8, lbd));
  apos -= dx;
  vst1q_u16(dst + 1 * stride, z2_mix8_neon(above, apos, lo, t[1], hi8, lbd));
  apos -= dx;
  vst1q_u16(dst + 2 * stride, z2_mix8_neon(above, apos, lo, t[2], hi8, lbd));
  apos -= dx;
  vst1q_u16(dst + 3 * stride, z2_mix8_neon(above, apos, lo, t[3], hi8, lbd));
  apos -= dx;
  vst1q_u16(dst + 4 * stride, z2_mix8_neon(above, apos, lo, t[4], hi8, lbd));
  apos -= dx;
  vst1q_u16(dst + 5 * stride, z2_mix8_neon(above, apos, lo, t[5], hi8, lbd));
  apos -= dx;
  vst1q_u16(dst + 6 * stride, z2_mix8_neon(above, apos, lo, t[6], hi8, lbd));
  apos -= dx;
  vst1q_u16(dst + 7 * stride, z2_mix8_neon(above, apos, lo, t[7], hi8, lbd));
}

// Out of line: inlined, the band's row positions and phases would spill.
static Z2_NOINLINE void z2_mixed_tile8_neon(uint16_t *dst, ptrdiff_t stride,
                                            const uint16_t *above,
                                            const uint16_t *left, int lpos,
                                            int apos, int dx, int dy, int lo,
                                            uint16x8_t hi8) {
  z2_tile8_neon(dst, stride, above, left, lpos, apos, dx, dy, lo, hi8, 0);
}

static Z2_NOINLINE void z2_mixed_tile8_lbd_neon(uint16_t *dst, ptrdiff_t stride,
                                                const uint16_t *above,
                                                const uint16_t *left, int lpos,
                                                int apos, int dx, int dy,
                                                int lo, uint16x8_t hi8) {
  z2_tile8_neon(dst, stride, above, left, lpos, apos, dx, dy, lo, hi8, 1);
}

static INLINE void z2_small_neon(uint16_t *dst, ptrdiff_t stride, int bw,
                                 int bh, const uint16_t *above,
                                 const uint16_t *left, int dx, int dy,
                                 int mrl_index, uint16x8_t hi8) {
  if (bh == 4) {
    if (bw == 4) {
      const uint16x4_t hi4 = vget_low_u16(hi8);
      const int lo = -2 - mrl_index;
      int lpos = -(1 + mrl_index) * dy - 64;
      uint16x4_t t0[4];
      for (int k = 0; k < 4; ++k, lpos -= dy) {
        t0[k] = z2_left_col4_neon(left, lpos, lo, hi4);
      }
      transpose_array_inplace_u16_4x4(t0);
      int apos = -(1 + mrl_index) * dx - 64;
      for (int k = 0; k < 4; ++k, apos -= dx) {
        vst1_u16(dst + k * stride, z2_mix4_neon(above, apos, lo, t0[k], hi4));
      }
      return;
    }
    z2_h4_chunk_neon(dst, stride, above, left, 0, dx, dy, mrl_index, hi8);
    return;
  }
  if (bw == 4) {
    z2_w4_band_neon(dst, stride, above, left, 0, dx, dy, mrl_index, hi8);
    return;
  }
  z2_tile8_neon(dst, stride, above, left, -(1 + mrl_index) * dy - 64,
                -(1 + mrl_index) * dx - 64, dx, dy, -2 - mrl_index, hi8, 0);
}

static AVM_FORCE_INLINE void z2_above_row_neon(uint16_t *dst,
                                               const uint16_t *above, Z2Row row,
                                               int c0, int bw, uint16x8_t hi8,
                                               int lbd) {
  const uint16_t *p = above + row.ob - 1;
  for (int c = c0; c + 8 <= bw; c += 8) {
    vst1q_u16(dst + c, z2_filt8x_neon(p + c, row.f, hi8, lbd));
  }
}

static AVM_FORCE_INLINE void z2_bands8_neon(uint16_t *dst, ptrdiff_t stride,
                                            int bw, int bh,
                                            const uint16_t *above,
                                            const uint16_t *left, int dx,
                                            int dy, int mrl_index,
                                            uint16x8_t hi8, int lbd) {
  const int lo = -2 - mrl_index;
  for (int r0 = 0; r0 < bh; r0 += 8, dst += 8 * stride) {
    const int j_left = z2_c_above(r0, bw, dx, mrl_index) & ~7;
    const int j_end = (z2_c_above(r0 + 7, bw, dx, mrl_index) + 7) & ~7;

    int j = 0;
    for (; j < j_left; j += 8) {
      uint16x8_t t[8];
      z2_left_tile8_neon(left, r0, j, dy, mrl_index, hi8, lbd, t);
      uint16_t *d = dst + j;
      vst1q_u16(d + 0 * stride, t[0]);
      vst1q_u16(d + 1 * stride, t[1]);
      vst1q_u16(d + 2 * stride, t[2]);
      vst1q_u16(d + 3 * stride, t[3]);
      vst1q_u16(d + 4 * stride, t[4]);
      vst1q_u16(d + 5 * stride, t[5]);
      vst1q_u16(d + 6 * stride, t[6]);
      vst1q_u16(d + 7 * stride, t[7]);
    }
    for (; j < j_end; j += 8) {
      const int lpos = -(j + 1 + mrl_index) * dy + ((r0 << 6) - 64);
      const int apos = -(r0 + 1 + mrl_index) * dx + ((j << 6) - 64);
      if (lbd) {
        z2_mixed_tile8_lbd_neon(dst + j, stride, above, left, lpos, apos, dx,
                                dy, lo, hi8);
      } else {
        z2_mixed_tile8_neon(dst + j, stride, above, left, lpos, apos, dx, dy,
                            lo, hi8);
      }
    }

    if (j_end < bw) {
      for (int k = 0; k < 8; ++k) {
        z2_above_row_neon(dst + k * stride, above,
                          z2_row(r0 + k, mrl_index, dx), j_end, bw, hi8, lbd);
      }
    }
  }
}

// Out of line so the entry function skips the band loops' frame setup.
static Z2_NOINLINE void z2_large_neon(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int dx, int dy,
                                      int mrl_index, uint16x8_t hi8) {
  if (bw == 4) {
    for (int r0 = 0; r0 < bh; r0 += 8, dst += 8 * stride) {
      z2_w4_band_neon(dst, stride, above, left, r0, dx, dy, mrl_index, hi8);
    }
    return;
  }
  if (bh == 4) {
    const int j_end = (z2_c_above(3, bw, dx, mrl_index) + 7) & ~7;
    for (int j = 0; j < j_end; j += 8) {
      z2_h4_chunk_neon(dst, stride, above, left, j, dx, dy, mrl_index, hi8);
    }
    if (j_end < bw) {
      for (int k = 0; k < 4; ++k) {
        z2_above_row_neon(dst + k * stride, above, z2_row(k, mrl_index, dx),
                          j_end, bw, hi8, 0);
      }
    }
    return;
  }

  if (vgetq_lane_u16(hi8, 0) == 255) {
    z2_bands8_neon(dst, stride, bw, bh, above, left, dx, dy, mrl_index, hi8, 1);
  } else {
    z2_bands8_neon(dst, stride, bw, bh, above, left, dx, dy, mrl_index, hi8, 0);
  }
}

void av2_highbd_dr_prediction_z2_idif_neon(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int dx, int dy,
                                           int bd, int mrl_index) {
  assert(dx > 0);
  assert(dy > 0);

  const uint16x8_t hi8 = vdupq_n_u16((1 << bd) - 1);
  if (bw <= 8 && bh <= 8) {
    z2_small_neon(dst, stride, bw, bh, above, left, dx, dy, mrl_index, hi8);
    return;
  }
  z2_large_neon(dst, stride, bw, bh, above, left, dx, dy, mrl_index, hi8);
}
