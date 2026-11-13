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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "config/avm_config.h"

#if HAVE_SSE2 && CONFIG_RUNTIME_CPU_DETECT
#include "avm_ports/x86.h"
#endif

#include "common/lanczos_resample.h"

/* Shift down with rounding for use when n >= 0, value >= 0 */
#define ROUND_POWER_OF_TWO(value, n) (((value) + (((1 << (n)) >> 1))) >> (n))

/* Shift down with rounding for signed integers, for use when n >= 0 */
#define ROUND_POWER_OF_TWO_SIGNED(value, n)           \
  (((value) < 0) ? -ROUND_POWER_OF_TWO(-(value), (n)) \
                 : ROUND_POWER_OF_TWO((value), (n)))

#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

double get_centered_x0(int p, int q) { return (double)(q - p) / (2 * p); }

double get_cosited_chroma_x0(int p, int q) { return (double)(q - p) / (4 * p); }

double get_inverse_x0_numeric(int p, int q, double x0) { return -x0 * p / q; }

double get_inverse_x0(int p, int q, double x0, int subsampled) {
  if (x0 == (double)('c'))
    x0 = get_centered_x0(p, q);
  else if (x0 == (double)('d'))
    x0 = subsampled ? get_cosited_chroma_x0(p, q) : get_centered_x0(p, q);
  return get_inverse_x0_numeric(p, q, x0);
}

static inline int doclip(int x, int low, int high) {
  return (x < low ? low : x > high ? high : x);
}

void show_resample_filter(RationalResampleFilter *rf) {
  printf("Resample factor: %d / %d\n", rf->p, rf->q);
  printf("Extension type: %s\n", ext2str(rf->ext_type));
  printf("Start = %d\n", rf->start);
  printf("Steps = ");
  for (int i = 0; i < rf->p; ++i) {
    printf("%d, ", rf->steps[i]);
  }
  printf("\n");
  printf("Phases = ");
  for (int i = 0; i < rf->p; ++i) {
    printf("%f, ", rf->phases[i]);
  }
  printf("\n");
  printf("Filters [length %d, bits %d]:\n", rf->length, rf->filter_bits);
  for (int i = 0; i < rf->p; ++i) {
    printf("  { ");
    for (int j = 0; j < rf->length; ++j) printf("%d, ", rf->filter[i][j]);
    printf("  }\n");
  }
  printf("\n");
}

static double sinc(double x) {
  if (fabs(x) < 1e-12) return 1.0;
  return sin(M_PI * x) / (M_PI * x);
}

static double mod_bessel_first(double x) {
  const double t = 0.25 * x * x;
  double fact = 1.0;
  double tpow = 1.0;
  double v = 1.0;
  double dv;
  int k = 1;
  do {
    fact *= k;
    tpow *= t;
    dv = tpow / (fact * fact);
    v += dv;
    k++;
  } while (fabs(dv) > fabs(v) * 1e-8);
  return v;
}

// This is a window function assumed to be defined between [-1, 1] and
// with the value at y=0 being 1.
static double window(double y, WIN_TYPE win) {
  switch (win) {
    case WIN_LANCZOS: {
      return sinc(y);
    }
    case WIN_LANCZOS_DIL: {
      return sinc(y * 0.95);
    }
    case WIN_GAUSSIAN: {
      const double sigma = 0.66;
      const double sigma2 = sigma * sigma;
      return exp(-y * y / sigma2);
    }
    case WIN_GENGAUSSIAN: {
      const double alpha = 4;
      const double sigma = 0.78;
      return exp(-pow(fabs(y / sigma), alpha));
    }
    case WIN_COSINE: {
      return cos(M_PI * y / 2);
    }
    case WIN_HAMMING: {
      const double a0 = 25.0 / 46.0;
      const double a1 = 1.0 - a0;
      return (a0 + a1 * cos(M_PI * y));
    }
    case WIN_BLACKMAN: {
      const double a0 = 0.42659;
      const double a1 = 0.49656;
      const double a2 = 1.0 - a0 - a1;
      return a0 + a1 * cos(M_PI * y) + a2 * cos(2 * M_PI * y);
    }
    case WIN_KAISER: {
      const double alpha = 1.32;
      const double u = M_PI * alpha;
      const double v = M_PI * alpha * sqrt(1 - y * y);
      return mod_bessel_first(v) / mod_bessel_first(u);
    }
    default: {
      assert(0 && "Unknown window type");
      return 0;
    }
  }
}

static double kernel(double x, int a, WIN_TYPE win_type) {
  const double absx = fabs(x);
  if (absx < (double)a) {
    return sinc(x) * window(x / a, win_type);
  } else {
    return 0.0;
  }
}

static int get_lanczos_downsampler_filter_length(int p, int q, int a) {
  assert(p < q);
  return 2 * ((a * q + p - 1) / p);
}

static int get_lanczos_upsampler_filter_length(int p, int q, int a) {
  (void)p;
  (void)q;
  assert(p >= q);
  return 2 * a;
}

static void integerize_array(double *x, int len, int bits, int16_t *y) {
  int sumy = 0;
  for (int i = 0; i < len; ++i) {
    y[i] = (int16_t)rint(x[i] * (1 << bits));
    sumy += y[i];
  }
  while (sumy > (1 << bits)) {
    double mx = -65536.0;
    int imx = -1;
    for (int i = 0; i < len; ++i) {
      const double v = (double)y[i] - (x[i] * (1 << bits));
      if (v > mx) {
        mx = v;
        imx = i;
      }
    }
    assert(imx >= 0);
    y[imx] -= 1;
    sumy -= 1;
  }
  while (sumy < (1 << bits)) {
    double mx = 65536.0;
    int imx = -1;
    for (int i = 0; i < len; ++i) {
      const double v = (double)y[i] - (x[i] * (1 << bits));
      if (v < mx) {
        mx = v;
        imx = i;
      }
    }
    assert(imx >= 0);
    y[imx] += 1;
    sumy += 1;
  }
  sumy = 0;
  for (int i = 0; i < len; ++i) {
    sumy += y[i];
  }
  assert(sumy == (1 << bits));
}

static void get_lanczos_downsampler(double x, int p, int q, int a, int bits,
                                    WIN_TYPE win_type, int16_t *ifilter) {
  double filter[MAX_FILTER_LEN] = { 0.0 };
  int tapsby2 = get_lanczos_downsampler_filter_length(p, q, a) / 2;
  assert(tapsby2 * 2 <= MAX_FILTER_LEN);
  double filter_sum = 0;
  for (int i = -tapsby2 + 1; i <= tapsby2; ++i) {
    const double tap = kernel((i - x) * p / q, a, win_type);
    filter[i + tapsby2 - 1] = tap;
    filter_sum += tap;
  }
  assert(filter_sum != 0.0);
  for (int i = -tapsby2 + 1; i <= tapsby2; ++i) {
    filter[i + tapsby2 - 1] /= filter_sum;
  }
  integerize_array(filter, 2 * tapsby2, bits, ifilter);
}

