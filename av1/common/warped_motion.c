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
#include <memory.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>

#include "config/av1_rtcd.h"

#include "av1/common/reconinter.h"
#include "av1/common/warped_motion.h"
#include "av1/common/scale.h"

// For warping, we really use a 6-tap filter, but we do blocks of 8 pixels
// at a time. The zoom/rotation/shear in the model are applied to the
// "fractional" position of each pixel, which therefore varies within
// [-1, 2) * WARPEDPIXEL_PREC_SHIFTS.
// We need an extra 2 taps to fit this in, for a total of 8 taps.
/* clang-format off */
const int16_t av1_warped_filter[WARPEDPIXEL_PREC_SHIFTS * 3 + 1][8] = {
#if WARPEDPIXEL_PREC_BITS == 6
  // [-1, 0)
  { 0,   0, 127,   1,   0, 0, 0, 0 }, { 0, - 1, 127,   2,   0, 0, 0, 0 },
  { 1, - 3, 127,   4, - 1, 0, 0, 0 }, { 1, - 4, 126,   6, - 2, 1, 0, 0 },
  { 1, - 5, 126,   8, - 3, 1, 0, 0 }, { 1, - 6, 125,  11, - 4, 1, 0, 0 },
  { 1, - 7, 124,  13, - 4, 1, 0, 0 }, { 2, - 8, 123,  15, - 5, 1, 0, 0 },
  { 2, - 9, 122,  18, - 6, 1, 0, 0 }, { 2, -10, 121,  20, - 6, 1, 0, 0 },
  { 2, -11, 120,  22, - 7, 2, 0, 0 }, { 2, -12, 119,  25, - 8, 2, 0, 0 },
  { 3, -13, 117,  27, - 8, 2, 0, 0 }, { 3, -13, 116,  29, - 9, 2, 0, 0 },
  { 3, -14, 114,  32, -10, 3, 0, 0 }, { 3, -15, 113,  35, -10, 2, 0, 0 },
  { 3, -15, 111,  37, -11, 3, 0, 0 }, { 3, -16, 109,  40, -11, 3, 0, 0 },
  { 3, -16, 108,  42, -12, 3, 0, 0 }, { 4, -17, 106,  45, -13, 3, 0, 0 },
  { 4, -17, 104,  47, -13, 3, 0, 0 }, { 4, -17, 102,  50, -14, 3, 0, 0 },
  { 4, -17, 100,  52, -14, 3, 0, 0 }, { 4, -18,  98,  55, -15, 4, 0, 0 },
  { 4, -18,  96,  58, -15, 3, 0, 0 }, { 4, -18,  94,  60, -16, 4, 0, 0 },
  { 4, -18,  91,  63, -16, 4, 0, 0 }, { 4, -18,  89,  65, -16, 4, 0, 0 },
  { 4, -18,  87,  68, -17, 4, 0, 0 }, { 4, -18,  85,  70, -17, 4, 0, 0 },
  { 4, -18,  82,  73, -17, 4, 0, 0 }, { 4, -18,  80,  75, -17, 4, 0, 0 },
  { 4, -18,  78,  78, -18, 4, 0, 0 }, { 4, -17,  75,  80, -18, 4, 0, 0 },
  { 4, -17,  73,  82, -18, 4, 0, 0 }, { 4, -17,  70,  85, -18, 4, 0, 0 },
  { 4, -17,  68,  87, -18, 4, 0, 0 }, { 4, -16,  65,  89, -18, 4, 0, 0 },
  { 4, -16,  63,  91, -18, 4, 0, 0 }, { 4, -16,  60,  94, -18, 4, 0, 0 },
  { 3, -15,  58,  96, -18, 4, 0, 0 }, { 4, -15,  55,  98, -18, 4, 0, 0 },
  { 3, -14,  52, 100, -17, 4, 0, 0 }, { 3, -14,  50, 102, -17, 4, 0, 0 },
  { 3, -13,  47, 104, -17, 4, 0, 0 }, { 3, -13,  45, 106, -17, 4, 0, 0 },
  { 3, -12,  42, 108, -16, 3, 0, 0 }, { 3, -11,  40, 109, -16, 3, 0, 0 },
  { 3, -11,  37, 111, -15, 3, 0, 0 }, { 2, -10,  35, 113, -15, 3, 0, 0 },
  { 3, -10,  32, 114, -14, 3, 0, 0 }, { 2, - 9,  29, 116, -13, 3, 0, 0 },
  { 2, - 8,  27, 117, -13, 3, 0, 0 }, { 2, - 8,  25, 119, -12, 2, 0, 0 },
  { 2, - 7,  22, 120, -11, 2, 0, 0 }, { 1, - 6,  20, 121, -10, 2, 0, 0 },
  { 1, - 6,  18, 122, - 9, 2, 0, 0 }, { 1, - 5,  15, 123, - 8, 2, 0, 0 },
  { 1, - 4,  13, 124, - 7, 1, 0, 0 }, { 1, - 4,  11, 125, - 6, 1, 0, 0 },
  { 1, - 3,   8, 126, - 5, 1, 0, 0 }, { 1, - 2,   6, 126, - 4, 1, 0, 0 },
  { 0, - 1,   4, 127, - 3, 1, 0, 0 }, { 0,   0,   2, 127, - 1, 0, 0, 0 },

  // [0, 1)
  { 0,  0,   0, 127,   1,   0,  0,  0}, { 0,  0,  -1, 127,   2,   0,  0,  0},
  { 0,  1,  -3, 127,   4,  -2,  1,  0}, { 0,  1,  -5, 127,   6,  -2,  1,  0},
  { 0,  2,  -6, 126,   8,  -3,  1,  0}, {-1,  2,  -7, 126,  11,  -4,  2, -1},
  {-1,  3,  -8, 125,  13,  -5,  2, -1}, {-1,  3, -10, 124,  16,  -6,  3, -1},
  {-1,  4, -11, 123,  18,  -7,  3, -1}, {-1,  4, -12, 122,  20,  -7,  3, -1},
  {-1,  4, -13, 121,  23,  -8,  3, -1}, {-2,  5, -14, 120,  25,  -9,  4, -1},
  {-1,  5, -15, 119,  27, -10,  4, -1}, {-1,  5, -16, 118,  30, -11,  4, -1},
  {-2,  6, -17, 116,  33, -12,  5, -1}, {-2,  6, -17, 114,  35, -12,  5, -1},
  {-2,  6, -18, 113,  38, -13,  5, -1}, {-2,  7, -19, 111,  41, -14,  6, -2},
  {-2,  7, -19, 110,  43, -15,  6, -2}, {-2,  7, -20, 108,  46, -15,  6, -2},
  {-2,  7, -20, 106,  49, -16,  6, -2}, {-2,  7, -21, 104,  51, -16,  7, -2},
  {-2,  7, -21, 102,  54, -17,  7, -2}, {-2,  8, -21, 100,  56, -18,  7, -2},
  {-2,  8, -22,  98,  59, -18,  7, -2}, {-2,  8, -22,  96,  62, -19,  7, -2},
  {-2,  8, -22,  94,  64, -19,  7, -2}, {-2,  8, -22,  91,  67, -20,  8, -2},
  {-2,  8, -22,  89,  69, -20,  8, -2}, {-2,  8, -22,  87,  72, -21,  8, -2},
  {-2,  8, -21,  84,  74, -21,  8, -2}, {-2,  8, -22,  82,  77, -21,  8, -2},
  {-2,  8, -21,  79,  79, -21,  8, -2}, {-2,  8, -21,  77,  82, -22,  8, -2},
  {-2,  8, -21,  74,  84, -21,  8, -2}, {-2,  8, -21,  72,  87, -22,  8, -2},
  {-2,  8, -20,  69,  89, -22,  8, -2}, {-2,  8, -20,  67,  91, -22,  8, -2},
  {-2,  7, -19,  64,  94, -22,  8, -2}, {-2,  7, -19,  62,  96, -22,  8, -2},
  {-2,  7, -18,  59,  98, -22,  8, -2}, {-2,  7, -18,  56, 100, -21,  8, -2},
  {-2,  7, -17,  54, 102, -21,  7, -2}, {-2,  7, -16,  51, 104, -21,  7, -2},
  {-2,  6, -16,  49, 106, -20,  7, -2}, {-2,  6, -15,  46, 108, -20,  7, -2},
  {-2,  6, -15,  43, 110, -19,  7, -2}, {-2,  6, -14,  41, 111, -19,  7, -2},
  {-1,  5, -13,  38, 113, -18,  6, -2}, {-1,  5, -12,  35, 114, -17,  6, -2},
  {-1,  5, -12,  33, 116, -17,  6, -2}, {-1,  4, -11,  30, 118, -16,  5, -1},
  {-1,  4, -10,  27, 119, -15,  5, -1}, {-1,  4,  -9,  25, 120, -14,  5, -2},
  {-1,  3,  -8,  23, 121, -13,  4, -1}, {-1,  3,  -7,  20, 122, -12,  4, -1},
  {-1,  3,  -7,  18, 123, -11,  4, -1}, {-1,  3,  -6,  16, 124, -10,  3, -1},
  {-1,  2,  -5,  13, 125,  -8,  3, -1}, {-1,  2,  -4,  11, 126,  -7,  2, -1},
  { 0,  1,  -3,   8, 126,  -6,  2,  0}, { 0,  1,  -2,   6, 127,  -5,  1,  0},
  { 0,  1,  -2,   4, 127,  -3,  1,  0}, { 0,  0,   0,   2, 127,  -1,  0,  0},

  // [1, 2)
  { 0, 0, 0,   1, 127,   0,   0, 0 }, { 0, 0, 0, - 1, 127,   2,   0, 0 },
  { 0, 0, 1, - 3, 127,   4, - 1, 0 }, { 0, 0, 1, - 4, 126,   6, - 2, 1 },
  { 0, 0, 1, - 5, 126,   8, - 3, 1 }, { 0, 0, 1, - 6, 125,  11, - 4, 1 },
  { 0, 0, 1, - 7, 124,  13, - 4, 1 }, { 0, 0, 2, - 8, 123,  15, - 5, 1 },
  { 0, 0, 2, - 9, 122,  18, - 6, 1 }, { 0, 0, 2, -10, 121,  20, - 6, 1 },
  { 0, 0, 2, -11, 120,  22, - 7, 2 }, { 0, 0, 2, -12, 119,  25, - 8, 2 },
  { 0, 0, 3, -13, 117,  27, - 8, 2 }, { 0, 0, 3, -13, 116,  29, - 9, 2 },
  { 0, 0, 3, -14, 114,  32, -10, 3 }, { 0, 0, 3, -15, 113,  35, -10, 2 },
  { 0, 0, 3, -15, 111,  37, -11, 3 }, { 0, 0, 3, -16, 109,  40, -11, 3 },
  { 0, 0, 3, -16, 108,  42, -12, 3 }, { 0, 0, 4, -17, 106,  45, -13, 3 },
  { 0, 0, 4, -17, 104,  47, -13, 3 }, { 0, 0, 4, -17, 102,  50, -14, 3 },
  { 0, 0, 4, -17, 100,  52, -14, 3 }, { 0, 0, 4, -18,  98,  55, -15, 4 },
  { 0, 0, 4, -18,  96,  58, -15, 3 }, { 0, 0, 4, -18,  94,  60, -16, 4 },
  { 0, 0, 4, -18,  91,  63, -16, 4 }, { 0, 0, 4, -18,  89,  65, -16, 4 },
  { 0, 0, 4, -18,  87,  68, -17, 4 }, { 0, 0, 4, -18,  85,  70, -17, 4 },
  { 0, 0, 4, -18,  82,  73, -17, 4 }, { 0, 0, 4, -18,  80,  75, -17, 4 },
  { 0, 0, 4, -18,  78,  78, -18, 4 }, { 0, 0, 4, -17,  75,  80, -18, 4 },
  { 0, 0, 4, -17,  73,  82, -18, 4 }, { 0, 0, 4, -17,  70,  85, -18, 4 },
  { 0, 0, 4, -17,  68,  87, -18, 4 }, { 0, 0, 4, -16,  65,  89, -18, 4 },
  { 0, 0, 4, -16,  63,  91, -18, 4 }, { 0, 0, 4, -16,  60,  94, -18, 4 },
  { 0, 0, 3, -15,  58,  96, -18, 4 }, { 0, 0, 4, -15,  55,  98, -18, 4 },
  { 0, 0, 3, -14,  52, 100, -17, 4 }, { 0, 0, 3, -14,  50, 102, -17, 4 },
  { 0, 0, 3, -13,  47, 104, -17, 4 }, { 0, 0, 3, -13,  45, 106, -17, 4 },
  { 0, 0, 3, -12,  42, 108, -16, 3 }, { 0, 0, 3, -11,  40, 109, -16, 3 },
  { 0, 0, 3, -11,  37, 111, -15, 3 }, { 0, 0, 2, -10,  35, 113, -15, 3 },
  { 0, 0, 3, -10,  32, 114, -14, 3 }, { 0, 0, 2, - 9,  29, 116, -13, 3 },
  { 0, 0, 2, - 8,  27, 117, -13, 3 }, { 0, 0, 2, - 8,  25, 119, -12, 2 },
  { 0, 0, 2, - 7,  22, 120, -11, 2 }, { 0, 0, 1, - 6,  20, 121, -10, 2 },
  { 0, 0, 1, - 6,  18, 122, - 9, 2 }, { 0, 0, 1, - 5,  15, 123, - 8, 2 },
  { 0, 0, 1, - 4,  13, 124, - 7, 1 }, { 0, 0, 1, - 4,  11, 125, - 6, 1 },
  { 0, 0, 1, - 3,   8, 126, - 5, 1 }, { 0, 0, 1, - 2,   6, 126, - 4, 1 },
  { 0, 0, 0, - 1,   4, 127, - 3, 1 }, { 0, 0, 0,   0,   2, 127, - 1, 0 },
  // dummy (replicate row index 191)
  { 0, 0, 0,   0,   2, 127, - 1, 0 },

#elif WARPEDPIXEL_PREC_BITS == 5
  // [-1, 0)
  {0,   0, 127,   1,   0, 0, 0, 0}, {1,  -3, 127,   4,  -1, 0, 0, 0},
  {1,  -5, 126,   8,  -3, 1, 0, 0}, {1,  -7, 124,  13,  -4, 1, 0, 0},
  {2,  -9, 122,  18,  -6, 1, 0, 0}, {2, -11, 120,  22,  -7, 2, 0, 0},
  {3, -13, 117,  27,  -8, 2, 0, 0}, {3, -14, 114,  32, -10, 3, 0, 0},
  {3, -15, 111,  37, -11, 3, 0, 0}, {3, -16, 108,  42, -12, 3, 0, 0},
  {4, -17, 104,  47, -13, 3, 0, 0}, {4, -17, 100,  52, -14, 3, 0, 0},
  {4, -18,  96,  58, -15, 3, 0, 0}, {4, -18,  91,  63, -16, 4, 0, 0},
  {4, -18,  87,  68, -17, 4, 0, 0}, {4, -18,  82,  73, -17, 4, 0, 0},
  {4, -18,  78,  78, -18, 4, 0, 0}, {4, -17,  73,  82, -18, 4, 0, 0},
  {4, -17,  68,  87, -18, 4, 0, 0}, {4, -16,  63,  91, -18, 4, 0, 0},
  {3, -15,  58,  96, -18, 4, 0, 0}, {3, -14,  52, 100, -17, 4, 0, 0},
  {3, -13,  47, 104, -17, 4, 0, 0}, {3, -12,  42, 108, -16, 3, 0, 0},
  {3, -11,  37, 111, -15, 3, 0, 0}, {3, -10,  32, 114, -14, 3, 0, 0},
  {2,  -8,  27, 117, -13, 3, 0, 0}, {2,  -7,  22, 120, -11, 2, 0, 0},
  {1,  -6,  18, 122,  -9, 2, 0, 0}, {1,  -4,  13, 124,  -7, 1, 0, 0},
  {1,  -3,   8, 126,  -5, 1, 0, 0}, {0,  -1,   4, 127,  -3, 1, 0, 0},
  // [0, 1)
  { 0,  0,   0, 127,   1,   0,   0,  0}, { 0,  1,  -3, 127,   4,  -2,   1,  0},
  { 0,  2,  -6, 126,   8,  -3,   1,  0}, {-1,  3,  -8, 125,  13,  -5,   2, -1},
  {-1,  4, -11, 123,  18,  -7,   3, -1}, {-1,  4, -13, 121,  23,  -8,   3, -1},
  {-1,  5, -15, 119,  27, -10,   4, -1}, {-2,  6, -17, 116,  33, -12,   5, -1},
  {-2,  6, -18, 113,  38, -13,   5, -1}, {-2,  7, -19, 110,  43, -15,   6, -2},
  {-2,  7, -20, 106,  49, -16,   6, -2}, {-2,  7, -21, 102,  54, -17,   7, -2},
  {-2,  8, -22,  98,  59, -18,   7, -2}, {-2,  8, -22,  94,  64, -19,   7, -2},
  {-2,  8, -22,  89,  69, -20,   8, -2}, {-2,  8, -21,  84,  74, -21,   8, -2},
  {-2,  8, -21,  79,  79, -21,   8, -2}, {-2,  8, -21,  74,  84, -21,   8, -2},
  {-2,  8, -20,  69,  89, -22,   8, -2}, {-2,  7, -19,  64,  94, -22,   8, -2},
  {-2,  7, -18,  59,  98, -22,   8, -2}, {-2,  7, -17,  54, 102, -21,   7, -2},
  {-2,  6, -16,  49, 106, -20,   7, -2}, {-2,  6, -15,  43, 110, -19,   7, -2},
  {-1,  5, -13,  38, 113, -18,   6, -2}, {-1,  5, -12,  33, 116, -17,   6, -2},
  {-1,  4, -10,  27, 119, -15,   5, -1}, {-1,  3,  -8,  23, 121, -13,   4, -1},
  {-1,  3,  -7,  18, 123, -11,   4, -1}, {-1,  2,  -5,  13, 125,  -8,   3, -1},
  { 0,  1,  -3,   8, 126,  -6,   2,  0}, { 0,  1,  -2,   4, 127,  -3,   1,  0},
  // [1, 2)
  {0, 0, 0,   1, 127,   0,   0, 0}, {0, 0, 1,  -3, 127,   4,  -1, 0},
  {0, 0, 1,  -5, 126,   8,  -3, 1}, {0, 0, 1,  -7, 124,  13,  -4, 1},
  {0, 0, 2,  -9, 122,  18,  -6, 1}, {0, 0, 2, -11, 120,  22,  -7, 2},
  {0, 0, 3, -13, 117,  27,  -8, 2}, {0, 0, 3, -14, 114,  32, -10, 3},
  {0, 0, 3, -15, 111,  37, -11, 3}, {0, 0, 3, -16, 108,  42, -12, 3},
  {0, 0, 4, -17, 104,  47, -13, 3}, {0, 0, 4, -17, 100,  52, -14, 3},
  {0, 0, 4, -18,  96,  58, -15, 3}, {0, 0, 4, -18,  91,  63, -16, 4},
  {0, 0, 4, -18,  87,  68, -17, 4}, {0, 0, 4, -18,  82,  73, -17, 4},
  {0, 0, 4, -18,  78,  78, -18, 4}, {0, 0, 4, -17,  73,  82, -18, 4},
  {0, 0, 4, -17,  68,  87, -18, 4}, {0, 0, 4, -16,  63,  91, -18, 4},
  {0, 0, 3, -15,  58,  96, -18, 4}, {0, 0, 3, -14,  52, 100, -17, 4},
  {0, 0, 3, -13,  47, 104, -17, 4}, {0, 0, 3, -12,  42, 108, -16, 3},
  {0, 0, 3, -11,  37, 111, -15, 3}, {0, 0, 3, -10,  32, 114, -14, 3},
  {0, 0, 2,  -8,  27, 117, -13, 3}, {0, 0, 2,  -7,  22, 120, -11, 2},
  {0, 0, 1,  -6,  18, 122,  -9, 2}, {0, 0, 1,  -4,  13, 124,  -7, 1},
  {0, 0, 1,  -3,   8, 126,  -5, 1}, {0, 0, 0,  -1,   4, 127,  -3, 1},
  // dummy (replicate row index 95)
  {0, 0, 0,  -1,   4, 127,  -3, 1},

#endif  // WARPEDPIXEL_PREC_BITS == 6
};

