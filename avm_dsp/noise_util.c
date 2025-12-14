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

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avm_dsp/noise_util.h"
#include "avm_dsp/fft_common.h"
#include "avm_mem/avm_mem.h"
#include "config/avm_dsp_rtcd.h"

float avm_noise_psd_get_default_value(int block_size, float factor) {
  return (factor * factor / 10000) * block_size * block_size / 8;
}

// Internal representation of noise transform. It keeps track of the
// transformed data and a temporary working buffer to use during the
// transform.
struct avm_noise_tx_t {
  float *tx_block;
  float *temp;
  int block_size;
  void (*fft)(const float *, float *, float *);
  void (*ifft)(const float *, float *, float *);
};

struct avm_noise_tx_t *avm_noise_tx_malloc(int block_size) {
  struct avm_noise_tx_t *noise_tx =
      (struct avm_noise_tx_t *)avm_malloc(sizeof(struct avm_noise_tx_t));
  if (!noise_tx) return NULL;
  memset(noise_tx, 0, sizeof(*noise_tx));
  switch (block_size) {
    case 2:
      noise_tx->fft = avm_fft2x2_float;
      noise_tx->ifft = avm_ifft2x2_float;
      break;
    case 4:
      noise_tx->fft = avm_fft4x4_float;
      noise_tx->ifft = avm_ifft4x4_float;
      break;
    case 8:
      noise_tx->fft = avm_fft8x8_float;
      noise_tx->ifft = avm_ifft8x8_float;
      break;
    case 16:
      noise_tx->fft = avm_fft16x16_float;
      noise_tx->ifft = avm_ifft16x16_float;
      break;
    case 32:
      noise_tx->fft = avm_fft32x32_float;
      noise_tx->ifft = avm_ifft32x32_float;
      break;
    default:
      avm_free(noise_tx);
      fprintf(stderr, "Unsupported block size %d\n", block_size);
      return NULL;
  }
  noise_tx->block_size = block_size;
  noise_tx->tx_block = (float *)avm_memalign(
      32, 2 * sizeof(*noise_tx->tx_block) * block_size * block_size);
  noise_tx->temp = (float *)avm_memalign(
      32, 2 * sizeof(*noise_tx->temp) * block_size * block_size);
  if (!noise_tx->tx_block || !noise_tx->temp) {
    avm_noise_tx_free(noise_tx);
    return NULL;
  }
  // Clear the buffers up front. Some outputs of the forward transform are
  // real only (the imaginary component will never be touched)
  memset(noise_tx->tx_block, 0,
         2 * sizeof(*noise_tx->tx_block) * block_size * block_size);
  memset(noise_tx->temp, 0,
         2 * sizeof(*noise_tx->temp) * block_size * block_size);
  return noise_tx;
}

void avm_noise_tx_forward(struct avm_noise_tx_t *noise_tx, const float *data) {
  noise_tx->fft(data, noise_tx->temp, noise_tx->tx_block);
}

void avm_noise_tx_filter(struct avm_noise_tx_t *noise_tx, const float *psd) {
  const int block_size = noise_tx->block_size;
  const float kBeta = 1.1f;
  const float kEps = 1e-6f;
  for (int y = 0; y < block_size; ++y) {
    for (int x = 0; x < block_size; ++x) {
      int i = y * block_size + x;
      float *c = noise_tx->tx_block + 2 * i;
      const float c0 = AVMMAX((float)fabs(c[0]), 1e-8f);
      const float c1 = AVMMAX((float)fabs(c[1]), 1e-8f);
      const float p = c0 * c0 + c1 * c1;
      if (p > kBeta * psd[i] && p > 1e-6) {
        noise_tx->tx_block[2 * i + 0] *= (p - psd[i]) / AVMMAX(p, kEps);
        noise_tx->tx_block[2 * i + 1] *= (p - psd[i]) / AVMMAX(p, kEps);
      } else {
        noise_tx->tx_block[2 * i + 0] *= (kBeta - 1.0f) / kBeta;
        noise_tx->tx_block[2 * i + 1] *= (kBeta - 1.0f) / kBeta;
      }
    }
  }
}