static void get_lanczos_upsampler(double x, int p, int q, int a, int bits,
                                  WIN_TYPE win_type, int16_t *ifilter) {
  double filter[MAX_FILTER_LEN] = { 0.0 };
  int tapsby2 = get_lanczos_upsampler_filter_length(p, q, a) / 2;
  assert(tapsby2 * 2 <= MAX_FILTER_LEN);
  double filter_sum = 0;
  for (int i = -tapsby2 + 1; i <= tapsby2; ++i) {
    const double tap = kernel(i - x, a, win_type);
    filter[i + tapsby2 - 1] = tap;
    filter_sum += tap;
  }
  assert(filter_sum != 0.0);
  for (int i = -tapsby2 + 1; i <= tapsby2; ++i) {
    filter[i + tapsby2 - 1] /= filter_sum;
  }
  integerize_array(filter, 2 * tapsby2, bits, ifilter);
}

static int gcd(int p, int q) {
  int p1 = (p < q ? p : q);
  int q1 = (p1 == p ? q : p);
  while (p1) {
    const int t = p1;
    p1 = q1 % p1;
    q1 = t;
  }
  return q1;
}

const char *ext_names[] = { "Repeat", "Symmetric", "Reflect", "Gradient" };
const char *ext2str(EXT_TYPE ext_type) { return ext_names[(int)ext_type]; }

int get_resample_filter(int p, int q, int a, double x0, EXT_TYPE ext_type,
                        WIN_TYPE win_type, int subsampled, int bits,
                        RationalResampleFilter *rf) {
  memset(rf, 0, sizeof(*rf));
  // Sized MAX_RATIONAL_FACTOR + 1 because offset[rf->p] and intpel[rf->p] are
  // written below to compute the final phase step rf->steps[rf->p - 1].
  double offset[MAX_RATIONAL_FACTOR + 1];
  int intpel[MAX_RATIONAL_FACTOR + 1];
  if (p <= 0 || q <= 0) {
    fprintf(stderr, "Resampling numerator or denominator must be positive\n");
    return 0;
  }
  const int g = gcd(p, q);
  assert(g > 0);
  rf->p = p / g;
  rf->q = q / g;
  if (rf->p <= 0 || rf->p > MAX_RATIONAL_FACTOR) {
    fprintf(stderr, "Resampling numerator %d ratio exceeds maximum allowed\n",
            rf->p);
    return 0;
  }
  if (rf->q <= 0 || rf->q > MAX_RATIONAL_FACTOR) {
    fprintf(stderr, "Resampling denominator %d ratio exceeds maximum allowed\n",
            rf->q);
    return 0;
  }
  rf->ext_type = ext_type;
  rf->win_type = win_type;
  if (x0 == (double)('c'))
    x0 = get_centered_x0(rf->p, rf->q);
  else if (x0 == (double)('d'))
    x0 = subsampled ? get_cosited_chroma_x0(rf->p, rf->q)
                    : get_centered_x0(rf->p, rf->q);
  rf->filter_bits = bits;
  for (int i = 0; i < rf->p; ++i) {
    offset[i] = (double)rf->q / (double)rf->p * i + x0;
    intpel[i] = (int)floor(offset[i]);
    rf->phases[i] = offset[i] - intpel[i];
  }
  offset[rf->p] = rf->q + x0;
  intpel[rf->p] = (int)floor(offset[rf->p]);

  rf->start = intpel[0];
  for (int i = 0; i < rf->p; ++i) rf->steps[i] = intpel[i + 1] - intpel[i];
  if (rf->p < rf->q) {  // downsampling
    rf->length = get_lanczos_downsampler_filter_length(rf->p, rf->q, a);
    if (rf->length > MAX_FILTER_LEN) {
      fprintf(stderr, "Filter length %d ratio exceeds maximum allowed\n",
              rf->length);
      return 0;
    }
    for (int i = 0; i < rf->p; ++i) {
      get_lanczos_downsampler(rf->phases[i], rf->p, rf->q, a, rf->filter_bits,
                              rf->win_type, rf->filter[i]);
    }
  } else if (rf->p >= rf->q) {  // upsampling
    rf->length = get_lanczos_upsampler_filter_length(rf->p, rf->q, a);
    if (rf->length > MAX_FILTER_LEN) {
      fprintf(stderr, "Filter length %d ratio exceeds maximum allowed\n",
              rf->length);
      return 0;
    }
    for (int i = 0; i < rf->p; ++i) {
      get_lanczos_upsampler(rf->phases[i], rf->p, rf->q, a, rf->filter_bits,
                            rf->win_type, rf->filter[i]);
    }
  }
  return 1;
}

int is_resampler_noop(RationalResampleFilter *rf) {
  return (rf->p == 1 && rf->q == 1 && rf->phases[0] == 0.0);
}

int get_resample_filter_inv(int p, int q, int a, double x0, EXT_TYPE ext_type,
                            WIN_TYPE win_type, int subsampled, int bits,
                            RationalResampleFilter *rf) {
  double y0 = get_inverse_x0(p, q, x0, subsampled);
  return get_resample_filter(q, p, a, y0, ext_type, win_type, subsampled, bits,
                             rf);
}

static inline int clip_to_profile(int sum, const ClipProfile *clip) {
  if (clip) {
    return clip->issigned ? doclip(sum, -(1 << (clip->bits - 1)),
                                   (1 << (clip->bits - 1)) - 1)
                          : doclip(sum, 0, (1 << clip->bits) - 1);
  }
  return doclip(sum, -(1 << 15), (1 << 15) - 1);
}

#if HAVE_SSE2
static inline bool lanczos_has_sse2(void) {
#if CONFIG_RUNTIME_CPU_DETECT
  return (x86_simd_caps() & HAS_SSE2) != 0;
#else
  return true;
#endif
}
#endif

// Assume x buffer is already extended on both sides with x pointing to the
// leftmost pixel, and the extension values are already filled up.
static void resample_1d_core_c(const int16_t *x, int inlen,
                               RationalResampleFilter *rf, int downshift,
                               ClipProfile *clip, int16_t *y, int outlen) {
  (void)inlen;
  assert(rf->filter_bits <= 14);
  assert(downshift >= 0 && downshift <= 30);
  const int L = rf->length;
  const int tapsby2 = L / 2;
  const int16_t *xext = x + rf->start;
  for (int i = 0, p = 0; i < outlen; ++i) {
    int32_t sum = 0;
    const int16_t *const f = rf->filter[p];
    const int16_t *const src = xext - tapsby2 + 1;
    for (int k = 0; k < L; ++k) {
      sum += (int32_t)f[k] * (int32_t)src[k];
    }
    sum = ROUND_POWER_OF_TWO_SIGNED(sum, downshift);
    y[i] = (int16_t)clip_to_profile(sum, clip);
    xext += rf->steps[p];
    if (++p == rf->p) p = 0;
  }
}

static void resample_1d_core(const int16_t *x, int inlen,
                             RationalResampleFilter *rf, int downshift,
                             ClipProfile *clip, int16_t *y, int outlen) {
#if HAVE_SSE2
  if (lanczos_has_sse2()) {
    av2_resample_1d_core_sse2(x, inlen, rf, downshift, clip, y, outlen);
    return;
  }
#endif
  resample_1d_core_c(x, inlen, rf, downshift, clip, y, outlen);
}

// Computes the exact right-hand input extension needed for outlen samples:
// when integer-rounded output dimensions pick a slightly non-exact reduced
// rational ratio p/q, the final output sample (outlen - 1) can center at an
// integer index pos >= inlen, requiring pos + rf->length / 2 - inlen + 1 right
// border samples (potentially > rf->length / 2) to stay in bounds.
static int get_resample_right_border(int inlen, int outlen,
                                     const RationalResampleFilter *rf) {
  const int tapsby2 = rf->length / 2;
  if (outlen <= 0) return tapsby2;
  const int last = outlen - 1;
  int pos = rf->start + (last / rf->p) * rf->q;
  const int rem = last % rf->p;
  for (int k = 0; k < rem; ++k) pos += rf->steps[k];
  const int needed = pos + tapsby2 - inlen + 1;
  return MAX(tapsby2, needed);
}