#if CONFIG_EXT_WARP_FILTER
DECLARE_ALIGNED(16, const int16_t,
                av1_ext_warped_filter[EXT_WARP_PHASES + 1][EXT_WARP_STORAGE_TAPS]) = {
// The extended warp filter is a 6-tap filter, but we store each kernel with
// two extra zeros at the end so that each kernel is 16-byte aligned
{ 0,   0, 128,   0,   0, 0, 0, 0 },
{ 0,  -1, 127,   2,   0, 0, 0, 0 },
{ 0,  -2, 127,   4,  -1, 0, 0, 0 },
{ 0,  -3, 126,   6,  -1, 0, 0, 0 },
{ 1,  -4, 125,   8,  -2, 0, 0, 0 },
{ 1,  -5, 124,  11,  -3, 0, 0, 0 },
{ 1,  -6, 123,  13,  -3, 0, 0, 0 },
{ 1,  -6, 122,  15,  -4, 0, 0, 0 },
{ 1,  -7, 120,  17,  -4, 1, 0, 0 },
{ 1,  -8, 119,  20,  -5, 1, 0, 0 },
{ 1,  -9, 118,  22,  -5, 1, 0, 0 },
{ 1,  -9, 117,  24,  -6, 1, 0, 0 },
{ 1, -10, 115,  27,  -6, 1, 0, 0 },
{ 1, -10, 114,  29,  -7, 1, 0, 0 },
{ 1, -11, 112,  32,  -7, 1, 0, 0 },
{ 1, -11, 111,  34,  -8, 1, 0, 0 },
{ 1, -11, 109,  36,  -8, 1, 0, 0 },
{ 2, -12, 107,  39,  -9, 1, 0, 0 },
{ 2, -12, 105,  41,  -9, 1, 0, 0 },
{ 2, -12, 103,  44, -10, 1, 0, 0 },
{ 2, -13, 102,  46, -10, 1, 0, 0 },
{ 2, -13,  99,  49, -10, 1, 0, 0 },
{ 2, -13,  98,  51, -11, 1, 0, 0 },
{ 2, -13,  95,  54, -11, 1, 0, 0 },
{ 2, -14,  93,  56, -11, 2, 0, 0 },
{ 2, -14,  91,  59, -12, 2, 0, 0 },
{ 2, -14,  89,  61, -12, 2, 0, 0 },
{ 2, -14,  87,  63, -12, 2, 0, 0 },
{ 2, -14,  85,  66, -13, 2, 0, 0 },
{ 2, -14,  83,  68, -13, 2, 0, 0 },
{ 2, -14,  80,  71, -13, 2, 0, 0 },
{ 2, -14,  78,  73, -13, 2, 0, 0 },
{ 2, -13,  75,  75, -13, 2, 0, 0 },
{ 2, -13,  73,  78, -14, 2, 0, 0 },
{ 2, -13,  71,  80, -14, 2, 0, 0 },
{ 2, -13,  68,  83, -14, 2, 0, 0 },
{ 2, -13,  66,  85, -14, 2, 0, 0 },
{ 2, -12,  63,  87, -14, 2, 0, 0 },
{ 2, -12,  61,  89, -14, 2, 0, 0 },
{ 2, -12,  59,  91, -14, 2, 0, 0 },
{ 2, -11,  56,  93, -14, 2, 0, 0 },
{ 1, -11,  54,  95, -13, 2, 0, 0 },
{ 1, -11,  51,  98, -13, 2, 0, 0 },
{ 1, -10,  49,  99, -13, 2, 0, 0 },
{ 1, -10,  46, 102, -13, 2, 0, 0 },
{ 1, -10,  44, 103, -12, 2, 0, 0 },
{ 1,  -9,  41, 105, -12, 2, 0, 0 },
{ 1,  -9,  39, 107, -12, 2, 0, 0 },
{ 1,  -8,  36, 109, -11, 1, 0, 0 },
{ 1,  -8,  34, 111, -11, 1, 0, 0 },
{ 1,  -7,  32, 112, -11, 1, 0, 0 },
{ 1,  -7,  29, 114, -10, 1, 0, 0 },
{ 1,  -6,  27, 115, -10, 1, 0, 0 },
{ 1,  -6,  24, 117,  -9, 1, 0, 0 },
{ 1,  -5,  22, 118,  -9, 1, 0, 0 },
{ 1,  -5,  20, 119,  -8, 1, 0, 0 },
{ 1,  -4,  17, 120,  -7, 1, 0, 0 },
{ 0,  -4,  15, 122,  -6, 1, 0, 0 },
{ 0,  -3,  13, 123,  -6, 1, 0, 0 },
{ 0,  -3,  11, 124,  -5, 1, 0, 0 },
{ 0,  -2,   8, 125,  -4, 1, 0, 0 },
{ 0,  -1,   6, 126,  -3, 0, 0, 0 },
{ 0,  -1,   4, 127,  -2, 0, 0, 0 },
{ 0,   0,   2, 127,  -1, 0, 0, 0 },
{ 0,   0,   0, 128,  0, 0, 0, 0 },
};
#endif  // CONFIG_EXT_WARP_FILTER

