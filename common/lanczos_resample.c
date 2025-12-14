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
  double offset[MAX_RATIONAL_FACTOR + 1];
  int intpel[MAX_RATIONAL_FACTOR];
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

// Assume x buffer is already extended on both sides with x pointing to the
// leftmost pixel, and the extension values are already filled up.
static void resample_1d_core(const int16_t *x, int inlen,
                             RationalResampleFilter *rf, int downshift,
                             ClipProfile *clip, int16_t *y, int outlen) {
  (void)inlen;
  const int tapsby2 = rf->length / 2;
  const int16_t *xext = x;
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

static void extend_border(int16_t *x, int inlen, EXT_TYPE ext_type,
                          int border) {
  switch (ext_type) {
    case EXT_REPEAT:
      for (int i = -border; i < 0; ++i) x[i] = x[0];
      for (int i = 0; i < border; ++i) x[i + inlen] = x[inlen - 1];
      break;
    case EXT_SYMMETRIC:
      if (inlen >= border) {
        for (int i = -border; i < 0; ++i) x[i] = x[-i - 1];
        for (int i = 0; i < border; ++i) x[i + inlen] = x[inlen - 1 - i];
      } else {
        for (int i = -border; i < 0; ++i)
          x[i] = x[(-i - 1 > inlen - 1 ? inlen - 1 : -i - 1)];
        for (int i = 0; i < border; ++i)
          x[i + inlen] = x[(inlen - 1 - i < 0 ? 0 : inlen - 1 - i)];
      }
      break;
    case EXT_REFLECT:
      if (inlen > border) {
        for (int i = -border; i < 0; ++i) x[i] = x[-i];
        for (int i = 0; i < border; ++i) x[i + inlen] = x[inlen - 2 - i];
      } else {
        for (int i = -border; i < 0; ++i)
          x[i] = x[(-i > inlen - 1 ? inlen - 1 : -i)];
        for (int i = 0; i < border; ++i)
          x[i + inlen] = x[(inlen - 2 - i < 0 ? 0 : inlen - 2 - i)];
      }
      break;
    case EXT_GRADIENT:
      if (inlen > border) {
        for (int i = -border; i < 0; ++i) x[i] = 2 * x[0] - x[-i];
        for (int i = 0; i < border; ++i)
          x[i + inlen] = 2 * x[inlen - 1] - x[inlen - 2 - i];
      } else {
        for (int i = -border; i < 0; ++i)
          x[i] = 2 * x[0] - x[(-i > inlen - 1 ? inlen - 1 : -i)];
        for (int i = 0; i < border; ++i)
          x[i + inlen] =
              2 * x[inlen - 1] - x[(inlen - 2 - i < 0 ? 0 : inlen - 2 - i)];
      }
      break;
  }
}

// Assume x buffer is already extended on both sides with x pointing to the
// leftmost pixel, but the extension values are not filled up.
static void resample_1d_xt(int16_t *x, int inlen, RationalResampleFilter *rf,
                           int downshift, ClipProfile *clip, int16_t *y,
                           int outlen) {
  extend_border(x, inlen, rf->ext_type, rf->length / 2);
  resample_1d_core(x, inlen, rf, downshift, clip, y, outlen);
}

// Assume a scratch buffer xext of size inlen + rf->length is provided
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

void resample_1d(const int16_t *x, int inlen, RationalResampleFilter *rf,
                 int downshift, ClipProfile *clip, int16_t *y, int outlen) {
  const int tapsby2 = rf->length / 2;
  int16_t *xext_ = (int16_t *)malloc((inlen + rf->length) * sizeof(*x));
  int16_t *xext = xext_ + tapsby2;

  resample_1d_xc(x, inlen, rf, downshift, clip, y, outlen, xext);

  free(xext_);
}

void av2_resample_2d(const int16_t *x, int inwidth, int inheight, int instride,
                     RationalResampleFilter *rfh, RationalResampleFilter *rfv,
                     int int_extra_bits, ClipProfile *clip, int16_t *y,
                     int outwidth, int outheight, int outstride) {
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
  int16_t *tmpbuf = (int16_t *)malloc(sizeof(int16_t) * outwidth * inheight);
  const int arrsize =
      outheight + ((inheight + rfv->length > inwidth + rfh->length)
                       ? (inheight + rfv->length)
                       : (inwidth + rfh->length));
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrh = tmparr_ + outheight + rfh->length / 2;
  int16_t *tmparrv = tmparr_ + outheight + rfv->length / 2;
  int16_t *tmparro = tmparr_;
  int tmpstride = outwidth;
  // intermediate data is stored in 16 bit buffers, so limit int_extra_bits
  int_extra_bits = MIN(int_extra_bits, 14 - clip->bits);
  const int downshifth = rfh->filter_bits - int_extra_bits;
  const int downshiftv = rfh->filter_bits + int_extra_bits;
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
  const int arrsize = inwidth + rfh->length;
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrh = tmparr_ + rfh->length / 2;
  for (int i = 0; i < inheight; ++i) {
    resample_1d_xc(x + instride * i, inwidth, rfh, rfh->filter_bits, clip,
                   y + i * outstride, outwidth, tmparrh);
  }
  free(tmparr_);
}

void resample_vert(const int16_t *x, int inwidth, int inheight, int instride,
                   RationalResampleFilter *rfv, ClipProfile *clip, int16_t *y,
                   int outheight, int outstride) {
  const int arrsize = outheight + inheight + rfv->length;
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrv = tmparr_ + outheight + rfv->length / 2;
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

static void extend_border_8b(uint8_t *x, int inlen, EXT_TYPE ext_type,
                             int border) {
  switch (ext_type) {
    case EXT_REPEAT:
      for (int i = -border; i < 0; ++i) x[i] = x[0];
      for (int i = 0; i < border; ++i) x[i + inlen] = x[inlen - 1];
      break;
    case EXT_SYMMETRIC:
      if (inlen >= border) {
        for (int i = -border; i < 0; ++i) x[i] = x[-i - 1];
        for (int i = 0; i < border; ++i) x[i + inlen] = x[inlen - 1 - i];
      } else {
        for (int i = -border; i < 0; ++i)
          x[i] = x[(-i - 1 > inlen - 1 ? inlen - 1 : -i - 1)];
        for (int i = 0; i < border; ++i)
          x[i + inlen] = x[(inlen - 1 - i < 0 ? 0 : inlen - 1 - i)];
      }
      break;
    case EXT_REFLECT:
      if (inlen > border) {
        for (int i = -border; i < 0; ++i) x[i] = x[-i];
        for (int i = 0; i < border; ++i) x[i + inlen] = x[inlen - 2 - i];
      } else {
        for (int i = -border; i < 0; ++i)
          x[i] = x[(-i > inlen - 1 ? inlen - 1 : -i)];
        for (int i = 0; i < border; ++i)
          x[i + inlen] = x[(inlen - 2 - i < 0 ? 0 : inlen - 2 - i)];
      }
      break;
    case EXT_GRADIENT:
      if (inlen > border) {
        for (int i = -border; i < 0; ++i) {
          const int t = 2 * x[0] - x[-i];
          x[i] = (uint8_t)doclip(t, 0, 255);
        }
        for (int i = 0; i < border; ++i) {
          const int t = 2 * x[inlen - 1] - x[inlen - 2 - i];
          x[i + inlen] = (uint8_t)doclip(t, 0, 255);
        }
      } else {
        for (int i = -border; i < 0; ++i) {
          const int t = 2 * x[0] - x[(-i > inlen - 1 ? inlen - 1 : -i)];
          x[i] = (uint8_t)doclip(t, 0, 255);
        }
        for (int i = 0; i < border; ++i) {
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
  extend_border_8b(x, inlen, rf->ext_type, rf->length / 2);
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
  extend_border_8b(x, inlen, rf->ext_type, rf->length / 2);
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
  uint8_t *xext_ = (uint8_t *)malloc((inlen + rf->length) * sizeof(*x));
  uint8_t *xext = xext_ + tapsby2;

  resample_1d_xc_8b(x, inlen, rf, downshift, clip, y, outlen, xext);

  free(xext_);
}

void av2_resample_2d_8b(const uint8_t *x, int inwidth, int inheight,
                        int instride, RationalResampleFilter *rfh,
                        RationalResampleFilter *rfv, int int_extra_bits,
                        ClipProfile *clip, uint8_t *y, int outwidth,
                        int outheight, int outstride) {
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
  int16_t *tmpbuf = (int16_t *)malloc(sizeof(int16_t) * outwidth * inheight);
  const int arrsize =
      outheight + ((inheight + rfv->length > inwidth + rfh->length)
                       ? (inheight + rfv->length)
                       : (inwidth + rfh->length));
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrh = tmparr_ + outheight + rfh->length / 2;
  int16_t *tmparrv = tmparr_ + outheight + rfv->length / 2;
  int16_t *tmparro = tmparr_;
  int tmpstride = outwidth;
  // intermediate data is stored in 16 bit buffers, so limit int_extra_bits
  int_extra_bits = MIN(int_extra_bits, 14 - clip->bits);
  const int downshifth = rfh->filter_bits - int_extra_bits;
  const int downshiftv = rfh->filter_bits + int_extra_bits;
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
  const int arrsize = inwidth + rfh->length;
  uint8_t *tmparr_ = (uint8_t *)calloc(arrsize, sizeof(*tmparr_));
  uint8_t *tmparrh = tmparr_ + rfh->length / 2;
  for (int i = 0; i < inheight; ++i) {
    resample_1d_xc_8b(x + instride * i, inwidth, rfh, rfh->filter_bits, clip,
                      y + i * outstride, outwidth, tmparrh);
  }
  free(tmparr_);
}

void resample_vert_8b(const uint8_t *x, int inwidth, int inheight, int instride,
                      RationalResampleFilter *rfv, ClipProfile *clip,
                      uint8_t *y, int outheight, int outstride) {
  const int arrsize = outheight + inheight + rfv->length;
  int16_t *tmparr_ = (int16_t *)calloc(arrsize, sizeof(int16_t));
  int16_t *tmparrv = tmparr_ + outheight + rfv->length / 2;
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

void av2_resample_plane_2d_lanczos(const uint16_t *const input, int height,
                                   int width, int in_stride, uint16_t *output,
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

  av2_resample_2d((const int16_t *)input, width, height, in_stride, &horz_rf,
                  &vert_rf, extra_prec_bits, &clip, (int16_t *)output, width2,
                  height2, out_stride);
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