static void extend_border_lr(int16_t *x, int inlen, EXT_TYPE ext_type,
                             int left_border, int right_border) {
  switch (ext_type) {
    case EXT_REPEAT:
      for (int i = -left_border; i < 0; ++i) x[i] = x[0];
      for (int i = 0; i < right_border; ++i) x[i + inlen] = x[inlen - 1];
      break;
    case EXT_SYMMETRIC:
      if (inlen >= left_border && inlen >= right_border) {
        for (int i = -left_border; i < 0; ++i) x[i] = x[-i - 1];
        for (int i = 0; i < right_border; ++i) x[i + inlen] = x[inlen - 1 - i];
      } else {
        for (int i = -left_border; i < 0; ++i)
          x[i] = x[(-i - 1 > inlen - 1 ? inlen - 1 : -i - 1)];
        for (int i = 0; i < right_border; ++i)
          x[i + inlen] = x[(inlen - 1 - i < 0 ? 0 : inlen - 1 - i)];
      }
      break;
    case EXT_REFLECT:
      if (inlen > left_border && inlen > right_border) {
        for (int i = -left_border; i < 0; ++i) x[i] = x[-i];
        for (int i = 0; i < right_border; ++i) x[i + inlen] = x[inlen - 2 - i];
      } else {
        for (int i = -left_border; i < 0; ++i)
          x[i] = x[(-i > inlen - 1 ? inlen - 1 : -i)];
        for (int i = 0; i < right_border; ++i)
          x[i + inlen] = x[(inlen - 2 - i < 0 ? 0 : inlen - 2 - i)];
      }
      break;
    case EXT_GRADIENT:
      if (inlen > left_border && inlen > right_border) {
        for (int i = -left_border; i < 0; ++i) {
          const int t = 2 * x[0] - x[-i];
          x[i] = (int16_t)doclip(t, INT16_MIN, INT16_MAX);
        }
        for (int i = 0; i < right_border; ++i) {
          const int t = 2 * x[inlen - 1] - x[inlen - 2 - i];
          x[i + inlen] = (int16_t)doclip(t, INT16_MIN, INT16_MAX);
        }
      } else {
        for (int i = -left_border; i < 0; ++i) {
          const int t = 2 * x[0] - x[(-i > inlen - 1 ? inlen - 1 : -i)];
          x[i] = (int16_t)doclip(t, INT16_MIN, INT16_MAX);
        }
        for (int i = 0; i < right_border; ++i) {
          const int t =
              2 * x[inlen - 1] - x[(inlen - 2 - i < 0 ? 0 : inlen - 2 - i)];
          x[i + inlen] = (int16_t)doclip(t, INT16_MIN, INT16_MAX);
        }
      }
      break;
  }
}

// Assume x buffer is already extended on both sides with x pointing to the
// leftmost pixel, but the extension values are not filled up.
static void resample_1d_xt(int16_t *x, int inlen, RationalResampleFilter *rf,
                           int downshift, ClipProfile *clip, int16_t *y,
                           int outlen) {
  const int right_border = get_resample_right_border(inlen, outlen, rf);
  extend_border_lr(x, inlen, rf->ext_type, rf->length / 2, right_border);
  resample_1d_core(x, inlen, rf, downshift, clip, y, outlen);
}

// Assume a scratch buffer xext of size inlen + rf->length/2 + right_border is
// provided
static void resample_1d_xc(const int16_t *x, int inlen,
                           RationalResampleFilter *rf, int downshift,
                           ClipProfile *clip, int16_t *y, int outlen,
                           int16_t *xext) {
  memcpy(xext, x, sizeof(*x) * inlen);

  resample_1d_xt(xext, inlen, rf, downshift, clip, y, outlen);
}

static void fill_col_to_arr(const int16_t *img, int stride, int len,
                            int16_t *arr) {
  int i;
  const int16_t *iptr = img;
  int16_t *aptr = arr;
  for (i = 0; i < len; ++i, iptr += stride) {
    *aptr++ = *iptr;
  }
}

static void fill_arr_to_col(int16_t *img, int stride, int len,
                            const int16_t *arr) {
  int i;
  int16_t *iptr = img;
  const int16_t *aptr = arr;
  for (i = 0; i < len; ++i, iptr += stride) {
    *iptr = *aptr++;
  }
}

static void resample_1d_xt_c(int16_t *x, int inlen, RationalResampleFilter *rf,
                             int downshift, ClipProfile *clip, int16_t *y,
                             int outlen) {
  const int right_border = get_resample_right_border(inlen, outlen, rf);
  extend_border_lr(x, inlen, rf->ext_type, rf->length / 2, right_border);
  resample_1d_core_c(x, inlen, rf, downshift, clip, y, outlen);
}

static void resample_1d_xc_c(const int16_t *x, int inlen,
                             RationalResampleFilter *rf, int downshift,
                             ClipProfile *clip, int16_t *y, int outlen,
                             int16_t *xext) {
  memcpy(xext, x, sizeof(*x) * inlen);
  resample_1d_xt_c(xext, inlen, rf, downshift, clip, y, outlen);
}

void resample_1d_c(const int16_t *x, int inlen, RationalResampleFilter *rf,
                   int downshift, ClipProfile *clip, int16_t *y, int outlen) {
  const int tapsby2 = rf->length / 2;
  const int right_border = get_resample_right_border(inlen, outlen, rf);
  int16_t *xext_ =
      (int16_t *)calloc(tapsby2 + inlen + right_border, sizeof(*x));
  int16_t *xext = xext_ + tapsby2;

  resample_1d_xc_c(x, inlen, rf, downshift, clip, y, outlen, xext);

  free(xext_);
}

void resample_1d(const int16_t *x, int inlen, RationalResampleFilter *rf,
                 int downshift, ClipProfile *clip, int16_t *y, int outlen) {
  const int tapsby2 = rf->length / 2;
  const int right_border = get_resample_right_border(inlen, outlen, rf);
  int16_t *xext_ =
      (int16_t *)calloc(tapsby2 + inlen + right_border, sizeof(*x));
  int16_t *xext = xext_ + tapsby2;

  resample_1d_xc(x, inlen, rf, downshift, clip, y, outlen, xext);

  free(xext_);
}

#if !HAVE_SSE2 || CONFIG_RUNTIME_CPU_DETECT
static inline int map_border_idx(int idx, int inlen, EXT_TYPE ext_type) {
  assert(inlen > 0);
  if (idx >= 0 && idx < inlen) return idx;
  switch (ext_type) {
    case EXT_REPEAT: return (idx < 0) ? 0 : (inlen - 1);
    case EXT_SYMMETRIC:
      if (idx < 0) {
        return (-idx - 1 > inlen - 1) ? (inlen - 1) : (-idx - 1);
      } else {
        const int i = idx - inlen;
        return (inlen - 1 - i < 0) ? 0 : (inlen - 1 - i);
      }
    case EXT_REFLECT:
      if (idx < 0) {
        return (-idx > inlen - 1) ? (inlen - 1) : (-idx);
      } else {
        const int i = idx - inlen;
        return (inlen - 2 - i < 0) ? 0 : (inlen - 2 - i);
      }
    default: return 0;
  }
}
#endif  // !HAVE_SSE2 || CONFIG_RUNTIME_CPU_DETECT