/* clang-format on */

// Recompute the translational part of a warp model, so that the center
// of the current block (determined by `mi_row`, `mi_col`, `bsize`)
// has an induced motion vector of `mv`
void av1_set_warp_translation(int mi_row, int mi_col, BLOCK_SIZE bsize, MV mv,
                              WarpedMotionParams *wm) {
  const int center_x = mi_col * MI_SIZE + block_size_wide[bsize] / 2 - 1;
  const int center_y = mi_row * MI_SIZE + block_size_high[bsize] / 2 - 1;

  // Note(rachelbarker): We subtract 1 from the diagonal part of the model here.
  // This is because the warp model M maps (current frame) pixel coordinates to
  // (ref frame) pixel coordinates. So, in order to calculate the induced
  // motion vector, we have to subtract the identity matrix.
  wm->wmmat[0] = mv.col * (1 << (WARPEDMODEL_PREC_BITS - 3)) -
                 (center_x * (wm->wmmat[2] - (1 << WARPEDMODEL_PREC_BITS)) +
                  center_y * wm->wmmat[3]);
  wm->wmmat[1] = mv.row * (1 << (WARPEDMODEL_PREC_BITS - 3)) -
                 (center_x * wm->wmmat[4] +
                  center_y * (wm->wmmat[5] - (1 << WARPEDMODEL_PREC_BITS)));

#if CONFIG_EXTENDED_WARP_PREDICTION
  wm->wmmat[0] = clamp(wm->wmmat[0], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - (1 << WARP_PARAM_REDUCE_BITS));
  wm->wmmat[1] = clamp(wm->wmmat[1], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - (1 << WARP_PARAM_REDUCE_BITS));
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
}

const uint16_t div_lut[DIV_LUT_NUM + 1] = {
  16384, 16320, 16257, 16194, 16132, 16070, 16009, 15948, 15888, 15828, 15768,
  15709, 15650, 15592, 15534, 15477, 15420, 15364, 15308, 15252, 15197, 15142,
  15087, 15033, 14980, 14926, 14873, 14821, 14769, 14717, 14665, 14614, 14564,
  14513, 14463, 14413, 14364, 14315, 14266, 14218, 14170, 14122, 14075, 14028,
  13981, 13935, 13888, 13843, 13797, 13752, 13707, 13662, 13618, 13574, 13530,
  13487, 13443, 13400, 13358, 13315, 13273, 13231, 13190, 13148, 13107, 13066,
  13026, 12985, 12945, 12906, 12866, 12827, 12788, 12749, 12710, 12672, 12633,
  12596, 12558, 12520, 12483, 12446, 12409, 12373, 12336, 12300, 12264, 12228,
  12193, 12157, 12122, 12087, 12053, 12018, 11984, 11950, 11916, 11882, 11848,
  11815, 11782, 11749, 11716, 11683, 11651, 11619, 11586, 11555, 11523, 11491,
  11460, 11429, 11398, 11367, 11336, 11305, 11275, 11245, 11215, 11185, 11155,
  11125, 11096, 11067, 11038, 11009, 10980, 10951, 10923, 10894, 10866, 10838,
  10810, 10782, 10755, 10727, 10700, 10673, 10645, 10618, 10592, 10565, 10538,
  10512, 10486, 10460, 10434, 10408, 10382, 10356, 10331, 10305, 10280, 10255,
  10230, 10205, 10180, 10156, 10131, 10107, 10082, 10058, 10034, 10010, 9986,
  9963,  9939,  9916,  9892,  9869,  9846,  9823,  9800,  9777,  9754,  9732,
  9709,  9687,  9664,  9642,  9620,  9598,  9576,  9554,  9533,  9511,  9489,
  9468,  9447,  9425,  9404,  9383,  9362,  9341,  9321,  9300,  9279,  9259,
  9239,  9218,  9198,  9178,  9158,  9138,  9118,  9098,  9079,  9059,  9039,
  9020,  9001,  8981,  8962,  8943,  8924,  8905,  8886,  8867,  8849,  8830,
  8812,  8793,  8775,  8756,  8738,  8720,  8702,  8684,  8666,  8648,  8630,
  8613,  8595,  8577,  8560,  8542,  8525,  8508,  8490,  8473,  8456,  8439,
  8422,  8405,  8389,  8372,  8355,  8339,  8322,  8306,  8289,  8273,  8257,
  8240,  8224,  8208,  8192,
};

static int is_affine_valid(const WarpedMotionParams *const wm) {
  const int32_t *mat = wm->wmmat;
  return (mat[2] > 0);
}

static int is_affine_shear_allowed(int16_t alpha, int16_t beta, int16_t gamma,
                                   int16_t delta) {
  if ((4 * abs(alpha) + 7 * abs(beta) >= (1 << WARPEDMODEL_PREC_BITS)) ||
      (4 * abs(gamma) + 4 * abs(delta) >= (1 << WARPEDMODEL_PREC_BITS)))
    return 0;
  else
    return 1;
}

// Returns 1 on success or 0 on an invalid affine set
int av1_get_shear_params(WarpedMotionParams *wm) {
  const int32_t *mat = wm->wmmat;
  if (!is_affine_valid(wm)) return 0;
  wm->alpha =
      clamp(mat[2] - (1 << WARPEDMODEL_PREC_BITS), INT16_MIN, INT16_MAX);
  wm->beta = clamp(mat[3], INT16_MIN, INT16_MAX);
  int16_t shift;
  int16_t y = resolve_divisor_32(abs(mat[2]), &shift) * (mat[2] < 0 ? -1 : 1);
  int64_t v = ((int64_t)mat[4] * (1 << WARPEDMODEL_PREC_BITS)) * y;
  wm->gamma =
      clamp((int)ROUND_POWER_OF_TWO_SIGNED_64(v, shift), INT16_MIN, INT16_MAX);
  v = ((int64_t)mat[3] * mat[4]) * y;
  wm->delta = clamp(mat[5] - (int)ROUND_POWER_OF_TWO_SIGNED_64(v, shift) -
                        (1 << WARPEDMODEL_PREC_BITS),
                    INT16_MIN, INT16_MAX);

  // Note(rachelbarker):
  // In extreme cases, the `clamp` operations in the previous block can set
  // parameters equal to to INT16_MAX == 32767.
  //
  // The following round-then-multiply, which is intended to reduce the bit
  // storage requirement in hardware, then rounds to 32768, which is outside
  // the range of an int16_t. But casting to int16_t is okay - it will cause
  // this value to become -32768, and so the model will be rejected
  // by is_affine_shear_allowed(), so the outcome is the same.
  //
  // However, we must make this cast explicit, because otherwise the integer
  // sanitizer (correctly) complains about overflow during an implicit cast
  wm->alpha =
      (int16_t)(ROUND_POWER_OF_TWO_SIGNED(wm->alpha, WARP_PARAM_REDUCE_BITS) *
                (1 << WARP_PARAM_REDUCE_BITS));
  wm->beta =
      (int16_t)(ROUND_POWER_OF_TWO_SIGNED(wm->beta, WARP_PARAM_REDUCE_BITS) *
                (1 << WARP_PARAM_REDUCE_BITS));
  wm->gamma =
      (int16_t)(ROUND_POWER_OF_TWO_SIGNED(wm->gamma, WARP_PARAM_REDUCE_BITS) *
                (1 << WARP_PARAM_REDUCE_BITS));
  wm->delta =
      (int16_t)(ROUND_POWER_OF_TWO_SIGNED(wm->delta, WARP_PARAM_REDUCE_BITS) *
                (1 << WARP_PARAM_REDUCE_BITS));

#if !CONFIG_EXT_WARP_FILTER
  if (!is_affine_shear_allowed(wm->alpha, wm->beta, wm->gamma, wm->delta))
    return 0;
#endif  // CONFIG_EXT_WARP_FILTER

  return 1;
}

#if CONFIG_EXTENDED_WARP_PREDICTION
// Reduce the precision of a warp model, ready for use in the warp filter
// and for storage. This should be called after the non-translational parameters
// are calculated, but before av1_set_warp_translation() or
// av1_get_shear_params() are called
//
// This also clamps the values. The clamping range is well outside the
// "useful" range (ie, what is_affine_shear_allowed() permits), but it
// ensures that hardware can store each value in a signed integer with
// (WARPEDMODEL_PREC_BITS - WARP_PARAM_REDUCE_BITS) total bits
void av1_reduce_warp_model(WarpedMotionParams *wm) {
#if CONFIG_EXT_WARP_FILTER
  // Constrain parameters so that they lie within the range of +/- 1/2
  // relative to the identity model.
  //
  // In order to avoid needing one extra bit, we limit the maximum to one
  // unit less than 1/2, similarly to how an int<n> can only go up to
  // 2^(n-1) - 1. However, unlike an int<n>, the allowable range must
  // remain symmetric, so that ROTZOOM models can maintain the constraint
  // that wmmat[4] == -wmmat[3].
  const int max_value =
      (1 << (WARPEDMODEL_PREC_BITS - 1)) - (1 << WARP_PARAM_REDUCE_BITS);
  const int min_value = -max_value;
#else
  // Think of this range as an int<N>, multiplied by (1 <<
  // WARP_PARAM_REDUCE_BITS). In other words, the max is -2^(N-1) and max is
  // (2^(N-1) - 1), but with an extra multiplier applied to both terms
  const int min_value = -(1 << (WARPEDMODEL_PREC_BITS - 1));
  const int max_value =
      (1 << (WARPEDMODEL_PREC_BITS - 1)) - (1 << WARP_PARAM_REDUCE_BITS);
#endif  // CONFIG_EXT_WARP_FILTER

  for (int i = 2; i < 6; i++) {
    int offset = (i == 2 || i == 5) ? (1 << WARPEDMODEL_PREC_BITS) : 0;

    int original = wm->wmmat[i] - offset;
    int rounded = ROUND_POWER_OF_TWO_SIGNED(original, WARP_PARAM_REDUCE_BITS) *
                  (1 << WARP_PARAM_REDUCE_BITS);
    int clamped = clamp(rounded, min_value, max_value);

    wm->wmmat[i] = clamped + offset;
  }
}
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

