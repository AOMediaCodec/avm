/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#define DIP_ROWS 64
#define DIP_COLS 16
#define DIP_BITS 12
#define DIP_OFFSET (1 << (12 - 1))
#define DIP_SCALE 4
#define DIP_FEATURES 11

extern const uint16_t av1_intra_matrix_weights[][DIP_ROWS][DIP_COLS];

void av1_intra_matrix_pred(const uint16_t *input, int mode, uint16_t *output,
                           int bd);