static void resample_vert_rows_16b(const int16_t *src, int width, int inheight,
                                   int instride, RationalResampleFilter *rfv,
                                   int downshift, ClipProfile *clip,
                                   int16_t *dst, int outheight, int outstride) {
  assert(rfv->filter_bits <= 14);
  assert(downshift >= 0 && downshift <= 30);
#if HAVE_SSE2
  if (lanczos_has_sse2()) {
    av2_resample_vert_rows_16b_sse2(src, width, inheight, instride, rfv,
                                    downshift, clip, dst, outheight, outstride);
    return;
  }
#endif
#if !HAVE_SSE2 || CONFIG_RUNTIME_CPU_DETECT
  const int L = rfv->length;
  const int tapsby2 = L / 2;
  const int16_t *row_ptrs[MAX_FILTER_LEN];
  int r_center = rfv->start;
  int p = 0;
  for (int out_r = 0; out_r < outheight; ++out_r) {
    const int16_t *const f = rfv->filter[p];
    int16_t *const out_row = dst + (size_t)out_r * outstride;
    for (int k = 0; k < L; ++k) {
      const int src_r =
          map_border_idx(r_center - tapsby2 + 1 + k, inheight, rfv->ext_type);
      row_ptrs[k] = src + (size_t)src_r * instride;
    }
    for (int col = 0; col < width; ++col) {
      int32_t sum = 0;
      for (int k = 0; k < L; ++k) {
        sum += (int32_t)f[k] * (int32_t)row_ptrs[k][col];
      }
      sum = ROUND_POWER_OF_TWO_SIGNED(sum, downshift);
      out_row[col] = (int16_t)clip_to_profile(sum, clip);
    }
    r_center += rfv->steps[p];
    if (++p == rfv->p) p = 0;
  }
#endif
}

static void resample_vert_rows_out8b(const int16_t *src, int width,
                                     int inheight, int instride,
                                     RationalResampleFilter *rfv, int downshift,
                                     ClipProfile *clip, uint8_t *dst,
                                     int outheight, int outstride) {
  assert(rfv->filter_bits <= 14);
  assert(downshift >= 0 && downshift <= 30);
#if HAVE_SSE2
  if (lanczos_has_sse2()) {
    av2_resample_vert_rows_out8b_sse2(src, width, inheight, instride, rfv,
                                      downshift, clip, dst, outheight,
                                      outstride);
    return;
  }
#endif
#if !HAVE_SSE2 || CONFIG_RUNTIME_CPU_DETECT
  const int L = rfv->length;
  const int tapsby2 = L / 2;
  const int16_t *row_ptrs[MAX_FILTER_LEN];
  int r_center = rfv->start;
  int p = 0;
  for (int out_r = 0; out_r < outheight; ++out_r) {
    const int16_t *const f = rfv->filter[p];
    uint8_t *const out_row = dst + (size_t)out_r * outstride;
    for (int k = 0; k < L; ++k) {
      const int src_r =
          map_border_idx(r_center - tapsby2 + 1 + k, inheight, rfv->ext_type);
      row_ptrs[k] = src + (size_t)src_r * instride;
    }
    for (int col = 0; col < width; ++col) {
      int32_t sum = 0;
      for (int k = 0; k < L; ++k) {
        sum += (int32_t)f[k] * (int32_t)row_ptrs[k][col];
      }
      sum = ROUND_POWER_OF_TWO_SIGNED(sum, downshift);
      if (clip) {
        out_row[col] = (uint8_t)clip_to_profile(sum, clip);
      } else {
        out_row[col] = (uint8_t)sum;
      }
    }
    r_center += rfv->steps[p];
    if (++p == rfv->p) p = 0;
  }
#endif
}

void av2_resample_2d(const int16_t *x, int inwidth, int inheight, int instride,
                     RationalResampleFilter *rfh, RationalResampleFilter *rfv,
                     int int_extra_bits, ClipProfile *clip, int16_t *y,
                     int outwidth, int outheight, int outstride) {
  if (inwidth <= 0 || inheight <= 0 || outwidth <= 0 || outheight <= 0) return;
  if (rfv == NULL || is_resampler_noop(rfv)) {
    resample_horz(x, inwidth, inheight, instride, rfh, clip, y, outwidth,
                  outstride);
    return;
  }
  if (rfh == NULL || is_resampler_noop(rfh)) {
    resample_vert(x, inwidth, inheight, instride, rfv, clip, y, outheight,
                  outstride);
    return;
  }
  int16_t *tmpbuf =
      (int16_t *)calloc((size_t)outwidth * inheight, sizeof(int16_t));
  const int left_h = rfh->length / 2;
  const int right_h = get_resample_right_border(inwidth, outwidth, rfh);
  const int span_h = left_h + inwidth + right_h;
  int tmpstride = outwidth;
  // intermediate data is stored in 16 bit buffers, so limit int_extra_bits
  const int clip_bits = clip ? clip->bits : 14;
  int_extra_bits = MAX(0, MIN(int_extra_bits, 14 - clip_bits));
  const int downshifth = rfh->filter_bits - int_extra_bits;
  const int downshiftv = rfh->filter_bits + int_extra_bits;
  if (rfv->ext_type != EXT_GRADIENT) {
    int16_t *tmparr_ = (int16_t *)calloc(span_h, sizeof(int16_t));
    int16_t *tmparrh = tmparr_ + left_h;
    for (int i = 0; i < inheight; ++i) {
      resample_1d_xc(x + instride * i, inwidth, rfh, downshifth, NULL,
                     tmpbuf + i * tmpstride, outwidth, tmparrh);
    }
    resample_vert_rows_16b(tmpbuf, outwidth, inheight, tmpstride, rfv,
                           downshiftv, clip, y, outheight, outstride);
    free(tmpbuf);
    free(tmparr_);
    return;
  }
  const int left_v = rfv->length / 2;
  const int right_v = get_resample_right_border(inheight, outheight, rfv);
  const int span_v = left_v + inheight + right_v;
  const int arrsize = outheight + MAX(span_h, span_v);
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrh = tmparr_ + outheight + left_h;
  int16_t *tmparrv = tmparr_ + outheight + left_v;
  int16_t *tmparro = tmparr_;
  for (int i = 0; i < inheight; ++i) {
    resample_1d_xc(x + instride * i, inwidth, rfh, downshifth, NULL,
                   tmpbuf + i * tmpstride, outwidth, tmparrh);
  }
  for (int i = 0; i < outwidth; ++i) {
    fill_col_to_arr(tmpbuf + i, outwidth, inheight, tmparrv);
    resample_1d_xt(tmparrv, inheight, rfv, downshiftv, clip, tmparro,
                   outheight);
    fill_arr_to_col(y + i, outstride, outheight, tmparro);
  }
  free(tmpbuf);
  free(tmparr_);
}

void resample_horz(const int16_t *x, int inwidth, int inheight, int instride,
                   RationalResampleFilter *rfh, ClipProfile *clip, int16_t *y,
                   int outwidth, int outstride) {
  if (rfh == NULL || is_resampler_noop(rfh)) {
    for (int i = 0; i < inheight; ++i) {
      memcpy(y + i * outstride, x + i * instride, inwidth * sizeof(*x));
    }
    return;
  }
  const int left_h = rfh->length / 2;
  const int right_h = get_resample_right_border(inwidth, outwidth, rfh);
  const int arrsize = left_h + inwidth + right_h;
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrh = tmparr_ + left_h;
  for (int i = 0; i < inheight; ++i) {
    resample_1d_xc(x + instride * i, inwidth, rfh, rfh->filter_bits, clip,
                   y + i * outstride, outwidth, tmparrh);
  }
  free(tmparr_);
}