static INLINE int highbd_error_measure(int err, int bd) {
  const int b = bd - 8;
  const int bmask = (1 << b) - 1;
  const int v = (1 << b);
  err = abs(err);
  const int e1 = err >> b;
  const int e2 = err & bmask;
  return error_measure_lut[255 + e1] * (v - e2) +
         error_measure_lut[256 + e1] * e2;
}

/* The warp filter for ROTZOOM and AFFINE models works as follows:
   * Split the input into 8x8 blocks
   * For each block, project the point (4, 4) within the block, to get the
     overall block position. Split into integer and fractional coordinates,
     maintaining full WARPEDMODEL precision
   * Filter horizontally: Generate 15 rows of 8 pixels each. Each pixel gets a
     variable horizontal offset. This means that, while the rows of the
     intermediate buffer align with the rows of the *reference* image, the
     columns align with the columns of the *destination* image.
   * Filter vertically: Generate the output block (up to 8x8 pixels, but if the
     destination is too small we crop the output at this stage). Each pixel has
     a variable vertical offset, so that the resulting rows are aligned with
     the rows of the destination image.

   To accomplish these alignments, we factor the warp matrix as a
   product of two shear / asymmetric zoom matrices:
   / a b \  = /   1       0    \ * / 1+alpha  beta \
   \ c d /    \ gamma  1+delta /   \    0      1   /
   where a, b, c, d are wmmat[2], wmmat[3], wmmat[4], wmmat[5] respectively.
   The horizontal shear (with alpha and beta) is applied first,
   then the vertical shear (with gamma and delta) is applied second.

   The only limitation is that, to fit this in a fixed 8-tap filter size,
   the fractional pixel offsets must be at most +-1. Since the horizontal filter
   generates 15 rows of 8 columns, and the initial point we project is at (4, 4)
   within the block, the parameters must satisfy
   4 * |alpha| + 7 * |beta| <= 1   and   4 * |gamma| + 4 * |delta| <= 1
   for this filter to be applicable.

   Note: This function assumes that the caller has done all of the relevant
   checks, ie. that we have a ROTZOOM or AFFINE model, that wm[4] and wm[5]
   are set appropriately (if using a ROTZOOM model), and that alpha, beta,
   gamma, delta are all in range.

   TODO(rachelbarker): Maybe support scaled references?
*/
/* A note on hardware implementation:
    The warp filter is intended to be implementable using the same hardware as
    the high-precision convolve filters from the loop-restoration and
    convolve-round experiments.

    For a single filter stage, considering all of the coefficient sets for the
    warp filter and the regular convolution filter, an input in the range
    [0, 2^k - 1] is mapped into the range [-56 * (2^k - 1), 184 * (2^k - 1)]
    before rounding.

    Allowing for some changes to the filter coefficient sets, call the range
    [-64 * 2^k, 192 * 2^k]. Then, if we initialize the accumulator to 64 * 2^k,
    we can replace this by the range [0, 256 * 2^k], which can be stored in an
    unsigned value with 8 + k bits.

    This allows the derivation of the appropriate bit widths and offsets for
    the various intermediate values: If

    F := FILTER_BITS = 7 (or else the above ranges need adjusting)
         So a *single* filter stage maps a k-bit input to a (k + F + 1)-bit
         intermediate value.
    H := ROUND0_BITS
    V := VERSHEAR_REDUCE_PREC_BITS
    (and note that we must have H + V = 2*F for the output to have the same
     scale as the input)

    then we end up with the following offsets and ranges:
    Horizontal filter: Apply an offset of 1 << (bd + F - 1), sum fits into a
                       uint{bd + F + 1}
    After rounding: The values stored in 'tmp' fit into a uint{bd + F + 1 - H}.
    Vertical filter: Apply an offset of 1 << (bd + 2*F - H), sum fits into a
                     uint{bd + 2*F + 2 - H}
    After rounding: The final value, before undoing the offset, fits into a
                    uint{bd + 2}.

    Then we need to undo the offsets before clamping to a pixel. Note that,
    if we do this at the end, the amount to subtract is actually independent
    of H and V:

    offset to subtract = (1 << ((bd + F - 1) - H + F - V)) +
                         (1 << ((bd + 2*F - H) - V))
                      == (1 << (bd - 1)) + (1 << bd)

    This allows us to entirely avoid clamping in both the warp filter and
    the convolve-round experiment. As of the time of writing, the Wiener filter
    from loop-restoration can encode a central coefficient up to 216, which
    leads to a maximum value of about 282 * 2^k after applying the offset.
    So in that case we still need to clamp.
*/
void av1_highbd_warp_affine_c(const int32_t *mat, const uint16_t *ref,
                              int width, int height, int stride, uint16_t *pred,
                              int p_col, int p_row, int p_width, int p_height,
                              int p_stride, int subsampling_x,
                              int subsampling_y, int bd,
                              ConvolveParams *conv_params, int16_t alpha,
                              int16_t beta, int16_t gamma, int16_t delta) {
  int32_t tmp[15 * 8];
  const int reduce_bits_horiz =
      conv_params->round_0 +
      AOMMAX(bd + FILTER_BITS - conv_params->round_0 - 14, 0);
  const int reduce_bits_vert = conv_params->is_compound
                                   ? conv_params->round_1
                                   : 2 * FILTER_BITS - reduce_bits_horiz;
  const int max_bits_horiz = bd + FILTER_BITS + 1 - reduce_bits_horiz;
  const int offset_bits_horiz = bd + FILTER_BITS - 1;
  const int offset_bits_vert = bd + 2 * FILTER_BITS - reduce_bits_horiz;
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  (void)max_bits_horiz;
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));

  for (int i = p_row; i < p_row + p_height; i += 8) {
    for (int j = p_col; j < p_col + p_width; j += 8) {
      // Calculate the center of this 8x8 block,
      // project to luma coordinates (if in a subsampled chroma plane),
      // apply the affine transformation,
      // then convert back to the original coordinates (if necessary)
      const int32_t src_x = (j + 4) << subsampling_x;
      const int32_t src_y = (i + 4) << subsampling_y;
      const int32_t dst_x = mat[2] * src_x + mat[3] * src_y + mat[0];
      const int32_t dst_y = mat[4] * src_x + mat[5] * src_y + mat[1];
      const int32_t x4 = dst_x >> subsampling_x;
      const int32_t y4 = dst_y >> subsampling_y;

      const int32_t ix4 = x4 >> WARPEDMODEL_PREC_BITS;
      int32_t sx4 = x4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t iy4 = y4 >> WARPEDMODEL_PREC_BITS;
      int32_t sy4 = y4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);

      sx4 += alpha * (-4) + beta * (-4);
      sy4 += gamma * (-4) + delta * (-4);

      sx4 &= ~((1 << WARP_PARAM_REDUCE_BITS) - 1);
      sy4 &= ~((1 << WARP_PARAM_REDUCE_BITS) - 1);

      // Horizontal filter
      for (int k = -7; k < 8; ++k) {
        const int iy = clamp(iy4 + k, 0, height - 1);

        int sx = sx4 + beta * (k + 4);
        for (int l = -4; l < 4; ++l) {
          int ix = ix4 + l - 3;
          const int offs = ROUND_POWER_OF_TWO(sx, WARPEDDIFF_PREC_BITS) +
                           WARPEDPIXEL_PREC_SHIFTS;
          assert(offs >= 0 && offs <= WARPEDPIXEL_PREC_SHIFTS * 3);
          const int16_t *coeffs = av1_warped_filter[offs];

          int32_t sum = 1 << offset_bits_horiz;
          for (int m = 0; m < 8; ++m) {
            const int sample_x = clamp(ix + m, 0, width - 1);
            sum += ref[iy * stride + sample_x] * coeffs[m];
          }
          sum = ROUND_POWER_OF_TWO(sum, reduce_bits_horiz);
          assert(0 <= sum && sum < (1 << max_bits_horiz));
          tmp[(k + 7) * 8 + (l + 4)] = sum;
          sx += alpha;
        }
      }

      // Vertical filter
      for (int k = -4; k < AOMMIN(4, p_row + p_height - i - 4); ++k) {
        int sy = sy4 + delta * (k + 4);
        for (int l = -4; l < AOMMIN(4, p_col + p_width - j - 4); ++l) {
          const int offs = ROUND_POWER_OF_TWO(sy, WARPEDDIFF_PREC_BITS) +
                           WARPEDPIXEL_PREC_SHIFTS;
          assert(offs >= 0 && offs <= WARPEDPIXEL_PREC_SHIFTS * 3);
          const int16_t *coeffs = av1_warped_filter[offs];

          int32_t sum = 1 << offset_bits_vert;
          for (int m = 0; m < 8; ++m) {
            sum += tmp[(k + m + 4) * 8 + (l + 4)] * coeffs[m];
          }

          if (conv_params->is_compound) {
            CONV_BUF_TYPE *p =
                &conv_params
                     ->dst[(i - p_row + k + 4) * conv_params->dst_stride +
                           (j - p_col + l + 4)];
            sum = ROUND_POWER_OF_TWO(sum, reduce_bits_vert);
            if (conv_params->do_average) {
              uint16_t *dst16 =
                  &pred[(i - p_row + k + 4) * p_stride + (j - p_col + l + 4)];
              int32_t tmp32 = *p;
              if (use_wtd_comp_avg) {
                tmp32 = tmp32 * conv_params->fwd_offset +
                        sum * conv_params->bck_offset;
                tmp32 = tmp32 >> DIST_PRECISION_BITS;
              } else {
                tmp32 += sum;
                tmp32 = tmp32 >> 1;
              }
              tmp32 = tmp32 - (1 << (offset_bits - conv_params->round_1)) -
                      (1 << (offset_bits - conv_params->round_1 - 1));
              *dst16 =
                  clip_pixel_highbd(ROUND_POWER_OF_TWO(tmp32, round_bits), bd);
            } else {
              *p = sum;
            }
          } else {
            uint16_t *p =
                &pred[(i - p_row + k + 4) * p_stride + (j - p_col + l + 4)];
            sum = ROUND_POWER_OF_TWO(sum, reduce_bits_vert);
            assert(0 <= sum && sum < (1 << (bd + 2)));
            *p = clip_pixel_highbd(sum - (1 << (bd - 1)) - (1 << bd), bd);
          }
          sy += gamma;
        }
      }
    }
  }
}