void avm_noise_tx_inverse(struct avm_noise_tx_t *noise_tx, float *data) {
  const int n = noise_tx->block_size * noise_tx->block_size;
  noise_tx->ifft(noise_tx->tx_block, noise_tx->temp, data);
  for (int i = 0; i < n; ++i) {
    data[i] /= n;
  }
}

void avm_noise_tx_add_energy(const struct avm_noise_tx_t *noise_tx,
                             float *psd) {
  const int block_size = noise_tx->block_size;
  for (int yb = 0; yb < block_size; ++yb) {
    for (int xb = 0; xb <= block_size / 2; ++xb) {
      float *c = noise_tx->tx_block + 2 * (yb * block_size + xb);
      psd[yb * block_size + xb] += c[0] * c[0] + c[1] * c[1];
    }
  }
}

void avm_noise_tx_free(struct avm_noise_tx_t *noise_tx) {
  if (!noise_tx) return;
  avm_free(noise_tx->tx_block);
  avm_free(noise_tx->temp);
  avm_free(noise_tx);
}

double avm_normalized_cross_correlation(const double *a, const double *b,
                                        int n) {
  double c = 0;
  double a_len = 0;
  double b_len = 0;
  for (int i = 0; i < n; ++i) {
    a_len += a[i] * a[i];
    b_len += b[i] * b[i];
    c += a[i] * b[i];
  }
  return c / (sqrt(a_len) * sqrt(b_len));
}

int avm_noise_data_validate(const double *data, int w, int h) {
  const double kVarianceThreshold = 2;
  const double kMeanThreshold = 2;

  int x = 0, y = 0;
  int ret_value = 1;
  double var = 0, mean = 0;
  double *mean_x, *mean_y, *var_x, *var_y;

  // Check that noise variance is not increasing in x or y
  // and that the data is zero mean.
  mean_x = (double *)avm_malloc(sizeof(*mean_x) * w);
  var_x = (double *)avm_malloc(sizeof(*var_x) * w);
  mean_y = (double *)avm_malloc(sizeof(*mean_x) * h);
  var_y = (double *)avm_malloc(sizeof(*var_y) * h);

  memset(mean_x, 0, sizeof(*mean_x) * w);
  memset(var_x, 0, sizeof(*var_x) * w);
  memset(mean_y, 0, sizeof(*mean_y) * h);
  memset(var_y, 0, sizeof(*var_y) * h);

  for (y = 0; y < h; ++y) {
    for (x = 0; x < w; ++x) {
      const double d = data[y * w + x];
      var_x[x] += d * d;
      var_y[y] += d * d;
      mean_x[x] += d;
      mean_y[y] += d;
      var += d * d;
      mean += d;
    }
  }
  mean /= (w * h);
  var = var / (w * h) - mean * mean;

  for (y = 0; y < h; ++y) {
    mean_y[y] /= h;
    var_y[y] = var_y[y] / h - mean_y[y] * mean_y[y];
    if (fabs(var_y[y] - var) >= kVarianceThreshold) {
      fprintf(stderr, "Variance distance too large %f %f\n", var_y[y], var);
      ret_value = 0;
      break;
    }
    if (fabs(mean_y[y] - mean) >= kMeanThreshold) {
      fprintf(stderr, "Mean distance too large %f %f\n", mean_y[y], mean);
      ret_value = 0;
      break;
    }
  }

  for (x = 0; x < w; ++x) {
    mean_x[x] /= w;
    var_x[x] = var_x[x] / w - mean_x[x] * mean_x[x];
    if (fabs(var_x[x] - var) >= kVarianceThreshold) {
      fprintf(stderr, "Variance distance too large %f %f\n", var_x[x], var);
      ret_value = 0;
      break;
    }
    if (fabs(mean_x[x] - mean) >= kMeanThreshold) {
      fprintf(stderr, "Mean distance too large %f %f\n", mean_x[x], mean);
      ret_value = 0;
      break;
    }
  }

  avm_free(mean_x);
  avm_free(mean_y);
  avm_free(var_x);
  avm_free(var_y);

  return ret_value;
}