void resample_vert(const int16_t *x, int inwidth, int inheight, int instride,
                   RationalResampleFilter *rfv, ClipProfile *clip, int16_t *y,
                   int outheight, int outstride) {
  if (rfv->ext_type != EXT_GRADIENT) {
    resample_vert_rows_16b(x, inwidth, inheight, instride, rfv,
                           rfv->filter_bits, clip, y, outheight, outstride);
    return;
  }
  const int left_v = rfv->length / 2;
  const int right_v = get_resample_right_border(inheight, outheight, rfv);
  const int arrsize = outheight + left_v + inheight + right_v;
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrv = tmparr_ + outheight + left_v;
  int16_t *tmparro = tmparr_;
  for (int i = 0; i < inwidth; ++i) {
    fill_col_to_arr(x + i, instride, inheight, tmparrv);
    resample_1d_xt(tmparrv, inheight, rfv, rfv->filter_bits, clip, tmparro,
                   outheight);
    fill_arr_to_col(y + i, outstride, outheight, tmparro);
  }
  free(tmparr_);
}

// Assume x buffer is already extended on both sides with x pointing to the
// leftmost pixel, and the extension values are already filled up.
static void resample_1d_core_in8b(const uint8_t *x, int inlen,
                                  RationalResampleFilter *rf, int downshift,
                                  ClipProfile *clip, int16_t *y, int outlen) {
  (void)inlen;
  const int tapsby2 = rf->length / 2;
  const uint8_t *xext = x;
  xext += rf->start;
  for (int i = 0, p = 0; i < outlen; ++i, p = (p + 1) % rf->p) {
    int64_t sum = 0;
    for (int j = -tapsby2 + 1; j <= tapsby2; ++j) {
      sum += (int)rf->filter[p][j + tapsby2 - 1] * (int)xext[j];
    }
    sum = ROUND_POWER_OF_TWO_SIGNED(sum, downshift);
    if (clip) {
      y[i] = (int16_t)(clip->issigned
                           ? doclip((int)sum, -(1 << (clip->bits - 1)),
                                    (1 << (clip->bits - 1)) - 1)
                           : doclip((int)sum, 0, (1 << clip->bits) - 1));
    } else {
      y[i] = (int16_t)doclip((int)sum, -(1 << 15), (1 << 15) - 1);
    }
    xext += rf->steps[p];
  }
}

// Assume x buffer is already extended on both sides with x pointing to the
// leftmost pixel, and the extension values are already filled up.
static void resample_1d_core_8b(const uint8_t *x, int inlen,
                                RationalResampleFilter *rf, int downshift,
                                ClipProfile *clip, uint8_t *y, int outlen) {
  (void)inlen;
  const int tapsby2 = rf->length / 2;
  const uint8_t *xext = x;
  xext += rf->start;
  for (int i = 0, p = 0; i < outlen; ++i, p = (p + 1) % rf->p) {
    int64_t sum = 0;
    for (int j = -tapsby2 + 1; j <= tapsby2; ++j) {
      sum += (int)rf->filter[p][j + tapsby2 - 1] * (int)xext[j];
    }
    sum = ROUND_POWER_OF_TWO_SIGNED(sum, downshift);
    if (clip) {
      y[i] = (uint8_t)(clip->issigned
                           ? doclip((int)sum, -(1 << (clip->bits - 1)),
                                    (1 << (clip->bits - 1)) - 1)
                           : doclip((int)sum, 0, (1 << clip->bits) - 1));
    } else {
      y[i] = (uint8_t)sum;
    }
    xext += rf->steps[p];
  }
}

static void extend_border_lr_8b(uint8_t *x, int inlen, EXT_TYPE ext_type,
                                int left_border, int right_border) {
  switch (ext_type) {
    case EXT_REPEAT:
      for (int i = -left_border; i < 0; ++i) x[i] = x[0];
      for (int i = 0; i < right_border; ++i) x[i + inlen] = x[inlen - 1];
      break;
    case EXT_SYMMETRIC:
      if (inlen >= left_border && inlen >= right_border) {
        for (int i = -left_border; i < 0; ++i) x[i] = x[-i - 1];
        for (int i = 0; i < right_border; ++i) x[i + inlen] = x[inlen - 1 - i];
      } else {
        for (int i = -left_border; i < 0; ++i)
          x[i] = x[(-i - 1 > inlen - 1 ? inlen - 1 : -i - 1)];
        for (int i = 0; i < right_border; ++i)
          x[i + inlen] = x[(inlen - 1 - i < 0 ? 0 : inlen - 1 - i)];
      }
      break;
    case EXT_REFLECT:
      if (inlen > left_border && inlen > right_border) {
        for (int i = -left_border; i < 0; ++i) x[i] = x[-i];
        for (int i = 0; i < right_border; ++i) x[i + inlen] = x[inlen - 2 - i];
      } else {
        for (int i = -left_border; i < 0; ++i)
          x[i] = x[(-i > inlen - 1 ? inlen - 1 : -i)];
        for (int i = 0; i < right_border; ++i)
          x[i + inlen] = x[(inlen - 2 - i < 0 ? 0 : inlen - 2 - i)];
      }
      break;
    case EXT_GRADIENT:
      if (inlen > left_border && inlen > right_border) {
        for (int i = -left_border; i < 0; ++i) {
          const int t = 2 * x[0] - x[-i];
          x[i] = (uint8_t)doclip(t, 0, 255);
        }
        for (int i = 0; i < right_border; ++i) {
          const int t = 2 * x[inlen - 1] - x[inlen - 2 - i];
          x[i + inlen] = (uint8_t)doclip(t, 0, 255);
        }
      } else {
        for (int i = -left_border; i < 0; ++i) {
          const int t = 2 * x[0] - x[(-i > inlen - 1 ? inlen - 1 : -i)];
          x[i] = (uint8_t)doclip(t, 0, 255);
        }
        for (int i = 0; i < right_border; ++i) {
          const int t =
              2 * x[inlen - 1] - x[(inlen - 2 - i < 0 ? 0 : inlen - 2 - i)];
          x[i + inlen] = (uint8_t)doclip(t, 0, 255);
        }
      }
      break;
  }
}

static void resample_1d_xt_8b(uint8_t *x, int inlen, RationalResampleFilter *rf,
                              int downshift, ClipProfile *clip, uint8_t *y,
                              int outlen) {
  const int right_border = get_resample_right_border(inlen, outlen, rf);
  extend_border_lr_8b(x, inlen, rf->ext_type, rf->length / 2, right_border);
  resample_1d_core_8b(x, inlen, rf, downshift, clip, y, outlen);
}

static void resample_1d_xc_8b(const uint8_t *x, int inlen,
                              RationalResampleFilter *rf, int downshift,
                              ClipProfile *clip, uint8_t *y, int outlen,
                              uint8_t *xext) {
  memcpy(xext, x, inlen * sizeof(*x));

  resample_1d_xt_8b(xext, inlen, rf, downshift, clip, y, outlen);
}