#if CONFIG_EXT_WARP_FILTER
void av1_ext_highbd_warp_affine_c(const int32_t *mat, const uint16_t *ref,
                                  int width, int height, int stride,
                                  uint16_t *pred, int p_col, int p_row,
                                  int p_width, int p_height, int p_stride,
                                  int subsampling_x, int subsampling_y, int bd,
                                  ConvolveParams *conv_params) {
  int32_t im_block[(4 + EXT_WARP_TAPS - 1) * 4];
  const int reduce_bits_horiz =
      conv_params->round_0 +
      AOMMAX(bd + FILTER_BITS - conv_params->round_0 - 14, 0);
  const int reduce_bits_vert = conv_params->is_compound
                                   ? conv_params->round_1
                                   : 2 * FILTER_BITS - reduce_bits_horiz;
  const int max_bits_horiz = bd + FILTER_BITS + 1 - reduce_bits_horiz;
  const int offset_bits_horiz = bd + FILTER_BITS - 1;
  const int offset_bits_vert = bd + 2 * FILTER_BITS - reduce_bits_horiz;
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  (void)max_bits_horiz;
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));

  for (int i = p_row; i < p_row + p_height; i += 4) {
    for (int j = p_col; j < p_col + p_width; j += 4) {
      // Calculate the center of this 4x4 block,
      // project to luma coordinates (if in a subsampled chroma plane),
      // apply the affine transformation,
      // then convert back to the original coordinates (if necessary)
      const int32_t src_x = (j + 2) << subsampling_x;
      const int32_t src_y = (i + 2) << subsampling_y;
      const int32_t dst_x = mat[2] * src_x + mat[3] * src_y + mat[0];
      const int32_t dst_y = mat[4] * src_x + mat[5] * src_y + mat[1];
      const int32_t x4 = dst_x >> subsampling_x;
      const int32_t y4 = dst_y >> subsampling_y;

      const int32_t ix4 = x4 >> WARPEDMODEL_PREC_BITS;
      int32_t sx4 = x4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t iy4 = y4 >> WARPEDMODEL_PREC_BITS;
      int32_t sy4 = y4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);

      // Horizontal Filter
      const int offs_x = ROUND_POWER_OF_TWO(sx4, EXT_WARP_ROUND_BITS);
      assert(offs_x >= 0 && offs_x <= EXT_WARP_PHASES);
      const int16_t *coeffs_x = av1_ext_warped_filter[offs_x];

      for (int k = -(EXT_WARP_TAPS_HALF + 1); k < EXT_WARP_TAPS_HALF + 2; ++k) {
        const int iy = clamp(iy4 + k, 0, height - 1);

        for (int l = -2; l < 2; ++l) {
          int ix = ix4 + l - (EXT_WARP_TAPS_HALF - 1);

          int32_t sum = 1 << offset_bits_horiz;
          for (int m = 0; m < EXT_WARP_TAPS; ++m) {
            const int sample_x = clamp(ix + m, 0, width - 1);
            sum += ref[iy * stride + sample_x] * coeffs_x[m];
          }
          sum = ROUND_POWER_OF_TWO(sum, reduce_bits_horiz);
          assert(0 <= sum && sum < (1 << max_bits_horiz));
          im_block[(k + (EXT_WARP_TAPS_HALF + 1)) * 4 + (l + 2)] = sum;
        }
      }

      // Vertical filter
      const int offs_y = ROUND_POWER_OF_TWO(sy4, WARPEDDIFF_PREC_BITS);
      assert(offs_y >= 0 && offs_y <= WARPEDPIXEL_PREC_SHIFTS);
      const int16_t *coeffs_y = av1_ext_warped_filter[offs_y];

      for (int k = -2; k < AOMMIN(2, p_row + p_height - i - 2); ++k) {
        for (int l = -2; l < AOMMIN(2, p_col + p_width - j - 2); ++l) {
          int32_t sum = 1 << offset_bits_vert;
          for (int m = 0; m < EXT_WARP_TAPS; ++m) {
            sum += im_block[(k + m + 2) * 4 + (l + 2)] * coeffs_y[m];
          }

          if (conv_params->is_compound) {
            CONV_BUF_TYPE *p =
                &conv_params
                     ->dst[(i - p_row + k + 2) * conv_params->dst_stride +
                           (j - p_col + l + 2)];
            sum = ROUND_POWER_OF_TWO(sum, reduce_bits_vert);
            if (conv_params->do_average) {
              uint16_t *dst16 =
                  &pred[(i - p_row + k + 2) * p_stride + (j - p_col + l + 2)];
              int32_t tmp32 = *p;
              if (use_wtd_comp_avg) {
                tmp32 = tmp32 * conv_params->fwd_offset +
                        sum * conv_params->bck_offset;
                tmp32 = tmp32 >> DIST_PRECISION_BITS;
              } else {
                tmp32 += sum;
                tmp32 = tmp32 >> 1;
              }
              tmp32 = tmp32 - (1 << (offset_bits - conv_params->round_1)) -
                      (1 << (offset_bits - conv_params->round_1 - 1));
              *dst16 =
                  clip_pixel_highbd(ROUND_POWER_OF_TWO(tmp32, round_bits), bd);
            } else {
              *p = sum;
            }
          } else {
            uint16_t *p =
                &pred[(i - p_row + k + 2) * p_stride + (j - p_col + l + 2)];
            sum = ROUND_POWER_OF_TWO(sum, reduce_bits_vert);
            assert(0 <= sum && sum < (1 << (bd + 2)));
            *p = clip_pixel_highbd(sum - (1 << (bd - 1)) - (1 << bd), bd);
          }
        }
      }
    }
  }
}
#endif  // CONFIG_EXT_WARP_FILTER

void highbd_warp_plane(WarpedMotionParams *wm, const uint16_t *const ref,
                       int width, int height, int stride, uint16_t *const pred,
                       int p_col, int p_row, int p_width, int p_height,
                       int p_stride, int subsampling_x, int subsampling_y,
                       int bd, ConvolveParams *conv_params) {
  assert(wm->wmtype <= AFFINE);
  if (wm->wmtype == ROTZOOM) {
    wm->wmmat[5] = wm->wmmat[2];
    wm->wmmat[4] = -wm->wmmat[3];
  }
  const int32_t *const mat = wm->wmmat;
  const int16_t alpha = wm->alpha;
  const int16_t beta = wm->beta;
  const int16_t gamma = wm->gamma;
  const int16_t delta = wm->delta;

#if CONFIG_EXT_WARP_FILTER
  if (!is_affine_shear_allowed(alpha, beta, gamma, delta))
    av1_ext_highbd_warp_affine(mat, ref, width, height, stride, pred, p_col,
                               p_row, p_width, p_height, p_stride,
                               subsampling_x, subsampling_y, bd, conv_params);
  else
#endif  // CONFIG_EXT_WARP_FILTER
    av1_highbd_warp_affine(mat, ref, width, height, stride, pred, p_col, p_row,
                           p_width, p_height, p_stride, subsampling_x,
                           subsampling_y, bd, conv_params, alpha, beta, gamma,
                           delta);
}

int64_t av1_calc_highbd_frame_error(const uint16_t *const ref, int stride,
                                    const uint16_t *const dst, int p_width,
                                    int p_height, int p_stride, int bd) {
  int64_t sum_error = 0;
  for (int i = 0; i < p_height; ++i) {
    for (int j = 0; j < p_width; ++j) {
      sum_error +=
          highbd_error_measure(dst[j + i * p_stride] - ref[j + i * stride], bd);
    }
  }
  return sum_error;
}

static int64_t highbd_segmented_frame_error(
    const uint16_t *const ref, int stride, const uint16_t *const dst,
    int p_width, int p_height, int p_stride, int bd, uint8_t *segment_map,
    int segment_map_stride) {
  int patch_w, patch_h;
  const int error_bsize_w = AOMMIN(p_width, WARP_ERROR_BLOCK);
  const int error_bsize_h = AOMMIN(p_height, WARP_ERROR_BLOCK);
  int64_t sum_error = 0;
  for (int i = 0; i < p_height; i += WARP_ERROR_BLOCK) {
    for (int j = 0; j < p_width; j += WARP_ERROR_BLOCK) {
      int seg_x = j >> WARP_ERROR_BLOCK_LOG;
      int seg_y = i >> WARP_ERROR_BLOCK_LOG;
      // Only compute the error if this block contains inliers from the motion
      // model
      if (!segment_map[seg_y * segment_map_stride + seg_x]) continue;

      // avoid computing error into the frame padding
      patch_w = AOMMIN(error_bsize_w, p_width - j);
      patch_h = AOMMIN(error_bsize_h, p_height - i);
      sum_error += av1_calc_highbd_frame_error(ref + j + i * stride, stride,
                                               dst + j + i * p_stride, patch_w,
                                               patch_h, p_stride, bd);
    }
  }
  return sum_error;
}

int64_t av1_frame_error(int bd, const uint16_t *ref, int stride, uint16_t *dst,
                        int p_width, int p_height, int p_stride) {
  return av1_calc_highbd_frame_error(ref, stride, dst, p_width, p_height,
                                     p_stride, bd);
}

int64_t av1_segmented_frame_error(int bd, const uint16_t *ref, int stride,
                                  uint16_t *dst, int p_width, int p_height,
                                  int p_stride, uint8_t *segment_map,
                                  int segment_map_stride) {
  return highbd_segmented_frame_error(ref, stride, dst, p_width, p_height,
                                      p_stride, bd, segment_map,
                                      segment_map_stride);
}

void av1_warp_plane(WarpedMotionParams *wm, int bd, const uint16_t *ref,
                    int width, int height, int stride, uint16_t *pred,
                    int p_col, int p_row, int p_width, int p_height,
                    int p_stride, int subsampling_x, int subsampling_y,
                    ConvolveParams *conv_params) {
  highbd_warp_plane(wm, ref, width, height, stride, pred, p_col, p_row, p_width,
                    p_height, p_stride, subsampling_x, subsampling_y, bd,
                    conv_params);
}

#define LS_MV_MAX 256  // max mv in 1/8-pel
// Use LS_STEP = 8 so that 2 less bits needed for A, Bx, By.
#define LS_STEP 8

// Assuming LS_MV_MAX is < MAX_SB_SIZE * 8,
// the precision needed is:
//   (MAX_SB_SIZE_LOG2 + 3) [for sx * sx magnitude] +
//   (MAX_SB_SIZE_LOG2 + 4) [for sx * dx magnitude] +
//   1 [for sign] +
//   LEAST_SQUARES_SAMPLES_MAX_BITS
//        [for adding up to LEAST_SQUARES_SAMPLES_MAX samples]
// The value is 23
#define LS_MAT_RANGE_BITS \
  ((MAX_SB_SIZE_LOG2 + 4) * 2 + LEAST_SQUARES_SAMPLES_MAX_BITS)

// Bit-depth reduction from the full-range
#define LS_MAT_DOWN_BITS 2

// bits range of A, Bx and By after downshifting
#define LS_MAT_BITS (LS_MAT_RANGE_BITS - LS_MAT_DOWN_BITS)
#define LS_MAT_MIN (-(1 << (LS_MAT_BITS - 1)))
#define LS_MAT_MAX ((1 << (LS_MAT_BITS - 1)) - 1)

// By setting LS_STEP = 8, the least 2 bits of every elements in A, Bx, By are
// 0. So, we can reduce LS_MAT_RANGE_BITS(2) bits here.
#define LS_SQUARE(a)                                          \
  (((a) * (a)*4 + (a)*4 * LS_STEP + LS_STEP * LS_STEP * 2) >> \
   (2 + LS_MAT_DOWN_BITS))
#define LS_PRODUCT1(a, b)                                           \
  (((a) * (b)*4 + ((a) + (b)) * 2 * LS_STEP + LS_STEP * LS_STEP) >> \
   (2 + LS_MAT_DOWN_BITS))
