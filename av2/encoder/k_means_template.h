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
#include <stdint.h>
#include <string.h>

#include "av2/encoder/palette.h"
#include "av2/encoder/random.h"

#ifndef AV2_K_MEANS_DIM
#error "This template requires AV2_K_MEANS_DIM to be defined"
#endif

#define RENAME_(x, y) AV2_K_MEANS_RENAME(x, y)
#define RENAME(x) RENAME_(x, AV2_K_MEANS_DIM)

static int RENAME(calc_dist)(const int *p1, const int *p2) {
  int dist = 0;
  for (int i = 0; i < AV2_K_MEANS_DIM; ++i) {
    const int diff = p1[i] - p2[i];
    dist += diff * diff;
  }
  return dist;
}

void RENAME(av2_calc_indices)(const int *data, const int *centroids,
                              uint8_t *indices, int n, int k) {
  for (int i = 0; i < n; ++i) {
    int min_dist = RENAME(calc_dist)(data + i * AV2_K_MEANS_DIM, centroids);
    indices[i] = 0;
    for (int j = 1; j < k; ++j) {
      const int this_dist = RENAME(calc_dist)(data + i * AV2_K_MEANS_DIM,
                                              centroids + j * AV2_K_MEANS_DIM);
      if (this_dist < min_dist) {
        min_dist = this_dist;
        indices[i] = j;
      }
    }
  }
}

static void RENAME(calc_centroids)(const int *data, int *centroids,
                                   const uint8_t *indices, int n, int k) {
  int i, j;
  int count[PALETTE_MAX_SIZE] = { 0 };
  unsigned int rand_state = (unsigned int)data[0];
  assert(n <= 32768);
  memset(centroids, 0, sizeof(centroids[0]) * k * AV2_K_MEANS_DIM);

  for (i = 0; i < n; ++i) {
    const int index = indices[i];
    assert(index < k);
    ++count[index];
    for (j = 0; j < AV2_K_MEANS_DIM; ++j) {
      centroids[index * AV2_K_MEANS_DIM + j] += data[i * AV2_K_MEANS_DIM + j];
    }
  }

  for (i = 0; i < k; ++i) {
    if (count[i] == 0) {
      memcpy(centroids + i * AV2_K_MEANS_DIM,
             data + (lcg_rand16(&rand_state) % n) * AV2_K_MEANS_DIM,
             sizeof(centroids[0]) * AV2_K_MEANS_DIM);
    } else {
      for (j = 0; j < AV2_K_MEANS_DIM; ++j) {
        centroids[i * AV2_K_MEANS_DIM + j] =
            DIVIDE_AND_ROUND(centroids[i * AV2_K_MEANS_DIM + j], count[i]);
      }
    }
  }
}

static int64_t RENAME(calc_total_dist)(const int *data, const int *centroids,
                                       const uint8_t *indices, int n, int k) {
  int64_t dist = 0;
  (void)k;
  for (int i = 0; i < n; ++i) {
    dist += RENAME(calc_dist)(data + i * AV2_K_MEANS_DIM,
                              centroids + indices[i] * AV2_K_MEANS_DIM);
  }
  return dist;
}

void RENAME(av2_k_means)(const int *data, int *centroids, uint8_t *indices,
                         int n, int k, int max_itr) {
  int pre_centroids[2 * PALETTE_MAX_SIZE];
  uint8_t pre_indices[MAX_SB_SQUARE];

  RENAME(av2_calc_indices)(data, centroids, indices, n, k);
  int64_t this_dist = RENAME(calc_total_dist)(data, centroids, indices, n, k);

  for (int i = 0; i < max_itr; ++i) {
    const int64_t pre_dist = this_dist;
    memcpy(pre_centroids, centroids,
           sizeof(pre_centroids[0]) * k * AV2_K_MEANS_DIM);
    memcpy(pre_indices, indices, sizeof(pre_indices[0]) * n);

    RENAME(calc_centroids)(data, centroids, indices, n, k);
    RENAME(av2_calc_indices)(data, centroids, indices, n, k);
    this_dist = RENAME(calc_total_dist)(data, centroids, indices, n, k);

    if (this_dist > pre_dist) {
      memcpy(centroids, pre_centroids,
             sizeof(pre_centroids[0]) * k * AV2_K_MEANS_DIM);
      memcpy(indices, pre_indices, sizeof(pre_indices[0]) * n);
      break;
    }
    if (!memcmp(centroids, pre_centroids,
                sizeof(pre_centroids[0]) * k * AV2_K_MEANS_DIM))
      break;
  }
}
#undef RENAME_
#undef RENAME