static void resample_1d_xt_in8b(uint8_t *x, int inlen,
                                RationalResampleFilter *rf, int downshift,
                                ClipProfile *clip, int16_t *y, int outlen) {
  const int right_border = get_resample_right_border(inlen, outlen, rf);
  extend_border_lr_8b(x, inlen, rf->ext_type, rf->length / 2, right_border);
  resample_1d_core_in8b(x, inlen, rf, downshift, clip, y, outlen);
}

static void resample_1d_xc_in8b(const uint8_t *x, int inlen,
                                RationalResampleFilter *rf, int downshift,
                                ClipProfile *clip, int16_t *y, int outlen,
                                uint8_t *xext) {
  memcpy(xext, x, inlen * sizeof(*x));

  resample_1d_xt_in8b(xext, inlen, rf, downshift, clip, y, outlen);
}

static void fill_col_to_arr_in8b(const uint8_t *img, int stride, int len,
                                 int16_t *arr) {
  int i;
  const uint8_t *iptr = img;
  int16_t *aptr = arr;
  for (i = 0; i < len; ++i, iptr += stride) {
    *aptr++ = (int16_t)(*iptr);
  }
}

static void fill_arr_to_col_out8b(uint8_t *img, int stride, int len,
                                  const int16_t *arr) {
  int i;
  uint8_t *iptr = img;
  const int16_t *aptr = arr;
  for (i = 0; i < len; ++i, iptr += stride) {
    *iptr = (uint8_t)*aptr++;
  }
}

void resample_1d_8b(const uint8_t *x, int inlen, RationalResampleFilter *rf,
                    int downshift, ClipProfile *clip, uint8_t *y, int outlen) {
  const int tapsby2 = rf->length / 2;
  const int right_border = get_resample_right_border(inlen, outlen, rf);
  uint8_t *xext_ =
      (uint8_t *)calloc(tapsby2 + inlen + right_border, sizeof(*x));
  uint8_t *xext = xext_ + tapsby2;

  resample_1d_xc_8b(x, inlen, rf, downshift, clip, y, outlen, xext);

  free(xext_);
}

void av2_resample_2d_8b(const uint8_t *x, int inwidth, int inheight,
                        int instride, RationalResampleFilter *rfh,
                        RationalResampleFilter *rfv, int int_extra_bits,
                        ClipProfile *clip, uint8_t *y, int outwidth,
                        int outheight, int outstride) {
  if (inwidth <= 0 || inheight <= 0 || outwidth <= 0 || outheight <= 0) return;
  if (rfv == NULL || is_resampler_noop(rfv)) {
    resample_horz_8b(x, inwidth, inheight, instride, rfh, clip, y, outwidth,
                     outstride);
    return;
  }
  if (rfh == NULL || is_resampler_noop(rfh)) {
    resample_vert_8b(x, inwidth, inheight, instride, rfv, clip, y, outheight,
                     outstride);
    return;
  }
  int16_t *tmpbuf =
      (int16_t *)calloc((size_t)outwidth * inheight, sizeof(int16_t));
  const int left_h = rfh->length / 2;
  const int right_h = get_resample_right_border(inwidth, outwidth, rfh);
  const int span_h = left_h + inwidth + right_h;
  int tmpstride = outwidth;
  // intermediate data is stored in 16 bit buffers, so limit int_extra_bits
  const int clip_bits = clip ? clip->bits : 14;
  int_extra_bits = MAX(0, MIN(int_extra_bits, 14 - clip_bits));
  const int downshifth = rfh->filter_bits - int_extra_bits;
  const int downshiftv = rfh->filter_bits + int_extra_bits;
  if (rfh->ext_type != EXT_GRADIENT && rfv->ext_type != EXT_GRADIENT) {
    int16_t *tmparr_ = (int16_t *)calloc(span_h, sizeof(int16_t));
    int16_t *tmparrh = tmparr_ + left_h;
    for (int i = 0; i < inheight; ++i) {
      const uint8_t *const x_row = x + (size_t)instride * i;
      for (int c = 0; c < inwidth; ++c) tmparrh[c] = (int16_t)x_row[c];
      resample_1d_xt(tmparrh, inwidth, rfh, downshifth, NULL,
                     tmpbuf + (size_t)i * tmpstride, outwidth);
    }
    resample_vert_rows_out8b(tmpbuf, outwidth, inheight, tmpstride, rfv,
                             downshiftv, clip, y, outheight, outstride);
    free(tmpbuf);
    free(tmparr_);
    return;
  }
  const int left_v = rfv->length / 2;
  const int right_v = get_resample_right_border(inheight, outheight, rfv);
  const int span_v = left_v + inheight + right_v;
  const int arrsize = outheight + MAX(span_h, span_v);
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrh = tmparr_ + outheight + left_h;
  int16_t *tmparrv = tmparr_ + outheight + left_v;
  int16_t *tmparro = tmparr_;
  for (int i = 0; i < inheight; ++i) {
    resample_1d_xc_in8b(x + instride * i, inwidth, rfh, downshifth, NULL,
                        tmpbuf + i * tmpstride, outwidth, (uint8_t *)tmparrh);
  }
  for (int i = 0; i < outwidth; ++i) {
    fill_col_to_arr(tmpbuf + i, outwidth, inheight, tmparrv);
    resample_1d_xt(tmparrv, inheight, rfv, downshiftv, clip, tmparro,
                   outheight);
    fill_arr_to_col_out8b(y + i, outstride, outheight, tmparro);
  }
  free(tmpbuf);
  free(tmparr_);
}

void resample_horz_8b(const uint8_t *x, int inwidth, int inheight, int instride,
                      RationalResampleFilter *rfh, ClipProfile *clip,
                      uint8_t *y, int outwidth, int outstride) {
  if (rfh == NULL || is_resampler_noop(rfh)) {
    for (int i = 0; i < inheight; ++i) {
      memcpy(y + i * outstride, x + i * instride, inwidth * sizeof(*x));
    }
    return;
  }
  const int left_h = rfh->length / 2;
  const int right_h = get_resample_right_border(inwidth, outwidth, rfh);
  const int arrsize = left_h + inwidth + right_h;
  uint8_t *tmparr_ = (uint8_t *)calloc(arrsize, sizeof(*tmparr_));
  uint8_t *tmparrh = tmparr_ + left_h;
  for (int i = 0; i < inheight; ++i) {
    resample_1d_xc_8b(x + instride * i, inwidth, rfh, rfh->filter_bits, clip,
                      y + i * outstride, outwidth, tmparrh);
  }
  free(tmparr_);
}

void resample_vert_8b(const uint8_t *x, int inwidth, int inheight, int instride,
                      RationalResampleFilter *rfv, ClipProfile *clip,
                      uint8_t *y, int outheight, int outstride) {
  const int left_v = rfv->length / 2;
  const int right_v = get_resample_right_border(inheight, outheight, rfv);
  const int arrsize = outheight + left_v + inheight + right_v;
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrv = tmparr_ + outheight + left_v;
  int16_t *tmparro = tmparr_;
  for (int i = 0; i < inwidth; ++i) {
    fill_col_to_arr_in8b(x + i, instride, inheight, tmparrv);
    resample_1d_xt(tmparrv, inheight, rfv, rfv->filter_bits, clip, tmparro,
                   outheight);
    fill_arr_to_col_out8b(y + i, outstride, outheight, tmparro);
  }
  free(tmparr_);
}