#define LS_PRODUCT2(a, b)                                               \
  (((a) * (b)*4 + ((a) + (b)) * 2 * LS_STEP + LS_STEP * LS_STEP * 2) >> \
   (2 + LS_MAT_DOWN_BITS))

#define USE_LIMITED_PREC_MULT 0

#if USE_LIMITED_PREC_MULT

#define MUL_PREC_BITS 16
static uint16_t resolve_multiplier_64(uint64_t D, int16_t *shift) {
  int msb = 0;
  uint16_t mult = 0;
  *shift = 0;
  if (D != 0) {
    msb = (int16_t)((D >> 32) ? get_msb((unsigned int)(D >> 32)) + 32
                              : get_msb((unsigned int)D));
    if (msb >= MUL_PREC_BITS) {
      mult = (uint16_t)ROUND_POWER_OF_TWO_64(D, msb + 1 - MUL_PREC_BITS);
      *shift = msb + 1 - MUL_PREC_BITS;
    } else {
      mult = (uint16_t)D;
      *shift = 0;
    }
  }
  return mult;
}

static int32_t get_mult_shift_ndiag(int64_t Px, int16_t iDet, int shift) {
  int32_t ret;
  int16_t mshift;
  uint16_t Mul = resolve_multiplier_64(llabs(Px), &mshift);
  int32_t v = (int32_t)Mul * (int32_t)iDet * (Px < 0 ? -1 : 1);
  shift -= mshift;
  if (shift > 0) {
    return (int32_t)clamp(ROUND_POWER_OF_TWO_SIGNED(v, shift),
                          -WARPEDMODEL_NONDIAGAFFINE_CLAMP + 1,
                          WARPEDMODEL_NONDIAGAFFINE_CLAMP - 1);
  } else {
    return (int32_t)clamp(v * (1 << (-shift)),
                          -WARPEDMODEL_NONDIAGAFFINE_CLAMP + 1,
                          WARPEDMODEL_NONDIAGAFFINE_CLAMP - 1);
  }
  return ret;
}

static int32_t get_mult_shift_diag(int64_t Px, int16_t iDet, int shift) {
  int16_t mshift;
  uint16_t Mul = resolve_multiplier_64(llabs(Px), &mshift);
  int32_t v = (int32_t)Mul * (int32_t)iDet * (Px < 0 ? -1 : 1);
  shift -= mshift;
  if (shift > 0) {
    return (int32_t)clamp(
        ROUND_POWER_OF_TWO_SIGNED(v, shift),
        (1 << WARPEDMODEL_PREC_BITS) - WARPEDMODEL_NONDIAGAFFINE_CLAMP + 1,
        (1 << WARPEDMODEL_PREC_BITS) + WARPEDMODEL_NONDIAGAFFINE_CLAMP - 1);
  } else {
    return (int32_t)clamp(
        v * (1 << (-shift)),
        (1 << WARPEDMODEL_PREC_BITS) - WARPEDMODEL_NONDIAGAFFINE_CLAMP + 1,
        (1 << WARPEDMODEL_PREC_BITS) + WARPEDMODEL_NONDIAGAFFINE_CLAMP - 1);
  }
}

#else

static int32_t get_mult_shift_ndiag(int64_t Px, int16_t iDet, int shift) {
  int64_t v = Px * (int64_t)iDet;
  return (int32_t)clamp64(ROUND_POWER_OF_TWO_SIGNED_64(v, shift),
                          -WARPEDMODEL_NONDIAGAFFINE_CLAMP + 1,
                          WARPEDMODEL_NONDIAGAFFINE_CLAMP - 1);
}

static int32_t get_mult_shift_diag(int64_t Px, int16_t iDet, int shift) {
  int64_t v = Px * (int64_t)iDet;
  return (int32_t)clamp64(
      ROUND_POWER_OF_TWO_SIGNED_64(v, shift),
      (1 << WARPEDMODEL_PREC_BITS) - WARPEDMODEL_NONDIAGAFFINE_CLAMP + 1,
      (1 << WARPEDMODEL_PREC_BITS) + WARPEDMODEL_NONDIAGAFFINE_CLAMP - 1);
}
#endif  // USE_LIMITED_PREC_MULT

static int find_affine_int(int np, const int *pts1, const int *pts2,
                           BLOCK_SIZE bsize, MV mv, WarpedMotionParams *wm,
                           int mi_row, int mi_col) {
  int32_t A[2][2] = { { 0, 0 }, { 0, 0 } };
  int32_t Bx[2] = { 0, 0 };
  int32_t By[2] = { 0, 0 };

  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const int rsuy = bh / 2 - 1;
  const int rsux = bw / 2 - 1;
  const int suy = rsuy * 8;
  const int sux = rsux * 8;
  const int duy = suy + mv.row;
  const int dux = sux + mv.col;

  // Assume the center pixel of the block has exactly the same motion vector
  // as transmitted for the block. First shift the origin of the source
  // points to the block center, and the origin of the destination points to
  // the block center added to the motion vector transmitted.
  // Let (xi, yi) denote the source points and (xi', yi') denote destination
  // points after origin shfifting, for i = 0, 1, 2, .... n-1.
  // Then if  P = [x0, y0,
  //               x1, y1
  //               x2, y1,
  //                ....
  //              ]
  //          q = [x0', x1', x2', ... ]'
  //          r = [y0', y1', y2', ... ]'
  // the least squares problems that need to be solved are:
  //          [h1, h2]' = inv(P'P)P'q and
  //          [h3, h4]' = inv(P'P)P'r
  // where the affine transformation is given by:
  //          x' = h1.x + h2.y
  //          y' = h3.x + h4.y
  //
  // The loop below computes: A = P'P, Bx = P'q, By = P'r
  // We need to just compute inv(A).Bx and inv(A).By for the solutions.
  // Contribution from neighbor block
  for (int i = 0; i < np; i++) {
    const int dx = pts2[i * 2] - dux;
    const int dy = pts2[i * 2 + 1] - duy;
    const int sx = pts1[i * 2] - sux;
    const int sy = pts1[i * 2 + 1] - suy;
    // (TODO)yunqing: This comparison wouldn't be necessary if the sample
    // selection is done in find_samples(). Also, global offset can be removed
    // while collecting samples.
    if (abs(sx - dx) < LS_MV_MAX && abs(sy - dy) < LS_MV_MAX) {
      A[0][0] += LS_SQUARE(sx);
      A[0][1] += LS_PRODUCT1(sx, sy);
      A[1][1] += LS_SQUARE(sy);
      Bx[0] += LS_PRODUCT2(sx, dx);
      Bx[1] += LS_PRODUCT1(sy, dx);
      By[0] += LS_PRODUCT1(sx, dy);
      By[1] += LS_PRODUCT2(sy, dy);
    }
  }

  // Just for debugging, and can be removed later.
  assert(A[0][0] >= LS_MAT_MIN && A[0][0] <= LS_MAT_MAX);
  assert(A[0][1] >= LS_MAT_MIN && A[0][1] <= LS_MAT_MAX);
  assert(A[1][1] >= LS_MAT_MIN && A[1][1] <= LS_MAT_MAX);
  assert(Bx[0] >= LS_MAT_MIN && Bx[0] <= LS_MAT_MAX);
  assert(Bx[1] >= LS_MAT_MIN && Bx[1] <= LS_MAT_MAX);
  assert(By[0] >= LS_MAT_MIN && By[0] <= LS_MAT_MAX);
  assert(By[1] >= LS_MAT_MIN && By[1] <= LS_MAT_MAX);

  // Compute Determinant of A
  const int64_t Det = (int64_t)A[0][0] * A[1][1] - (int64_t)A[0][1] * A[0][1];
  if (Det == 0) return 1;

  int16_t shift;
  int16_t iDet = resolve_divisor_64(llabs(Det), &shift) * (Det < 0 ? -1 : 1);
  shift -= WARPEDMODEL_PREC_BITS;
  if (shift < 0) {
    iDet <<= (-shift);
    shift = 0;
  }

  int64_t Px[2], Py[2];
  // These divided by the Det, are the least squares solutions
  Px[0] = (int64_t)A[1][1] * Bx[0] - (int64_t)A[0][1] * Bx[1];
  Px[1] = -(int64_t)A[0][1] * Bx[0] + (int64_t)A[0][0] * Bx[1];
  Py[0] = (int64_t)A[1][1] * By[0] - (int64_t)A[0][1] * By[1];
  Py[1] = -(int64_t)A[0][1] * By[0] + (int64_t)A[0][0] * By[1];

  wm->wmmat[2] = get_mult_shift_diag(Px[0], iDet, shift);
  wm->wmmat[3] = get_mult_shift_ndiag(Px[1], iDet, shift);
  wm->wmmat[4] = get_mult_shift_ndiag(Py[0], iDet, shift);
  wm->wmmat[5] = get_mult_shift_diag(Py[1], iDet, shift);

#if CONFIG_EXTENDED_WARP_PREDICTION
  av1_reduce_warp_model(wm);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
  // check compatibility with the fast warp filter
  if (!av1_get_shear_params(wm)) return 1;

  av1_set_warp_translation(mi_row, mi_col, bsize, mv, wm);
#if !CONFIG_EXTENDED_WARP_PREDICTION
  wm->wmmat[0] = clamp(wm->wmmat[0], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - 1);
  wm->wmmat[1] = clamp(wm->wmmat[1], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - 1);
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION

  wm->wmmat[6] = wm->wmmat[7] = 0;
  return 0;
}

int av1_find_projection(int np, const int *pts1, const int *pts2,
                        BLOCK_SIZE bsize, MV mv, WarpedMotionParams *wm_params,
                        int mi_row, int mi_col) {
  assert(wm_params->wmtype == AFFINE);

  if (find_affine_int(np, pts1, pts2, bsize, mv, wm_params, mi_row, mi_col))
    return 1;

  return 0;
}

#if CONFIG_EXTENDED_WARP_PREDICTION
/* Given a neighboring block's warp model and the motion vector at the center
   of the current block, construct a new warp model which is continuous with
   the neighbor at the common edge but which has the given motion vector at
   the center of the block.

    The `neighbor_is_above` parameter should be true if the neighboring block
    is above the current block, or false if it is to the left of the current
    block.

    Returns 0 if the resulting model can be used with the warp filter,
    1 if not.
*/
int av1_extend_warp_model(const bool neighbor_is_above, const BLOCK_SIZE bsize,
                          const MV *center_mv, const int mi_row,
                          const int mi_col,
                          const WarpedMotionParams *neighbor_wm,
                          WarpedMotionParams *wm_params) {
  const int half_width_log2 = mi_size_wide_log2[bsize] + MI_SIZE_LOG2 - 1;
  const int half_height_log2 = mi_size_high_log2[bsize] + MI_SIZE_LOG2 - 1;
  const int center_x = (mi_col * MI_SIZE) + (1 << half_width_log2) - 1;
  const int center_y = (mi_row * MI_SIZE) + (1 << half_height_log2) - 1;
  // Calculate the point (at warp model precision) where the center of the
  // current block should be mapped to
  int proj_center_x = (center_x * (1 << WARPEDMODEL_PREC_BITS)) +
                      (center_mv->col * (1 << (WARPEDMODEL_PREC_BITS - 3)));
  int proj_center_y = (center_y * (1 << WARPEDMODEL_PREC_BITS)) +
                      (center_mv->row * (1 << (WARPEDMODEL_PREC_BITS - 3)));

  *wm_params = default_warp_params;
  wm_params->wmtype = AFFINE;

  if (neighbor_is_above) {
    // We want to construct a model which will project the block center
    // according to the signaled motion vector, and which matches the
    // neighbor's warp model along the top edge of the block.
    //
    // We do this in three steps:
    // 1) Since the models should match along the whole top edge of the block,
    //    the coefficients of x in the warp model must be the same as for the
    //    neighboring block
    //
    // 2) The coefficients of y in the warp model can then be determined from
    //    the difference in projected positions between a point on the edge
    //    and the block center
    //
    // 3) The translational part can be derived (outside of this `if`)
    //    by subtracting the linear part of the model from the signaled MV.

    wm_params->wmmat[2] = neighbor_wm->wmmat[2];
    wm_params->wmmat[4] = neighbor_wm->wmmat[4];

    // Project above point
    int above_x = center_x;
    int above_y = center_y - (1 << half_height_log2);
    int proj_above_x = neighbor_wm->wmmat[2] * above_x +
                       neighbor_wm->wmmat[3] * above_y + neighbor_wm->wmmat[0];
    int proj_above_y = neighbor_wm->wmmat[4] * above_x +
                       neighbor_wm->wmmat[5] * above_y + neighbor_wm->wmmat[1];

    // y coefficients are (project(center) - project(above)) / (center.y -
    // above.y), which simplifies to (project(center) - project(above)) /
    // 2^(half_height_log2)
    wm_params->wmmat[3] =
        ROUND_POWER_OF_TWO(proj_center_x - proj_above_x, half_height_log2);
    wm_params->wmmat[5] =
        ROUND_POWER_OF_TWO(proj_center_y - proj_above_y, half_height_log2);
  } else {
    // If the neighboring block is to the left of the current block, we do the
    // same thing as for the above case, but with x and y axes interchanged

    wm_params->wmmat[3] = neighbor_wm->wmmat[3];
    wm_params->wmmat[5] = neighbor_wm->wmmat[5];

    // Project left point
    int left_x = center_x - (1 << half_width_log2);
    int left_y = center_y;
    int proj_left_x = neighbor_wm->wmmat[2] * left_x +
                      neighbor_wm->wmmat[3] * left_y + neighbor_wm->wmmat[0];
    int proj_left_y = neighbor_wm->wmmat[4] * left_x +
                      neighbor_wm->wmmat[5] * left_y + neighbor_wm->wmmat[1];

    // y coefficients are
    //    (project(center) - project(left)) / (center.y - left.y)
    // which simplifies to
    //    (project(center) - project(left)) / 2^(half_width_log2)
    wm_params->wmmat[2] =
        ROUND_POWER_OF_TWO(proj_center_x - proj_left_x, half_width_log2);
    wm_params->wmmat[4] =
        ROUND_POWER_OF_TWO(proj_center_y - proj_left_y, half_width_log2);
  }

  av1_reduce_warp_model(wm_params);
  // check compatibility with the fast warp filter
  if (!av1_get_shear_params(wm_params)) return 1;

  // Derive translational part from signaled MV
  av1_set_warp_translation(mi_row, mi_col, bsize, *center_mv, wm_params);

  return 0;
}
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_TEMPORAL_GLOBAL_MV
static int32_t get_mult_shift_ndiag_u(int64_t v, int shift) {
  return (int32_t)clamp64(ROUND_POWER_OF_TWO_SIGNED_64(v, shift),
                          -WARPEDMODEL_NONDIAGAFFINE_CLAMP + 1,
                          WARPEDMODEL_NONDIAGAFFINE_CLAMP - 1);
}

static int32_t get_mult_shift_diag_u(int64_t v, int shift) {
  return (int32_t)clamp64(
      ROUND_POWER_OF_TWO_SIGNED_64(v, shift),
      (1 << WARPEDMODEL_PREC_BITS) - WARPEDMODEL_NONDIAGAFFINE_CLAMP + 1,
      (1 << WARPEDMODEL_PREC_BITS) + WARPEDMODEL_NONDIAGAFFINE_CLAMP - 1);
}

/*
static int32_t get_mult_shift_dx_uhp(int64_t *v, int shift, const int32_t *u) {
  int64_t off = ((int64_t)u[0] << (WARPEDMODEL_PREC_BITS + shift)) -
                 (int64_t)u[0] * v[0] - (int64_t)u[1] * v[1];
  return (int32_t)clamp64(ROUND_POWER_OF_TWO_SIGNED_64(v[2] + off, 3 + shift),
                          -WARPEDMODEL_TRANS_CLAMP + 1,
                          WARPEDMODEL_TRANS_CLAMP - 1);
}

static int32_t get_mult_shift_dy_uhp(int64_t *v, int shift, const int32_t *u) {
  int64_t off = ((int64_t)u[1] << (WARPEDMODEL_PREC_BITS + shift)) -
                 (int64_t)u[0] * v[0] - (int64_t)u[1] * v[1];
  return (int32_t)clamp64(ROUND_POWER_OF_TWO_SIGNED_64(v[2] + off, 3 + shift),
                          -WARPEDMODEL_TRANS_CLAMP + 1,
                          WARPEDMODEL_TRANS_CLAMP - 1);
}
*/

static int32_t get_mult_shift_dx_u(int64_t v, int shift, const int32_t *u,
                                   int32_t *w) {
  int32_t vs = (int32_t)(ROUND_POWER_OF_TWO_SIGNED_64(v, shift));
  int32_t off = (u[0] << WARPEDMODEL_PREC_BITS) - u[0] * w[0] - u[1] * w[1];

  return (int32_t)clamp64(ROUND_POWER_OF_TWO_SIGNED(vs + off, 3),
                          -WARPEDMODEL_TRANS_CLAMP + 1,
                          WARPEDMODEL_TRANS_CLAMP - 1);
}

static int32_t get_mult_shift_dy_u(int64_t v, int shift, const int32_t *u,
                                   int32_t *w) {
  int32_t vs = (int32_t)(ROUND_POWER_OF_TWO_SIGNED_64(v, shift));
  int32_t off = (u[1] << WARPEDMODEL_PREC_BITS) - u[0] * w[0] - u[1] * w[1];

  return (int32_t)clamp64(ROUND_POWER_OF_TWO_SIGNED(vs + off, 3),
                          -WARPEDMODEL_TRANS_CLAMP + 1,
                          WARPEDMODEL_TRANS_CLAMP - 1);
}

#define LS_MAT_DOWN_BITS_U 0
#define LS_SQUARE_U(a)                                        \
  (((a) * (a)*4 + (a)*4 * LS_STEP + LS_STEP * LS_STEP * 2) >> \
   (2 + LS_MAT_DOWN_BITS_U))
#define LS_PRODUCT1_U(a, b)                                         \
  (((a) * (b)*4 + ((a) + (b)) * 2 * LS_STEP + LS_STEP * LS_STEP) >> \
   (2 + LS_MAT_DOWN_BITS_U))
#define LS_PRODUCT2_U(a, b)                                             \
  (((a) * (b)*4 + ((a) + (b)) * 2 * LS_STEP + LS_STEP * LS_STEP * 2) >> \
   (2 + LS_MAT_DOWN_BITS_U))
#define LS_SUM_U(a) (((a)*4 + LS_STEP * 2) >> (2 + LS_MAT_DOWN_BITS_U))