int get_resampled_output_length(int inlen, int p, int q, int force_even) {
  if (!force_even) {
    // round
    return (inlen * p + q / 2) / q;
  }
  int outlen_floor = inlen * p / q;
  // choose floor or ceil depending on which one is even
  if ((outlen_floor % 2) == 1)
    return outlen_floor + 1;
  else
    return outlen_floor;
}

void av2_derive_scale_factor(int width_scaled, int width, int *p, int *q) {
  assert(width > 0);
  assert(width_scaled > 0);

  *p = -1;
  *q = -1;

  // Lanczos library supports a scaling factor p/q with both p and q <= 16.
  if ((width > (width_scaled * MAX_RATIONAL_FACTOR)) ||
      (width_scaled > (width * MAX_RATIONAL_FACTOR)))
    return;

  int best_err = abs(width - width_scaled);
  int best_denom = 1;

  for (int denom = 1; denom <= MAX_RATIONAL_FACTOR; ++denom) {
    for (int num = 1; num <= MAX_RATIONAL_FACTOR; ++num) {
      int err = abs(num * width - denom * width_scaled);

      if (err * best_denom < best_err * denom) {
        *p = num;
        *q = denom;
        best_err = err;
        best_denom = denom;
      }
    }
  }

  if (best_err * MAX_RATIONAL_FACTOR * 2 > (best_denom * width)) {
    *p = -1;
    *q = -1;
  }
  return;
}

void av2_resample_plane_2d_lanczos_hv(const uint16_t *const input, int height,
                                      int width, int in_stride,
                                      uint16_t *output, int height2, int width2,
                                      int out_stride, int subx, int suby,
                                      int bd, int denom_hor, int num_hor,
                                      int denom_ver, int num_ver,
                                      int lanczos_a_hor, int lanczos_a_ver) {
  int coeff_prec_bits = 14;
  int extra_prec_bits = 6;
  WIN_TYPE win = WIN_LANCZOS;
  EXT_TYPE ext = EXT_REPEAT;
  ClipProfile clip = { bd, 0 };
  int horz_a = lanczos_a_hor;
  int vert_a = lanczos_a_ver;
  // Horizontally use centered for luma, co-sited for chroma
  double horz_x0 = subx ? (double)('d') : (double)('c');
  // Vertically use centered for luma and chroma
  double vert_x0 = (double)('c');

  RationalResampleFilter horz_rf;
  RationalResampleFilter vert_rf;

  if (!get_resample_filter(num_hor, denom_hor, horz_a, horz_x0, ext, win, subx,
                           coeff_prec_bits, &horz_rf)) {
    fprintf(stderr, "Cannot generate filter, exiting!\n");
    exit(1);
  }
  if (!get_resample_filter(num_ver, denom_ver, vert_a, vert_x0, ext, win, suby,
                           coeff_prec_bits, &vert_rf)) {
    fprintf(stderr, "Cannot generate filter, exiting!\n");
    exit(1);
  }

  av2_resample_2d((const int16_t *)input, width, height, in_stride, &horz_rf,
                  &vert_rf, extra_prec_bits, &clip, (int16_t *)output, width2,
                  height2, out_stride);
}

void av2_resample_plane_2d_lanczos(const uint16_t *const input, int height,
                                   int width, int in_stride, uint16_t *output,
                                   int height2, int width2, int out_stride,
                                   int subx, int suby, int bd, int denom,
                                   int num, int lanczos_a_hor,
                                   int lanczos_a_ver) {
  av2_resample_plane_2d_lanczos_hv(
      input, height, width, in_stride, output, height2, width2, out_stride,
      subx, suby, bd, denom, num, denom, num, lanczos_a_hor, lanczos_a_ver);
}

void av2_resample_plane_2d_8b_lanczos(const uint8_t *const input, int height,
                                      int width, int in_stride, uint8_t *output,
                                      int height2, int width2, int out_stride,
                                      int subx, int suby, int bd, int denom,
                                      int num, int lanczos_a_hor,
                                      int lanczos_a_ver) {
  int coeff_prec_bits = 14;
  int extra_prec_bits = 6;
  WIN_TYPE win = WIN_LANCZOS;
  EXT_TYPE ext = EXT_REPEAT;
  ClipProfile clip = { bd, 0 };
  int horz_a = lanczos_a_hor;
  int vert_a = lanczos_a_ver;
  // Horizontally use centered for luma, co-sited for chroma
  double horz_x0 = subx ? (double)('d') : (double)('c');
  // Vertically use centered for luma and chroma
  double vert_x0 = (double)('c');

  RationalResampleFilter horz_rf;
  RationalResampleFilter vert_rf;

  if (!get_resample_filter(num, denom, horz_a, horz_x0, ext, win, subx,
                           coeff_prec_bits, &horz_rf)) {
    fprintf(stderr, "Cannot generate filter, exiting!\n");
    exit(1);
  }
  if (!get_resample_filter(num, denom, vert_a, vert_x0, ext, win, suby,
                           coeff_prec_bits, &vert_rf)) {
    fprintf(stderr, "Cannot generate filter, exiting!\n");
    exit(1);
  }

  av2_resample_2d_8b(input, width, height, in_stride, &horz_rf, &vert_rf,
                     extra_prec_bits, &clip, output, width2, height2,
                     out_stride);
}

void av2_resample_2d_c(const int16_t *x, int inwidth, int inheight,
                       int instride, RationalResampleFilter *rfh,
                       RationalResampleFilter *rfv, int int_extra_bits,
                       ClipProfile *clip, int16_t *y, int outwidth,
                       int outheight, int outstride) {
  if (inwidth <= 0 || inheight <= 0 || outwidth <= 0 || outheight <= 0) return;
  if (rfv == NULL || is_resampler_noop(rfv)) {
    if (rfh == NULL || is_resampler_noop(rfh)) {
      for (int i = 0; i < inheight; ++i) {
        memcpy(y + i * outstride, x + i * instride, inwidth * sizeof(*x));
      }
      return;
    }
    const int left_h = rfh->length / 2;
    const int right_h = get_resample_right_border(inwidth, outwidth, rfh);
    int16_t *tmparr_ =
        (int16_t *)calloc(left_h + inwidth + right_h, sizeof(int16_t));
    int16_t *tmparrh = tmparr_ + left_h;
    for (int i = 0; i < inheight; ++i) {
      resample_1d_xc_c(x + instride * i, inwidth, rfh, rfh->filter_bits, clip,
                       y + i * outstride, outwidth, tmparrh);
    }
    free(tmparr_);
    return;
  }
  if (rfh == NULL || is_resampler_noop(rfh)) {
    const int left_v = rfv->length / 2;
    const int right_v = get_resample_right_border(inheight, outheight, rfv);
    const int arrsize = outheight + left_v + inheight + right_v;
    int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
    int16_t *tmparrv = tmparr_ + outheight + left_v;
    int16_t *tmparro = tmparr_;
    for (int i = 0; i < inwidth; ++i) {
      fill_col_to_arr(x + i, instride, inheight, tmparrv);
      resample_1d_xt_c(tmparrv, inheight, rfv, rfv->filter_bits, clip, tmparro,
                       outheight);
      fill_arr_to_col(y + i, outstride, outheight, tmparro);
    }
    free(tmparr_);
    return;
  }
  int16_t *tmpbuf =
      (int16_t *)calloc((size_t)outwidth * inheight, sizeof(int16_t));
  const int left_h = rfh->length / 2;
  const int right_h = get_resample_right_border(inwidth, outwidth, rfh);
  const int span_h = left_h + inwidth + right_h;
  const int left_v = rfv->length / 2;
  const int right_v = get_resample_right_border(inheight, outheight, rfv);
  const int span_v = left_v + inheight + right_v;
  const int arrsize = outheight + MAX(span_h, span_v);
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrh = tmparr_ + outheight + left_h;
  int16_t *tmparrv = tmparr_ + outheight + left_v;
  int16_t *tmparro = tmparr_;
  int tmpstride = outwidth;
  const int clip_bits = clip ? clip->bits : 14;
  int_extra_bits = MAX(0, MIN(int_extra_bits, 14 - clip_bits));
  const int downshifth = rfh->filter_bits - int_extra_bits;
  const int downshiftv = rfh->filter_bits + int_extra_bits;
  for (int i = 0; i < inheight; ++i) {
    resample_1d_xc_c(x + instride * i, inwidth, rfh, downshifth, NULL,
                     tmpbuf + i * tmpstride, outwidth, tmparrh);
  }
  for (int i = 0; i < outwidth; ++i) {
    fill_col_to_arr(tmpbuf + i, outwidth, inheight, tmparrv);
    resample_1d_xt_c(tmparrv, inheight, rfv, downshiftv, clip, tmparro,
                     outheight);
    fill_arr_to_col(y + i, outstride, outheight, tmparro);
  }
  free(tmpbuf);
  free(tmparr_);
}