static int find_affine_unconstrained_int(int np, const int *pts1,
                                         const int *pts2,
                                         WarpedMotionParams *wm, int mi_row,
                                         int mi_col) {
  int32_t A[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
  int32_t Bx[3] = { 0, 0, 0 };
  int32_t By[3] = { 0, 0, 0 };
  int i, n = 0;

  int64_t C00, C01, C02, C11, C12, C22;
  int64_t Px[3], Py[3];
  int64_t Det;

  // Offsets to make the values in the arrays smaller
  const int32_t u[2] = { mi_col * MI_SIZE * 8, mi_row * MI_SIZE * 8 };

  // Let source points (xi, yi) map to destimation points (xi', yi'),
  //     for i = 0, 1, 2, .... n-1
  // Then if  P = [x0, y0, 1,
  //               x1, y1, 1
  //               x2, y2, 1,
  //                ....
  //              ]
  //          q = [x0', x1', x2', ... ]'
  //          r = [y0', y1', y2', ... ]'
  // the least squares problems that need to be solved are:
  //          [h1, h2, dx]' = inv(P'P)P'q and
  //          [h3, h4, dy]' = inv(P'P)P'r
  // where the affine transformation is given by:
  //          x' = h1.x + h2.y + dx
  //          y' = h3.x + h4.y + dy
  //
  // The loop below computes: A = P'P, Bx = P'q, By = P'r
  // We need to just compute inv(A).Bx and inv(A).By for the solutions.
  //
  int sx, sy, dx, dy;

  // Contribution from samples
  for (i = 0; i < np; i++) {
    dx = pts2[i * 2] - u[0];
    dy = pts2[i * 2 + 1] - u[1];
    sx = pts1[i * 2] - u[0];
    sy = pts1[i * 2 + 1] - u[1];
    if (abs(sx - dx) < LS_MV_MAX && abs(sy - dy) < LS_MV_MAX) {
      A[0][0] += LS_SQUARE_U(sx);
      A[0][1] += LS_PRODUCT1_U(sx, sy);
      A[0][2] += LS_SUM_U(sx);
      A[1][1] += LS_SQUARE_U(sy);
      A[1][2] += LS_SUM_U(sy);
      A[2][2] += 1;
      Bx[0] += LS_PRODUCT2_U(sx, dx);
      Bx[1] += LS_PRODUCT1_U(sy, dx);
      Bx[2] += LS_SUM_U(dx);
      By[0] += LS_PRODUCT1_U(sx, dy);
      By[1] += LS_PRODUCT2_U(sy, dy);
      By[2] += LS_SUM_U(dy);
      n++;
    }
  }
  // Compute Cofactors of A
  C00 = (int64_t)A[1][1] * A[2][2] - (int64_t)A[1][2] * A[1][2];
  C01 = (int64_t)A[1][2] * A[0][2] - (int64_t)A[0][1] * A[2][2];
  C02 = (int64_t)A[0][1] * A[1][2] - (int64_t)A[0][2] * A[1][1];
  C11 = (int64_t)A[0][0] * A[2][2] - (int64_t)A[0][2] * A[0][2];
  C12 = (int64_t)A[0][1] * A[0][2] - (int64_t)A[0][0] * A[1][2];
  C22 = (int64_t)A[0][0] * A[1][1] - (int64_t)A[0][1] * A[0][1];

  // Scale by 1/2^6
  C00 = ROUND_POWER_OF_TWO_SIGNED_64(C00, 6);
  C01 = ROUND_POWER_OF_TWO_SIGNED_64(C01, 6);
  C02 = ROUND_POWER_OF_TWO_SIGNED_64(C02, 6);
  C11 = ROUND_POWER_OF_TWO_SIGNED_64(C11, 6);
  C12 = ROUND_POWER_OF_TWO_SIGNED_64(C12, 6);
  C22 = ROUND_POWER_OF_TWO_SIGNED_64(C22, 6);

  // Compute Determinant of A
  Det = C00 * A[0][0] + C01 * A[0][1] + C02 * A[0][2];
  if (Det == 0) return 1;

  // These divided by the Det, are the least squares solutions
  Px[0] = C00 * Bx[0] + C01 * Bx[1] + C02 * Bx[2];
  Px[1] = C01 * Bx[0] + C11 * Bx[1] + C12 * Bx[2];
  Px[2] = C02 * Bx[0] + C12 * Bx[1] + C22 * Bx[2];
  Py[0] = C00 * By[0] + C01 * By[1] + C02 * By[2];
  Py[1] = C01 * By[0] + C11 * By[1] + C12 * By[2];
  Py[2] = C02 * By[0] + C12 * By[1] + C22 * By[2];

  int16_t shift;
  int16_t iDet = resolve_divisor_64(labs(Det), &shift) * (Det < 0 ? -1 : 1);
  shift -= WARPEDMODEL_PREC_BITS;
  if (shift < 0) {
    iDet <<= (-shift);
    shift = 0;
  }

  Px[0] *= iDet;
  Px[1] *= iDet;
  Px[2] *= iDet;
  Py[0] *= iDet;
  Py[1] *= iDet;
  Py[2] *= iDet;

  wm->wmmat[2] = get_mult_shift_diag_u(Px[0], shift);
  wm->wmmat[3] = get_mult_shift_ndiag_u(Px[1], shift);
  wm->wmmat[4] = get_mult_shift_ndiag_u(Py[0], shift);
  wm->wmmat[5] = get_mult_shift_diag_u(Py[1], shift);

  // Adjust x displacement for the offset
  wm->wmmat[0] = get_mult_shift_dx_u(Px[2], shift, u, &wm->wmmat[2]);
  // Adjust y displacement for the offset
  wm->wmmat[1] = get_mult_shift_dy_u(Py[2], shift, u, &wm->wmmat[4]);

#if CONFIG_EXTENDED_WARP_PREDICTION
  av1_reduce_warp_model(wm);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

  wm->wmmat[6] = wm->wmmat[7] = 0;
  wm->wmtype = AFFINE;
  return 0;
}

int av1_find_projection_unconstrained(int np, const int *pts1, const int *pts2,
                                      WarpedMotionParams *wm_params, int mi_row,
                                      int mi_col) {
  if (find_affine_unconstrained_int(np, pts1, pts2, wm_params, mi_row, mi_col))
    return 1;

  // check compatibility with the fast warp filter
  if (!av1_get_shear_params(wm_params)) return 1;

  return 0;
}
#endif  // CONFIG_TEMPORAL_GLOBAL_MV

#if CONFIG_INTERINTRA_WARP
// Note ext1 = 1 corresponds to the case when 2 prior samples are available
// in the beginning of the sequence and so no padding is needed.
// ext1 = 0 corresponds to the case when only 1 prior sample is available
// in the beginning of the sequence.
// Likewise ext2 = 1 corresponds to the case when 2 external samples are
// available at the end of the sequence and so no padding is needed at the end.
// ext2 = 0 corresponds to the case when only 1 xeternal sample is available
// at the end of the sequence.
static int get_gradient(const uint16_t *x, int stride, int ext1, int ext2) {
  static const int16_t gradient[2] = { 88, -12 };
  int g;
  if (ext1 && ext2) {
    g = (x[stride] - x[-stride]) * gradient[0] +
        (x[2 * stride] - x[-2 * stride]) * gradient[1];
  } else if (!ext1 && ext2) {
    g = (x[stride] - x[-stride]) * gradient[0] +
        (x[2 * stride] - 2 * x[-1 * stride] + x[0]) * gradient[1];
  } else if (ext1 && !ext2) {
    g = (x[stride] - x[-stride]) * gradient[0] +
        (2 * x[1 * stride] - x[0] - x[-2 * stride]) * gradient[1];
  } else {
    g = (x[stride] - x[-stride]) * gradient[0] +
        (2 * x[1 * stride] - 2 * x[-1 * stride]) * gradient[1];
  }
  g = ROUND_POWER_OF_TWO_SIGNED(g, FILTER_BITS);
  return g;
}

/* src - pointer to current block, but only the pixels of top and left in
 * the causal intra region are expected to be used
 * ref - pointer to reference block obtained by floor(mv) interger offset
 * in reference buffer. A 4-pixel border is expected to be available for
 * use around this reference block.
 * mv - motion vector
 */
#define GRAD_THRESH 1
static int find_interintra_rotzoom_int(const uint16_t *src, int src_stride,
                                       const uint16_t *ref, int ref_stride,
                                       BLOCK_SIZE bsize, MV mv,
                                       WarpedMotionParams *wm, int mi_row,
                                       int mi_col, int bd) {
  const int border = MAX_INTERINTRA_BORDER;
  const int inner_border = MAX_INTERINTRA_INNER_BORDER;
  const int overhang = MAX_INTERINTRA_OVERHANG;

  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  uint16_t top[MAX_INTERINTRA_TOPLEFT_SIZE];
  uint16_t left[MAX_INTERINTRA_TOPLEFT_SIZE];
  const int top_stride = bw + 2 * overhang;
  const int left_stride = border + inner_border;
  av1_prepare_inter_topleft(ref, ref_stride, bsize, border, inner_border,
                            overhang, mv, top, top_stride, left, left_stride,
                            bd);
  const int off = 1;
  int32_t A[2][2] = { { 0, 0 }, { 0, 0 } };
  int32_t B[2] = { 0, 0 };
  for (int i = 1; i < border; ++i) {
    for (int j = off; j < bw - off; ++j) {
      const int d = (*(top + i * top_stride + j + overhang) -
                     *(src + (i - border) * src_stride + j));
      const int y = (i - border) - (bh / 2 - 1);
      const int x = j - (bw / 2 - 1);
      const int gx = get_gradient(top + i * top_stride + j + overhang, 1,
                                  j > 1 - overhang, j < bw - 2 + overhang);
      const int gy = get_gradient(top + i * top_stride + j + overhang,
                                  top_stride, i > 1, 1);
#if GRAD_THRESH
      if (abs(gx) < GRAD_THRESH && abs(gy) < GRAD_THRESH) continue;
#endif  // GRAD_THRESH
      const int p1 = x * gx + y * gy;
      const int p2 = y * gx - x * gy;
      A[0][0] += p1 * p1;
      A[1][1] += p2 * p2;
      A[0][1] += p1 * p2;
      B[0] += p1 * d;
      B[1] += p2 * d;
    }
  }
  for (int i = off; i < bh - off; ++i) {
    for (int j = 1; j < border; ++j) {
      const int d = (*(left + (i + overhang) * left_stride + j) -
                     *(src + i * src_stride + j - border));
      const int y = i - (bh / 2 - 1);
      const int x = (j - border) - (bw / 2 - 1);
      const int gx =
          get_gradient(left + (i + overhang) * left_stride + j, 1, j > 1, 1);
      const int gy =
          get_gradient(left + (i + overhang) * left_stride + j, left_stride,
                       i > 1 - overhang, i < bh - 2 + overhang);
#if GRAD_THRESH
      if (abs(gx) < GRAD_THRESH && abs(gy) < GRAD_THRESH) continue;
#endif  // GRAD_THRESH
      const int p1 = x * gx + y * gy;
      const int p2 = y * gx - x * gy;
      A[0][0] += p1 * p1;
      A[1][1] += p2 * p2;
      A[0][1] += p1 * p2;
      B[0] += p1 * d;
      B[1] += p2 * d;
    }
  }
  A[1][0] = A[0][1];

  // Compute Determinant of A
  const int64_t Det = (int64_t)A[0][0] * A[1][1] - (int64_t)A[0][1] * A[0][1];
  if (Det == 0) return 1;

  int16_t shift;
  int16_t iDet = resolve_divisor_64(llabs(Det), &shift) * (Det < 0 ? -1 : 1);
  shift -= WARPEDMODEL_PREC_BITS;
  if (shift < 0) {
    iDet <<= (-shift);
    shift = 0;
  }

  int64_t P[2];
  // These divided by the Det, are the least squares solutions
  P[0] = (int64_t)A[1][1] * B[0] - (int64_t)A[0][1] * B[1];
  P[1] = -(int64_t)A[0][1] * B[0] + (int64_t)A[0][0] * B[1];

  wm->wmmat[2] =
      get_mult_shift_diag(P[0], iDet, shift) + (1 << WARPEDMODEL_PREC_BITS);
  wm->wmmat[3] = get_mult_shift_ndiag(P[1], iDet, shift);
  wm->wmmat[4] = -wm->wmmat[3];
  wm->wmmat[5] = wm->wmmat[2];

#if CONFIG_EXTENDED_WARP_PREDICTION
  av1_reduce_warp_model(wm);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
  // check compatibility with the fast warp filter
  if (!av1_get_shear_params(wm)) return 1;
  av1_set_warp_translation(mi_row, mi_col, bsize, mv, wm);
#if !CONFIG_EXTENDED_WARP_PREDICTION
  wm->wmmat[0] = clamp(wm->wmmat[0], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - 1);
  wm->wmmat[1] = clamp(wm->wmmat[1], -WARPEDMODEL_TRANS_CLAMP,
                       WARPEDMODEL_TRANS_CLAMP - 1);
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION
  wm->wmtype = ROTZOOM;

  wm->wmmat[6] = wm->wmmat[7] = 0;
  return 0;
}

int av1_find_projection_interintra(const MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                   const uint16_t *dst, int dst_stride, MV mv,
                                   WarpedMotionParams *wm_params) {
  const struct macroblockd_plane *pd = &xd->plane[0];
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  const int ref_stride = pd->pre[0].stride;
  uint16_t *const ref = pd->pre[0].buf +
                        (mv.row >> (SUBPEL_BITS - 1)) * ref_stride +
                        (mv.col >> (SUBPEL_BITS - 1));
  if (find_interintra_rotzoom_int(dst, dst_stride, ref, ref_stride, bsize, mv,
                                  wm_params, mi_row, mi_col, xd->bd))
    return 1;

  // check compatibility with the fast warp filter
  if (!av1_get_shear_params(wm_params)) return 1;

  return 0;
}

int av1_find_projection_interintra_ext(const MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                       const uint16_t *dst, int dst_stride,
                                       MV mv, WarpedMotionParams *wm_params,
                                       int width, int height,
                                       uint16_t *tmpbuf) {
  const struct macroblockd_plane *pd = &xd->plane[0];
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  const int border = MAX_INTERINTRA_BORDER;
  int x0 = mi_col * MI_SIZE + (mv.col >> (SUBPEL_BITS - 1));
  int x1 = x0 + block_size_wide[bsize];
  int y0 = mi_row * MI_SIZE + (mv.row >> (SUBPEL_BITS - 1));
  int y1 = y0 + block_size_high[bsize];
  x0 -= border;
  y0 -= border;
  x1 += border;
  y1 += border;
  const int ref_stride = pd->pre[0].stride;

  if (x0 < 0 || y0 < 0 || x1 > width || y1 > height) {
    // padding needed
    const int b_w = x1 - x0;
    const int b_h = y1 - y0;
    const int buf_stride = b_w;

    uint16_t *const ref = pd->pre[0].buf0 + y0 * ref_stride + x0;
    highbd_build_mc_border(ref, ref_stride, tmpbuf, buf_stride, x0, y0, b_w,
                           b_h, width, height);

    uint16_t *const buf = tmpbuf + border * buf_stride + border;

    if (find_interintra_rotzoom_int(dst, dst_stride, buf, buf_stride, bsize, mv,
                                    wm_params, mi_row, mi_col, xd->bd))
      return 1;
  } else {
    // padding not needed
    uint16_t *const ref = pd->pre[0].buf +
                          (mv.row >> (SUBPEL_BITS - 1)) * ref_stride +
                          (mv.col >> (SUBPEL_BITS - 1));
    if (find_interintra_rotzoom_int(dst, dst_stride, ref, ref_stride, bsize, mv,
                                    wm_params, mi_row, mi_col, xd->bd))
      return 1;
  }

  // check compatibility with the fast warp filter
  if (!av1_get_shear_params(wm_params)) return 1;

  return 0;
}
#endif  // CONFIG_INTERINTRA_WARP