void av2_resample_plane_2d_lanczos_hv_c(const uint16_t *const input, int height,
                                        int width, int in_stride,
                                        uint16_t *output, int height2,
                                        int width2, int out_stride, int subx,
                                        int suby, int bd, int denom_hor,
                                        int num_hor, int denom_ver, int num_ver,
                                        int lanczos_a_hor, int lanczos_a_ver) {
  int coeff_prec_bits = 14;
  int extra_prec_bits = 6;
  WIN_TYPE win = WIN_LANCZOS;
  EXT_TYPE ext = EXT_REPEAT;
  ClipProfile clip = { bd, 0 };
  double horz_x0 = subx ? (double)('d') : (double)('c');
  double vert_x0 = (double)('c');
  RationalResampleFilter horz_rf;
  RationalResampleFilter vert_rf;

  if (!get_resample_filter(num_hor, denom_hor, lanczos_a_hor, horz_x0, ext, win,
                           subx, coeff_prec_bits, &horz_rf)) {
    fprintf(stderr, "Cannot generate filter, exiting!\n");
    exit(1);
  }
  if (!get_resample_filter(num_ver, denom_ver, lanczos_a_ver, vert_x0, ext, win,
                           suby, coeff_prec_bits, &vert_rf)) {
    fprintf(stderr, "Cannot generate filter, exiting!\n");
    exit(1);
  }

  av2_resample_2d_c((const int16_t *)input, width, height, in_stride, &horz_rf,
                    &vert_rf, extra_prec_bits, &clip, (int16_t *)output, width2,
                    height2, out_stride);
}

void av2_resample_2d_8b_c(const uint8_t *x, int inwidth, int inheight,
                          int instride, RationalResampleFilter *rfh,
                          RationalResampleFilter *rfv, int int_extra_bits,
                          ClipProfile *clip, uint8_t *y, int outwidth,
                          int outheight, int outstride) {
  if (inwidth <= 0 || inheight <= 0 || outwidth <= 0 || outheight <= 0) return;
  if (rfv == NULL || is_resampler_noop(rfv)) {
    resample_horz_8b(x, inwidth, inheight, instride, rfh, clip, y, outwidth,
                     outstride);
    return;
  }
  if (rfh == NULL || is_resampler_noop(rfh)) {
    const int left_v = rfv->length / 2;
    const int right_v = get_resample_right_border(inheight, outheight, rfv);
    const int arrsize = outheight + left_v + inheight + right_v;
    int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
    int16_t *tmparrv = tmparr_ + outheight + left_v;
    int16_t *tmparro = tmparr_;
    for (int i = 0; i < inwidth; ++i) {
      fill_col_to_arr_in8b(x + i, instride, inheight, tmparrv);
      resample_1d_xt_c(tmparrv, inheight, rfv, rfv->filter_bits, clip, tmparro,
                       outheight);
      fill_arr_to_col_out8b(y + i, outstride, outheight, tmparro);
    }
    free(tmparr_);
    return;
  }
  int16_t *tmpbuf =
      (int16_t *)calloc((size_t)outwidth * inheight, sizeof(int16_t));
  const int left_h = rfh->length / 2;
  const int right_h = get_resample_right_border(inwidth, outwidth, rfh);
  const int span_h = left_h + inwidth + right_h;
  const int left_v = rfv->length / 2;
  const int right_v = get_resample_right_border(inheight, outheight, rfv);
  const int span_v = left_v + inheight + right_v;
  const int arrsize = outheight + MAX(span_h, span_v);
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrh = tmparr_ + outheight + left_h;
  int16_t *tmparrv = tmparr_ + outheight + left_v;
  int16_t *tmparro = tmparr_;
  int tmpstride = outwidth;
  const int clip_bits = clip ? clip->bits : 14;
  int_extra_bits = MAX(0, MIN(int_extra_bits, 14 - clip_bits));
  const int downshifth = rfh->filter_bits - int_extra_bits;
  const int downshiftv = rfh->filter_bits + int_extra_bits;
  for (int i = 0; i < inheight; ++i) {
    resample_1d_xc_in8b(x + instride * i, inwidth, rfh, downshifth, NULL,
                        tmpbuf + i * tmpstride, outwidth, (uint8_t *)tmparrh);
  }
  for (int i = 0; i < outwidth; ++i) {
    fill_col_to_arr(tmpbuf + i, outwidth, inheight, tmparrv);
    resample_1d_xt_c(tmparrv, inheight, rfv, downshiftv, clip, tmparro,
                     outheight);
    fill_arr_to_col_out8b(y + i, outstride, outheight, tmparro);
  }
  free(tmpbuf);
  free(tmparr_);
}

void av2_resample_plane_2d_8b_lanczos_c(const uint8_t *const input, int height,
                                        int width, int in_stride,
                                        uint8_t *output, int height2,
                                        int width2, int out_stride, int subx,
                                        int suby, int bd, int denom, int num,
                                        int lanczos_a_hor, int lanczos_a_ver) {
  int coeff_prec_bits = 14;
  int extra_prec_bits = 6;
  WIN_TYPE win = WIN_LANCZOS;
  EXT_TYPE ext = EXT_REPEAT;
  ClipProfile clip = { bd, 0 };
  double horz_x0 = subx ? (double)('d') : (double)('c');
  double vert_x0 = (double)('c');
  RationalResampleFilter horz_rf;
  RationalResampleFilter vert_rf;

  if (!get_resample_filter(num, denom, lanczos_a_hor, horz_x0, ext, win, subx,
                           coeff_prec_bits, &horz_rf)) {
    fprintf(stderr, "Cannot generate filter, exiting!\n");
    exit(1);
  }
  if (!get_resample_filter(num, denom, lanczos_a_ver, vert_x0, ext, win, suby,
                           coeff_prec_bits, &vert_rf)) {
    fprintf(stderr, "Cannot generate filter, exiting!\n");
    exit(1);
  }

  av2_resample_2d_8b_c(input, width, height, in_stride, &horz_rf, &vert_rf,
                       extra_prec_bits, &clip, output, width2, height2,
                       out_stride);
}
