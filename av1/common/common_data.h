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

#ifndef AOM_AV1_COMMON_COMMON_DATA_H_
#define AOM_AV1_COMMON_COMMON_DATA_H_

#include <assert.h>
#include <stdbool.h>

#include "av1/common/enums.h"
#include "aom/aom_integer.h"
#include "aom_dsp/aom_dsp_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Log 2 conversion lookup tables in units of mode info (4x4).
// The Mi_Width_Log2 table in the spec (Section 9.3. Conversion tables).
static const uint8_t mi_size_wide_log2[BLOCK_SIZES_ALL] = {
  0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5,
  5, 6, 6, 0, 2, 1, 3, 2, 4, 0, 3, 1, 4, 0, 4,
};
// The Mi_Height_Log2 table in the spec (Section 9.3. Conversion tables).
static const uint8_t mi_size_high_log2[BLOCK_SIZES_ALL] = {
  0, 1, 0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 5, 4, 5,
  6, 5, 6, 2, 0, 3, 1, 4, 2, 3, 0, 4, 1, 4, 0,
};

// Width/height lookup tables in units of mode info (4x4).
// The Num_4x4_Blocks_Wide table in the spec (Section 9.3. Conversion tables).
static const uint8_t mi_size_wide[BLOCK_SIZES_ALL] = {
  1,  1,  2,  2, 2, 4, 4, 4, 8,  8, 8, 16, 16, 16, 32, 32,
  32, 64, 64, 1, 4, 2, 8, 4, 16, 1, 8, 2,  16, 1,  16,
};

// The Num_4x4_Blocks_High table in the spec (Section 9.3. Conversion tables).
static const uint8_t mi_size_high[BLOCK_SIZES_ALL] = {
  1,  2,  1,  2, 4, 2, 4, 8,  4, 8, 16, 8,  16, 32, 16, 32,
  64, 32, 64, 4, 1, 8, 2, 16, 4, 8, 1,  16, 2,  16, 1,
};

// Width/height lookup tables in units of samples.
// The Block_Width table in the spec (Section 9.3. Conversion tables).
static const uint16_t block_size_wide[BLOCK_SIZES_ALL] = {
  4,   4,   8,   8, 8,  16, 16, 16, 32, 32, 32, 64, 64, 64, 128, 128,
  128, 256, 256, 4, 16, 8,  32, 16, 64, 4,  32, 8,  64, 4,  64,
};

// The Block_Height table in the spec (Section 9.3. Conversion tables).
static const uint16_t block_size_high[BLOCK_SIZES_ALL] = {
  4,   8,   4,   8,  16, 8,  16, 32, 16, 32, 64, 32, 64, 128, 64, 128,
  256, 128, 256, 16, 4,  32, 8,  64, 16, 32, 4,  64, 8,  64,  4,
};

// Maps a block size to a context.
// The Size_Group table in the spec (Section 9.3. Conversion tables).
// AOMMIN(3, AOMMIN(mi_size_wide_log2(bsize), mi_size_high_log2(bsize)))
static const uint8_t size_group_lookup[BLOCK_SIZES_ALL] = {
  0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 2,
};

// Maps a block size to a transform partition context.
// 1) 0: for block sizes do not allow 4way partition
// 2) For 8x8 <= square block size < 64x64, assuming i mapping value is a used
//    for NxN square block size, then (i + 1) is the mapping value of (N)x(2N)
//    and (2N)x(N) sizes.
// 3) For block size >= 64x64, the mapping value is 7
// 4) For 1:4/4:1 and 1:8/8:1 block sizes, mapping value is 8

static const uint8_t size_to_tx_part_group_lookup[BLOCK_SIZES_ALL] = {
  0, 0, 0, 1, 2, 2, 3, 4, 4, 5, 6, 6, 7, 7, 7, 7,
  7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
/*
Maps a block size to a transform partition context.
size_to_tx_type_group_vert_and_horz_lookup - 1-2
size_to_tx_type_group_vert_or_horz_lookup - 0-13
size_to_tx_type_group_vert_and_horz_lookup - used when block can support
both vertical and horizontal TX partitions.
size_to_tx_type_group_vert_or_horz_lookup - used when block can support
either vertical or horizontal TX partitions.
0 in size_to_tx_type_group_vert_or_horz_lookup is case when there will not
be any TX partitions (TX_PARTITION_NONE)
*/
static const uint8_t
    size_to_tx_type_group_vert_and_horz_lookup[BLOCK_SIZES_ALL] = {
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      0,
      1,
      2,
      3,
      4,
      5,
      6,
      7,
      8,
      9,
      9,
      9,
      9,
      9,
      9,
      9,
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      10,
      11,
      12,
      13,
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      10,
      11,
      BLOCK_INVALID,  // unused
      BLOCK_INVALID   // unused
    };

static const uint8_t
    size_to_tx_type_group_vert_or_horz_lookup[BLOCK_SIZES_ALL] = {
      0,
      0,
      0,
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      1,
      2,
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      1,
      2,
      BLOCK_INVALID,  // unused
      BLOCK_INVALID,  // unused
      1,
      2
    };
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING

static const uint8_t size_to_tx_type_group_lookup[BLOCK_SIZES_ALL] = {
  0,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 10, 10, 10,
  10, 10, 10, 11, 12, 13, 14, 15, 16, 11, 12, 13, 14, 11, 12
};

static const uint8_t fsc_bsize_groups[BLOCK_SIZES_ALL] = {
  0, 1, 1, 2, 3, 3, 4, 5, 5, 5, 6, 6, 6, 6, 6, 6,
  6, 6, 6, 3, 3, 4, 4, 6, 6, 4, 4, 6, 6, 6, 6
};

static const uint8_t num_pels_log2_lookup[BLOCK_SIZES_ALL] = {
  4,  5,  5,  6, 7, 7, 8, 9,  9,  10, 11, 11, 12, 13, 13, 14,
  15, 15, 16, 6, 6, 8, 8, 10, 10, 7,  7,  9,  9,  8,  8,
};

// Supported weighting factor for compound weighted prediction
static const int8_t cwp_weighting_factor[2][MAX_CWP_NUM] = {
  { 8, 12, 4, 10, 6 },
  { 8, 12, 4, 20, -4 },
};

#if CONFIG_TIP_ENHANCEMENT
// Supported weighting factor for TIP
static const int8_t tip_weighting_factors[MAX_TIP_WTD_NUM] = { 8,  12, 16, 18,
                                                               20, 4,  6,  -4 };
#endif  // CONFIG_TIP_ENHANCEMENT

#if CONFIG_VQ_MVD_CODING
// Supported set of MVD values in unit of 1/8 pel for AMVD mode
static const int16_t amvd_index_to_mvd[MAX_AMVD_INDEX + 1] = { 0,  2,  4,
                                                               6,  8,  16,
                                                               32, 64, 128 };
#endif  // CONFIG_VQ_MVD_CODING

/* clang-format off */
// This table covers all block sizes.
static const BLOCK_SIZE
    subsize_lookup[ALL_PARTITION_TYPES][BLOCK_SIZES_ALL] = { {
    // PARTITION_NONE
    BLOCK_4X4,                                   // 4
    BLOCK_4X8,     BLOCK_8X4,     BLOCK_8X8,     // 8
    BLOCK_8X16,    BLOCK_16X8,    BLOCK_16X16,   // 16
    BLOCK_16X32,   BLOCK_32X16,   BLOCK_32X32,   // 32
    BLOCK_32X64,   BLOCK_64X32,   BLOCK_64X64,   // 64
    BLOCK_64X128,  BLOCK_128X64,  BLOCK_128X128, // 128
    BLOCK_128X256, BLOCK_256X128, BLOCK_256X256, // 256
    BLOCK_4X16,    BLOCK_16X4,                   // 4,16
    BLOCK_8X32,    BLOCK_32X8,                   // 8,32
    BLOCK_16X64,   BLOCK_64X16,                  // 16,64
    BLOCK_4X32,    BLOCK_32X4,                   // 4, 32
    BLOCK_8X64,    BLOCK_64X8,                   // 8, 64
    BLOCK_4X64,    BLOCK_64X4,                   // 4, 64
  }, {
    // PARTITION_HORZ
    BLOCK_INVALID,                               // 4
    BLOCK_4X4,     BLOCK_INVALID, BLOCK_8X4,     // 8
    BLOCK_8X8,     BLOCK_16X4,    BLOCK_16X8,    // 16
    BLOCK_16X16,   BLOCK_32X8,    BLOCK_32X16,   // 32
    BLOCK_32X32,   BLOCK_64X16,   BLOCK_64X32,   // 64
    BLOCK_64X64,   BLOCK_INVALID, BLOCK_128X64,  // 128
    BLOCK_128X128, BLOCK_INVALID, BLOCK_256X128, // 256
    BLOCK_4X8,     BLOCK_INVALID,                // 4,16
    BLOCK_8X16,    BLOCK_32X4,                   // 8,32
    BLOCK_16X32,   BLOCK_64X8,                   // 16,64
    BLOCK_4X16,    BLOCK_INVALID,                // 4, 32
    BLOCK_8X32,    BLOCK_64X4,                   // 8, 64
    BLOCK_4X32,    BLOCK_INVALID,                // 4, 64
  }, {
    // PARTITION_VERT
    BLOCK_INVALID,                               // 4
    BLOCK_INVALID, BLOCK_4X4,     BLOCK_4X8,     // 8
    BLOCK_4X16,    BLOCK_8X8,     BLOCK_8X16,    // 16
    BLOCK_8X32,    BLOCK_16X16,   BLOCK_16X32,   // 32
    BLOCK_16X64,   BLOCK_32X32,   BLOCK_32X64,   // 64
    BLOCK_INVALID, BLOCK_64X64,   BLOCK_64X128,  // 128
    BLOCK_INVALID, BLOCK_128X128, BLOCK_128X256, // 256
    BLOCK_INVALID, BLOCK_8X4,                    // 4,16
    BLOCK_4X32,    BLOCK_16X8,                   // 8,32
    BLOCK_8X64,    BLOCK_32X16,                  // 16,64
    BLOCK_INVALID, BLOCK_16X4,                   // 4, 32
    BLOCK_4X64,    BLOCK_32X8,                   // 8, 64
    BLOCK_INVALID, BLOCK_32X4,                   // 4, 64
  }, {
    // PARTITION_HORZ_3
    BLOCK_INVALID,                               // 4
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 8
    BLOCK_8X4,     BLOCK_INVALID, BLOCK_16X4,    // 16
    BLOCK_16X8,    BLOCK_32X4, BLOCK_32X8,       // 32
    BLOCK_32X16,   BLOCK_64X8, BLOCK_64X16,      // 64
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 128
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 256
    BLOCK_INVALID, BLOCK_INVALID,                // 4,16
    BLOCK_8X8,     BLOCK_INVALID,                // 8,32
    BLOCK_16X16,   BLOCK_64X4,                   // 16,64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 32
    BLOCK_INVALID, BLOCK_INVALID,                // 8, 64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 64
  }, {
    // PARTITION_VERT_3
    BLOCK_INVALID,                               // 4
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 8
    BLOCK_INVALID, BLOCK_4X8,     BLOCK_4X16,    // 16
    BLOCK_4X32,    BLOCK_8X16,    BLOCK_8X32,    // 32
    BLOCK_8X64,    BLOCK_16X32,   BLOCK_16X64,   // 64
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 128
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 256
    BLOCK_INVALID, BLOCK_INVALID,                // 4,16
    BLOCK_INVALID, BLOCK_8X8,                    // 8,32
    BLOCK_4X64,    BLOCK_16X16,                  // 16,64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 32
    BLOCK_INVALID, BLOCK_INVALID,                // 8, 64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 64
  }, {  // PARTITION_HORZ_4A
    BLOCK_INVALID,                               // 4
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 8
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 16
    BLOCK_16X4,    BLOCK_INVALID, BLOCK_32X4,    // 32
    BLOCK_32X8,    BLOCK_64X4,    BLOCK_64X8,    // 64
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 128
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 256
    BLOCK_INVALID, BLOCK_INVALID,                // 4,16
    BLOCK_8X4,     BLOCK_INVALID,                // 8,32
    BLOCK_16X8,    BLOCK_INVALID,                // 16,64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 32
    BLOCK_INVALID, BLOCK_INVALID,                // 8, 64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 64
  }, {  // PARTITION_HORZ_4B
    BLOCK_INVALID,                               // 4
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 8
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 16
    BLOCK_16X4,    BLOCK_INVALID, BLOCK_32X4,    // 32
    BLOCK_32X8,    BLOCK_64X4,    BLOCK_64X8,    // 64
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 128
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 256
    BLOCK_INVALID, BLOCK_INVALID,                // 4,16
    BLOCK_8X4,     BLOCK_INVALID,                // 8,32
    BLOCK_16X8,    BLOCK_INVALID,                // 16,64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 32
    BLOCK_INVALID, BLOCK_INVALID,                // 8, 64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 64
  }, {  // PARTITION_VERT_4A
    BLOCK_INVALID,                               // 4
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 8
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 16
    BLOCK_INVALID, BLOCK_4X16,    BLOCK_4X32,    // 32
    BLOCK_4X64,    BLOCK_8X32,    BLOCK_8X64,    // 64
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 128
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 256
    BLOCK_INVALID, BLOCK_INVALID,                // 4,16
    BLOCK_INVALID, BLOCK_4X8,                    // 8,32
    BLOCK_INVALID, BLOCK_8X16,                   // 16,64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 32
    BLOCK_INVALID, BLOCK_INVALID,                // 8, 64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 64
  }, {  // PARTITION_VERT_4B
     BLOCK_INVALID,                               // 4
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 8
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 16
    BLOCK_INVALID, BLOCK_4X16,    BLOCK_4X32,    // 32
    BLOCK_4X64,    BLOCK_8X32,    BLOCK_8X64,    // 64
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 128
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, // 256
    BLOCK_INVALID, BLOCK_INVALID,                // 4,16
    BLOCK_INVALID, BLOCK_4X8,                    // 8,32
    BLOCK_INVALID, BLOCK_8X16,                   // 16,64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 32
    BLOCK_INVALID, BLOCK_INVALID,                // 8, 64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 64
  }, {
    // PARTITION_SPLIT
    BLOCK_INVALID,                               // 4
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_4X4,     // 8
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_8X8,     // 16
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_16X16,   // 32
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_32X32,   // 64
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_64X64,   // 128
    BLOCK_INVALID, BLOCK_INVALID, BLOCK_128X128, // 256
    BLOCK_INVALID, BLOCK_INVALID,                // 4,16
    BLOCK_INVALID, BLOCK_INVALID,                // 8,32
    BLOCK_INVALID, BLOCK_INVALID,                // 16,64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 32
    BLOCK_INVALID, BLOCK_INVALID,                // 8, 64
    BLOCK_INVALID, BLOCK_INVALID,                // 4, 64
  },
};
/* clang-format on */

static AOM_INLINE PARTITION_TYPE sdp_chroma_part_from_luma(
    BLOCK_SIZE bsize, PARTITION_TYPE luma_part, int ssx, int ssy) {
  const int bh_chr = block_size_high[bsize] >> ssy;
  const int bw_chr = block_size_wide[bsize] >> ssx;
  assert(bh_chr >= 16 && bw_chr >= 16 &&
         "Current implementation cannot handle SDP for sub 16x16 blocks!");

  switch (luma_part) {
    case PARTITION_NONE: return PARTITION_NONE;
    case PARTITION_HORZ: return (bh_chr < 8) ? PARTITION_NONE : PARTITION_HORZ;
    case PARTITION_VERT: return (bw_chr < 8) ? PARTITION_NONE : PARTITION_VERT;
    case PARTITION_HORZ_4A:
      if (bh_chr >= 32) {
        return PARTITION_HORZ_4A;
      } else if (bh_chr >= 8) {
        return PARTITION_HORZ;
      } else {
        return PARTITION_NONE;
      }
    case PARTITION_HORZ_4B:
      if (bh_chr >= 32) {
        return PARTITION_HORZ_4B;
      } else if (bh_chr >= 8) {
        return PARTITION_HORZ;
      } else {
        return PARTITION_NONE;
      }
    case PARTITION_VERT_4A:
      if (bw_chr >= 32) {
        return PARTITION_VERT_4A;
      } else if (bw_chr >= 8) {
        return PARTITION_VERT;
      } else {
        return PARTITION_NONE;
      }
    case PARTITION_VERT_4B:
      if (bw_chr >= 32) {
        return PARTITION_VERT_4B;
      } else if (bw_chr >= 8) {
        return PARTITION_VERT;
      } else {
        return PARTITION_NONE;
      }
    case PARTITION_HORZ_3:
      if (bh_chr >= 16)
        return PARTITION_HORZ_3;
      else
        return (bh_chr < 8) ? PARTITION_NONE : PARTITION_HORZ;
    case PARTITION_VERT_3:
      if (bw_chr >= 16)
        return PARTITION_VERT_3;
      else
        return (bw_chr < 8) ? PARTITION_NONE : PARTITION_VERT;
    case PARTITION_SPLIT:
      return (bh_chr < 8 || bw_chr < 8) ? PARTITION_NONE : PARTITION_SPLIT;
    case PARTITION_INVALID: return PARTITION_INVALID;
    default: assert(0);
  }
  return PARTITION_INVALID;
}

/* clang-format off */

static const TX_SIZE max_txsize_lookup[BLOCK_SIZES_ALL] = {
  //                   4X4
                       TX_4X4,
  // 4X8,    8X4,      8X8
  TX_4X4,    TX_4X4,   TX_8X8,
  // 8X16,   16X8,     16X16
  TX_8X8,    TX_8X8,   TX_16X16,
  // 16X32,  32X16,    32X32
  TX_16X16,  TX_16X16, TX_32X32,
  // 32X64,  64X32,
  TX_32X32,  TX_32X32,
  // 64X64
  TX_64X64,
  // 64x128, 128x64,   128x128
  TX_64X64,  TX_64X64, TX_64X64,
  // 128X256,256X128,  256X256
  TX_64X64,  TX_64X64, TX_64X64,
  // 4x16,   16x4,     8x32
  TX_4X4,    TX_4X4,   TX_8X8,
  // 32x8,   16x64,    64x16
  TX_8X8,    TX_16X16, TX_16X16,
  // 4x32,   32x4,     8x64
  TX_4X4,    TX_4X4,   TX_8X8,
  // 64x8,   4x64,     64x4
  TX_8X8,    TX_4X4,   TX_4X4,
};

static const TX_SIZE max_txsize_rect_lookup[BLOCK_SIZES_ALL] = {
      // 4X4
      TX_4X4,
      // 4X8,    8X4,      8X8
      TX_4X8,    TX_8X4,   TX_8X8,
      // 8X16,   16X8,     16X16
      TX_8X16,   TX_16X8,  TX_16X16,
      // 16X32,  32X16,    32X32
      TX_16X32,  TX_32X16, TX_32X32,
      // 32X64,  64X32,
      TX_32X64,  TX_64X32,
      // 64X64
      TX_64X64,
      // 64x128, 128x64,   128x128
      TX_64X64,  TX_64X64, TX_64X64,
      // 128X256,256X128,  256X256
      TX_64X64,  TX_64X64, TX_64X64,
      // 4x16,   16x4,
      TX_4X16,   TX_16X4,
      // 8x32,   32x8
      TX_8X32,   TX_32X8,
      // 16x64,  64x16
      TX_16X64,  TX_64X16,
      // 4x32,   32x4,
      TX_4X32,   TX_32X4,
      // 8x64,   64x8
      TX_8X64,   TX_64X8,
      // 4x64,   64x4
      TX_4X64,   TX_64X4
};

#if CONFIG_LOSSLESS_LARGER_IDTX
static const TX_SIZE lossless_max_txsize_lookup[BLOCK_SIZES_ALL] = {
      // 4X4
      TX_4X4,
      // 4X8,    8X4,      8X8
      TX_4X8,    TX_8X4,   TX_8X8,
      // 8X16,   16X8,     16X16
      TX_8X16,   TX_16X8,  TX_16X16,
      // 16X32,  32X16,    32X32
      TX_16X32,  TX_32X16, TX_32X32,
      // 32X64,  64X32,
      TX_32X32,  TX_32X32,
      // 64X64
      TX_32X32,
      // 64x128, 128x64,   128x128
      TX_32X32,  TX_32X32, TX_32X32,
      // 128X256,256X128,  256X256
      TX_32X32,  TX_32X32, TX_32X32,
      // 4x16,   16x4,
      TX_4X16,   TX_16X4,
      // 8x32,   32x8
      TX_8X32,   TX_32X8,
      // 16x64,  64x16
      TX_16X32,  TX_32X16,
      // 4x32,   32x4,
      TX_4X32,   TX_32X4,
      // 8x64,   64x8
      TX_8X32,   TX_32X8,
      // 4x64,   64x4
      TX_4X32,   TX_32X4
};
#endif  // CONFIG_LOSSLESS_LARGER_IDTX

static const TX_TYPE_1D vtx_tab[TX_TYPES] = {
  DCT_1D,      ADST_1D, DCT_1D,      ADST_1D,
  FLIPADST_1D, DCT_1D,  FLIPADST_1D, ADST_1D, FLIPADST_1D, IDTX_1D,
  DCT_1D,      IDTX_1D, ADST_1D,     IDTX_1D, FLIPADST_1D, IDTX_1D,
};

static const TX_TYPE_1D htx_tab[TX_TYPES] = {
  DCT_1D,  DCT_1D,      ADST_1D,     ADST_1D,
  DCT_1D,  FLIPADST_1D, FLIPADST_1D, FLIPADST_1D, ADST_1D, IDTX_1D,
  IDTX_1D, DCT_1D,      IDTX_1D,     ADST_1D,     IDTX_1D, FLIPADST_1D,
};

#define TXSIZE_CAT_INVALID (-1)

/* clang-format on */

#if CONFIG_INSPECTION
// Smallest sub_tx size units. Used to compute the index in the
// tx type map.
// TODO(urvang): Is this even required?
static const TX_SIZE smallest_sub_tx_size_map[TX_SIZES_ALL] = {
  TX_4X4,    // TX_4X4
  TX_4X4,    // TX_8X8
  TX_4X4,    // TX_16X16
  TX_8X8,    // TX_32X32
  TX_16X16,  // TX_64X64
  TX_4X4,    // TX_4X8
  TX_4X4,    // TX_8X4
  TX_4X4,    // TX_8X16
  TX_4X4,    // TX_16X8
  TX_4X8,    // TX_16X32
  TX_8X4,    // TX_32X16
  TX_8X16,   // TX_32X64
  TX_16X8,   // TX_64X32
  TX_4X4,    // TX_4X16
  TX_4X4,    // TX_16X4
  TX_4X8,    // TX_8X32
  TX_8X4,    // TX_32X8
  TX_4X16,   // TX_16X64
  TX_16X4,   // TX_64X16
};
#endif  // CONFIG_INSPECTION

static const TX_SIZE sub_tx_size_map[TX_SIZES_ALL] = {
  TX_4X4,    // TX_4X4
  TX_4X4,    // TX_8X8
  TX_8X8,    // TX_16X16
  TX_16X16,  // TX_32X32
  TX_32X32,  // TX_64X64
  TX_4X4,    // TX_4X8
  TX_4X4,    // TX_8X4
  TX_8X8,    // TX_8X16
  TX_8X8,    // TX_16X8
  TX_16X16,  // TX_16X32
  TX_16X16,  // TX_32X16
  TX_32X32,  // TX_32X64
  TX_32X32,  // TX_64X32
  TX_4X8,    // TX_4X16
  TX_8X4,    // TX_16X4
  TX_8X16,   // TX_8X32
  TX_16X8,   // TX_32X8
  TX_16X32,  // TX_16X64
  TX_32X16,  // TX_64X16
  TX_4X16,   // TX_4X32
  TX_16X4,   // TX_32X4
  TX_8X32,   // TX_8X64
  TX_32X8,   // TX_64X8
  TX_4X32,   // TX_4X64
  TX_32X4,   // TX_64X4
};

static const TX_SIZE txsize_horz_map[TX_SIZES_ALL] = {
  TX_4X4,    // TX_4X4
  TX_8X8,    // TX_8X8
  TX_16X16,  // TX_16X16
  TX_32X32,  // TX_32X32
  TX_64X64,  // TX_64X64
  TX_4X4,    // TX_4X8
  TX_8X8,    // TX_8X4
  TX_8X8,    // TX_8X16
  TX_16X16,  // TX_16X8
  TX_16X16,  // TX_16X32
  TX_32X32,  // TX_32X16
  TX_32X32,  // TX_32X64
  TX_64X64,  // TX_64X32
  TX_4X4,    // TX_4X16
  TX_16X16,  // TX_16X4
  TX_8X8,    // TX_8X32
  TX_32X32,  // TX_32X8
  TX_16X16,  // TX_16X64
  TX_64X64,  // TX_64X16
  TX_4X4,    // TX_4X32
  TX_32X32,  // TX_32X4
  TX_8X8,    // TX_8X64
  TX_64X64,  // TX_64X8
  TX_4X4,    // TX_4X64
  TX_64X64,  // TX_64X4
};

static const TX_SIZE txsize_vert_map[TX_SIZES_ALL] = {
  TX_4X4,    // TX_4X4
  TX_8X8,    // TX_8X8
  TX_16X16,  // TX_16X16
  TX_32X32,  // TX_32X32
  TX_64X64,  // TX_64X64
  TX_8X8,    // TX_4X8
  TX_4X4,    // TX_8X4
  TX_16X16,  // TX_8X16
  TX_8X8,    // TX_16X8
  TX_32X32,  // TX_16X32
  TX_16X16,  // TX_32X16
  TX_64X64,  // TX_32X64
  TX_32X32,  // TX_64X32
  TX_16X16,  // TX_4X16
  TX_4X4,    // TX_16X4
  TX_32X32,  // TX_8X32
  TX_8X8,    // TX_32X8
  TX_64X64,  // TX_16X64
  TX_16X16,  // TX_64X16
  TX_32X32,  // TX_4X32
  TX_4X4,    // TX_32X4
  TX_64X64,  // TX_8X64
  TX_8X8,    // TX_64X8
  TX_64X64,  // TX_4X64
  TX_4X4,    // TX_64X4
};

#define TX_SIZE_W_MIN 4

// Transform block width in pixels
static const int tx_size_wide[TX_SIZES_ALL] = {
  4, 8,  16, 32, 64, 4,  8, 8,  16, 16, 32, 32, 64,
  4, 16, 8,  32, 16, 64, 4, 32, 8,  64, 4,  64,
};

#define TX_SIZE_H_MIN 4

// Transform block height in pixels
static const int tx_size_high[TX_SIZES_ALL] = {
  4,  8, 16, 32, 64, 8,  4,  16, 8,  32, 16, 64, 32,
  16, 4, 32, 8,  64, 16, 32, 4,  64, 8,  64, 4,
};

// Transform block width in unit
static const int tx_size_wide_unit[TX_SIZES_ALL] = {
  1, 2, 4, 8, 16, 1,  2, 2, 4, 4,  8, 8,  16,
  1, 4, 2, 8, 4,  16, 1, 8, 2, 16, 1, 16,
};

// Transform block height in unit
static const int tx_size_high_unit[TX_SIZES_ALL] = {
  1, 2, 4, 8, 16, 2, 1, 4, 2,  8, 4,  16, 8,
  4, 1, 8, 2, 16, 4, 8, 1, 16, 2, 16, 1,
};

// Transform block width in log2
static const int tx_size_wide_log2[TX_SIZES_ALL] = {
  2, 3, 4, 5, 6, 2, 3, 3, 4, 4, 5, 5, 6, 2, 4, 3, 5, 4, 6, 2, 5, 3, 6, 2, 6,
};

// Transform block height in log2
static const int tx_size_high_log2[TX_SIZES_ALL] = {
  2, 3, 4, 5, 6, 3, 2, 4, 3, 5, 4, 6, 5, 4, 2, 5, 3, 6, 4, 5, 2, 6, 3, 6, 2,
};

static const int tx_size_2d[TX_SIZES_ALL + 1] = {
  16, 64, 256, 1024, 4096, 32,   32,  128, 128, 512, 512, 2048, 2048,
  64, 64, 256, 256,  1024, 1024, 128, 128, 512, 512, 256, 256,
};

static const BLOCK_SIZE txsize_to_bsize[TX_SIZES_ALL] = {
  BLOCK_4X4,    // TX_4X4
  BLOCK_8X8,    // TX_8X8
  BLOCK_16X16,  // TX_16X16
  BLOCK_32X32,  // TX_32X32
  BLOCK_64X64,  // TX_64X64
  BLOCK_4X8,    // TX_4X8
  BLOCK_8X4,    // TX_8X4
  BLOCK_8X16,   // TX_8X16
  BLOCK_16X8,   // TX_16X8
  BLOCK_16X32,  // TX_16X32
  BLOCK_32X16,  // TX_32X16
  BLOCK_32X64,  // TX_32X64
  BLOCK_64X32,  // TX_64X32
  BLOCK_4X16,   // TX_4X16
  BLOCK_16X4,   // TX_16X4
  BLOCK_8X32,   // TX_8X32
  BLOCK_32X8,   // TX_32X8
  BLOCK_16X64,  // TX_16X64
  BLOCK_64X16,  // TX_64X16
  BLOCK_4X32,   // TX_4X32
  BLOCK_32X4,   // TX_32X4
  BLOCK_8X64,   // TX_8X64
  BLOCK_64X8,   // TX_64X8
  BLOCK_4X64,   // TX_4X64
  BLOCK_64X4,   // TX_64X4
};

static const TX_SIZE txsize_sqr_map[TX_SIZES_ALL] = {
  TX_4X4,    // TX_4X4
  TX_8X8,    // TX_8X8
  TX_16X16,  // TX_16X16
  TX_32X32,  // TX_32X32
  TX_64X64,  // TX_64X64
  TX_4X4,    // TX_4X8
  TX_4X4,    // TX_8X4
  TX_8X8,    // TX_8X16
  TX_8X8,    // TX_16X8
  TX_16X16,  // TX_16X32
  TX_16X16,  // TX_32X16
  TX_32X32,  // TX_32X64
  TX_32X32,  // TX_64X32
  TX_4X4,    // TX_4X16
  TX_4X4,    // TX_16X4
  TX_8X8,    // TX_8X32
  TX_8X8,    // TX_32X8
  TX_16X16,  // TX_16X64
  TX_16X16,  // TX_64X16
  TX_4X4,    // TX_4X32
  TX_4X4,    // TX_32X4
  TX_8X8,    // TX_8X64
  TX_8X8,    // TX_64X8
  TX_4X4,    // TX_4X64
  TX_4X4,    // TX_64X4
};

static const TX_SIZE txsize_sqr_up_map[TX_SIZES_ALL] = {
  TX_4X4,    // TX_4X4
  TX_8X8,    // TX_8X8
  TX_16X16,  // TX_16X16
  TX_32X32,  // TX_32X32
  TX_64X64,  // TX_64X64
  TX_8X8,    // TX_4X8
  TX_8X8,    // TX_8X4
  TX_16X16,  // TX_8X16
  TX_16X16,  // TX_16X8
  TX_32X32,  // TX_16X32
  TX_32X32,  // TX_32X16
  TX_64X64,  // TX_32X64
  TX_64X64,  // TX_64X32
  TX_16X16,  // TX_4X16
  TX_16X16,  // TX_16X4
  TX_32X32,  // TX_8X32
  TX_32X32,  // TX_32X8
  TX_64X64,  // TX_16X64
  TX_64X64,  // TX_64X16
  TX_32X32,  // TX_4X32
  TX_32X32,  // TX_32X4
  TX_64X64,  // TX_8X64
  TX_64X64,  // TX_64X8
  TX_64X64,  // TX_4X64
  TX_64X64,  // TX_64X4
};

static const int8_t txsize_log2_minus4[TX_SIZES_ALL] = {
  0,  // TX_4X4
  2,  // TX_8X8
  4,  // TX_16X16
  6,  // TX_32X32
  6,  // TX_64X64
  1,  // TX_4X8
  1,  // TX_8X4
  3,  // TX_8X16
  3,  // TX_16X8
  5,  // TX_16X32
  5,  // TX_32X16
  6,  // TX_32X64
  6,  // TX_64X32
  2,  // TX_4X16
  2,  // TX_16X4
  4,  // TX_8X32
  4,  // TX_32X8
  5,  // TX_16X64
  5,  // TX_64X16
  3,  // TX_4X32
  3,  // TX_32X4
  4,  // TX_8X64
  4,  // TX_64X8
  3,  // TX_4X64
  3,  // TX_64X4
};

/* clang-format off */
static const TX_SIZE tx_mode_to_biggest_tx_size[TX_MODES] = {
  TX_4X4,    // ONLY_4X4
  TX_64X64,  // TX_MODE_LARGEST
  TX_64X64,  // TX_MODE_SELECT
};

// The Subsampled_Size table in the spec (Section 5.11.38. Get plane residual
// size function).
static const BLOCK_SIZE ss_size_lookup[BLOCK_SIZES_ALL][2][2] = {
  //  ss_x == 0      ss_x == 0          ss_x == 1      ss_x == 1
  //  ss_y == 0      ss_y == 1          ss_y == 0      ss_y == 1
  { { BLOCK_4X4,     BLOCK_4X4 },     { BLOCK_4X4,     BLOCK_4X4 } },
  { { BLOCK_4X8,     BLOCK_4X4 },     { BLOCK_INVALID, BLOCK_4X4 } },
  { { BLOCK_8X4,     BLOCK_INVALID }, { BLOCK_4X4,     BLOCK_4X4 } },
  { { BLOCK_8X8,     BLOCK_8X4 },     { BLOCK_4X8,     BLOCK_4X4 } },
  { { BLOCK_8X16,    BLOCK_8X8 },     { BLOCK_4X16,    BLOCK_4X8 } },
  { { BLOCK_16X8,    BLOCK_16X4 },    { BLOCK_8X8,     BLOCK_8X4 } },
  { { BLOCK_16X16,   BLOCK_16X8 },    { BLOCK_8X16,    BLOCK_8X8 } },
  { { BLOCK_16X32,   BLOCK_16X16 },   { BLOCK_8X32,    BLOCK_8X16 } },
  { { BLOCK_32X16,   BLOCK_32X8 },    { BLOCK_16X16,   BLOCK_16X8 } },
  { { BLOCK_32X32,   BLOCK_32X16 },   { BLOCK_16X32,   BLOCK_16X16 } },
  { { BLOCK_32X64,   BLOCK_32X32 },   { BLOCK_16X64,   BLOCK_16X32 } },
  { { BLOCK_64X32,   BLOCK_64X16 },   { BLOCK_32X32,   BLOCK_32X16 } },
  { { BLOCK_64X64,   BLOCK_64X32 },   { BLOCK_32X64,   BLOCK_32X32 } },
  { { BLOCK_64X128,  BLOCK_64X64 },   { BLOCK_INVALID, BLOCK_32X64 } },
  { { BLOCK_128X64,  BLOCK_INVALID }, { BLOCK_64X64,   BLOCK_64X32 } },
  { { BLOCK_128X128, BLOCK_128X64 },  { BLOCK_64X128,  BLOCK_64X64 } },
  { { BLOCK_128X256, BLOCK_128X128 }, { BLOCK_INVALID, BLOCK_64X128 } },
  { { BLOCK_256X128, BLOCK_INVALID }, { BLOCK_128X128, BLOCK_128X64 } },
  { { BLOCK_256X256, BLOCK_256X128 }, { BLOCK_128X256, BLOCK_128X128 } },
  { { BLOCK_4X16,    BLOCK_4X8 },     { BLOCK_INVALID, BLOCK_4X8 } },
  { { BLOCK_16X4,    BLOCK_INVALID }, { BLOCK_8X4,     BLOCK_8X4 } },
  { { BLOCK_8X32,    BLOCK_8X16 },    { BLOCK_4X32,    BLOCK_4X16 } },
  { { BLOCK_32X8,    BLOCK_32X4 },    { BLOCK_16X8,    BLOCK_16X4 } },
  { { BLOCK_16X64,   BLOCK_16X32 },   { BLOCK_8X64,    BLOCK_8X32 } },
  { { BLOCK_64X16,   BLOCK_64X8 },    { BLOCK_32X16,   BLOCK_32X8 } },
  { { BLOCK_4X32, BLOCK_4X16}, { BLOCK_INVALID, BLOCK_4X16 } },
  { { BLOCK_32X4, BLOCK_INVALID }, { BLOCK_16X4, BLOCK_16X4 } },
  { { BLOCK_8X64, BLOCK_8X32 }, { BLOCK_4X64, BLOCK_4X32 } },
  { { BLOCK_64X8, BLOCK_64X4 }, { BLOCK_32X8, BLOCK_32X4 } },
  { { BLOCK_4X64, BLOCK_4X32 }, { BLOCK_INVALID, BLOCK_4X32 } },
  { { BLOCK_64X4, BLOCK_INVALID}, { BLOCK_32X4, BLOCK_32X4 } },
};
/* clang-format on */

// Generates 6 bit field in which each bit set to 1 represents
// a blocksize partition. For example:
// - 111111 means we split 256x256, 128x128, 64x64, 32x32, 16x16 and 8x8 to 4x4.
// - 110000 means we split 256x256 and 128x128 to 64x64.
// - 000000 means we keep 256x256 (no splits).
/* clang-format off */
static const struct {
  PARTITION_CONTEXT above;
  PARTITION_CONTEXT left;
} partition_context_lookup[BLOCK_SIZES_ALL] = {
  { 32 + 31, 32 + 31 },  // 4X4   - {0b111111, 0b111111}
  { 32 + 31, 32 + 30 },  // 4X8   - {0b111111, 0b111110}
  { 32 + 30, 32 + 31 },  // 8X4   - {0b111110, 0b111111}
  { 32 + 30, 32 + 30 },  // 8X8   - {0b111110, 0b111110}
  { 32 + 30, 32 + 28 },  // 8X16  - {0b111110, 0b111100}
  { 32 + 28, 32 + 30 },  // 16X8  - {0b111100, 0b111110}
  { 32 + 28, 32 + 28 },  // 16X16 - {0b111100, 0b111100}
  { 32 + 28, 32 + 24 },  // 16X32 - {0b111100, 0b111000}
  { 32 + 24, 32 + 28 },  // 32X16 - {0b111000, 0b111100}
  { 32 + 24, 32 + 24 },  // 32X32 - {0b111000, 0b111000}
  { 32 + 24, 32 + 16 },  // 32X64 - {0b111000, 0b110000}
  { 32 + 16, 32 + 24 },  // 64X32 - {0b110000, 0b111000}
  { 32 + 16, 32 + 16 },  // 64X64 - {0b110000, 0b110000}
  { 32 + 16, 32 +  0 },  // 64X128- {0b110000, 0b100000}
  { 32 +  0, 32 + 16 },  // 128X64- {0b100000, 0b110000}
  { 32 +  0, 32 +  0 },  // 128X128-{0b100000, 0b100000}
  { 32 +  0,  0 +  0 },  // 128X256-{0b100000, 0b000000}
  {  0 +  0, 32 +  0 },  // 256X128-{0b000000, 0b100000}
  {  0 +  0,  0 +  0 },  // 256X256-{0b000000, 0b000000}
  { 32 + 31, 32 + 28 },  // 4X16  - {0b111111, 0b111100}
  { 32 + 28, 32 + 31 },  // 16X4  - {0b111100, 0b111111}
  { 32 + 30, 32 + 24 },  // 8X32  - {0b111110, 0b111000}
  { 32 + 24, 32 + 30 },  // 32X8  - {0b111000, 0b111110}
  { 32 + 28, 32 + 16 },  // 16X64 - {0b111100, 0b110000}
  { 32 + 16, 32 + 28 },  // 64X16 - {0b110000, 0b111100}
  { 32 + 31, 32 + 24 },  // 4X32  - {0b111111, 0b111000}
  { 32 + 24, 32 + 31 },  // 32X4  - {0b111000, 0b111111}
  { 32 + 30, 32 + 16 },  // 8X64  - {0b111110, 0b110000}
  { 32 + 16, 32 + 30 },  // 64X8  - {0b110000, 0b111110}
  { 32 + 31, 32 + 16 },  // 4X64  - {0b111110, 0b110000}
  { 32 + 16, 32 + 31 },  // 64X4  - {0b110000, 0b111110}
};
/* clang-format on */

static const int intra_mode_context[INTRA_MODES] = {
  0, 1, 2, 3, 4, 4, 4, 4, 3, 0, 1, 2, 0,
};

// Note: this is also used in unit tests. So whenever one changes the table,
// the unit tests need to be changed accordingly.
static const int quant_dist_weight[4][2] = {
  { 2, 3 }, { 2, 5 }, { 2, 7 }, { 1, MAX_FRAME_DISTANCE }
};

static const int quant_dist_lookup_table[4][2] = {
  { 9, 7 },
  { 11, 5 },
  { 12, 4 },
  { 13, 3 },
};

// Mapping of mode dependent TX  based on intra modes.
static const int av1_md_class[INTRA_MODES] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
};

// Mapping between mode dependent TX size groups based on allowed TX sizes.
static const int av1_size_class[TX_SIZES_ALL] = { 0, 1, 2, 3, 3, 0, 0, 1, 1,
                                                  3, 3, 3, 3, 0, 0, 3, 3, 3,
                                                  3, 3, 3, 3, 3, 3, 3 };

static AOM_INLINE bool is_bsize_geq(BLOCK_SIZE bsize1, BLOCK_SIZE bsize2) {
  if (bsize1 == BLOCK_INVALID || bsize2 == BLOCK_INVALID) {
    return false;
  }
  return block_size_wide[bsize1] >= block_size_wide[bsize2] &&
         block_size_high[bsize1] >= block_size_high[bsize2];
}

static AOM_INLINE bool is_bsize_gt(BLOCK_SIZE bsize1, BLOCK_SIZE bsize2) {
  if (bsize1 == BLOCK_INVALID || bsize2 == BLOCK_INVALID) {
    return false;
  }
  return block_size_wide[bsize1] > block_size_wide[bsize2] &&
         block_size_high[bsize1] > block_size_high[bsize2];
}

static const int fwd_tx_shift[TX_SIZES_ALL][2] = {
  { 1, 10 },  // TX_4X4,    // 4x4 transform
  { 2, 10 },  // TX_8X8,    // 8x8 transform
  { 2, 11 },  // TX_16X16,  // 16x16 transform
  { 3, 12 },  // TX_32X32,  // 32x32 transform
  { 3, 14 },  // TX_64X64,  // 64x64 transform
  { 1, 11 },  // TX_4X8,    // 4x8 transform
  { 1, 11 },  // TX_8X4,    // 8x4 transform
  { 1, 12 },  // TX_8X16,   // 8x16 transform
  { 2, 11 },  // TX_16X8,   // 16x8 transform
  { 2, 13 },  // TX_16X32,  // 16x32 transform
  { 3, 12 },  // TX_32X16,  // 32x16 transform
  { 3, 14 },  // TX_32X64,  // 32x64 transform
  { 3, 14 },  // TX_64X32,  // 64x32 transform
  { 1, 11 },  // TX_4X16,   // 4x16 transform
  { 1, 11 },  // TX_16X4,   // 16x4 transform
  { 1, 12 },  // TX_8X32,   // 8x32 transform
  { 3, 10 },  // TX_32X8,   // 32x8 transform
  { 2, 13 },  // TX_16X64,  // 16x64 transform
  { 3, 12 },  // TX_64X16,  // 64x16 transform
  { 1, 12 },  // TX_4X32,   // 4x32 transform
  { 1, 12 },  // TX_32X4,   // 32x4 transform
  { 2, 13 },  // TX_8X64,   // 8x64 transform
  { 4, 11 },  // TX_64X8,   // 64x8 transform
  { 1, 12 },  // TX_4X64,   // 4x64 transform
  { 1, 12 },  // TX_64X4,   // 64x4 transform
};

static const int inv_tx_shift[TX_SIZES_ALL][2] = {
  { 7, 10 },  // TX_4X4,    // 4x4 transform
  { 7, 11 },  // TX_8X8,    // 8x8 transform
  { 6, 13 },  // TX_16X16,  // 16x16 transform
  { 6, 13 },  // TX_32X32,  // 32x32 transform
  { 6, 13 },  // TX_64X64,  // 64x64 transform
  { 7, 10 },  // TX_4X8,    // 4x8 transform
  { 7, 10 },  // TX_8X4,    // 8x4 transform
  { 7, 11 },  // TX_8X16,   // 8x16 transform
  { 7, 11 },  // TX_16X8,   // 16x8 transform
  { 6, 12 },  // TX_16X32,  // 16x32 transformß
  { 6, 12 },  // TX_32X16,  // 32x16 transform
  { 6, 12 },  // TX_32X64,  // 32x64 transform
  { 6, 12 },  // TX_64X32,  // 64x32 transform
  { 6, 12 },  // TX_4X16,   // 4x16 transform
  { 6, 12 },  // TX_16X4,   // 16x4 transform
  { 6, 13 },  // TX_8X32,   // 8x32 transform
  { 6, 13 },  // TX_32X8,   // 32x8 transform
  { 6, 13 },  // TX_16X64,  // 16x64 transform
  { 6, 13 },  // TX_64X16,  // 64x16 transform
  { 7, 11 },  // TX_4X32,   // 4x32 transform
  { 7, 11 },  // TX_32X4,   // 32x4 transform
  { 6, 12 },  // TX_8X64,   // 8x64 transform
  { 6, 12 },  // TX_64X8,   // 64x8 transform
  { 6, 13 },  // TX_4X64,   // 4x64 transform
  { 6, 13 },  // TX_64X4,   // 64x4 transform
};

static const int g_hor_tx_type[TX_TYPES] = {
  DCT2,  // DCT_DCT,            // DCT in both horizontal and vertical
  DCT2,  // ADST_DCT,           // ADST in vertical, DCT in horizontal
  DST7,  // DCT_ADST,           // DCT in vertical, ADST in horizontal
  DST7,  // ADST_ADST,          // ADST in both directions
  DCT2,  // FLIPADST_DCT,       // FLIPADST in vertical, DCT in horizontal
  DCT8,  // DCT_FLIPADST,       // DCT in vertical, FLIPADST in horizontal
  DCT8,  // FLIPADST_FLIPADST,  // FLIPADST in both directions
  DCT8,  // ADST_FLIPADST,      // ADST in vertical, FLIPADST in horizontal
  DST7,  // FLIPADST_ADST,      // FLIPADST in vertical, ADST in horizontal
  IDT,   // IDTX,               // Identity in both directions
  IDT,   // V_DCT,              // DCT in vertical, identity in horizontal
  DCT2,  // H_DCT,              // Identity in vertical, DCT in horizontal
  IDT,   // V_ADST,             // ADST in vertical, identity in horizontal
  DST7,  // H_ADST,             // Identity in vertical, ADST in horizontal
  IDT,   // V_FLIPADST,         // FLIPADST in vertical, identity in horizontal
  DCT8,  // H_FLIPADST,         // Identity in vertical, FLIPADST in horizontal
};

static const int g_ver_tx_type[TX_TYPES] = {
  DCT2,  // DCT_DCT,            // DCT in both horizontal and vertical
  DST7,  // ADST_DCT,           // ADST in vertical, DCT in horizontal
  DCT2,  // DCT_ADST,           // DCT in vertical, ADST in horizontal
  DST7,  // ADST_ADST,          // ADST in both directions
  DCT8,  // FLIPADST_DCT,       // FLIPADST in vertical, DCT in horizontal
  DCT2,  // DCT_FLIPADST,       // DCT in vertical, FLIPADST in horizontal
  DCT8,  // FLIPADST_FLIPADST,  // FLIPADST in both directions
  DST7,  // ADST_FLIPADST,      // ADST in vertical, FLIPADST in horizontal
  DCT8,  // FLIPADST_ADST,      // FLIPADST in vertical, ADST in horizontal
  IDT,   // IDTX,               // Identity in both directions
  DCT2,  // V_DCT,              // DCT in vertical, identity in horizontal
  IDT,   // H_DCT,              // Identity in vertical, DCT in horizontal
  DST7,  // V_ADST,             // ADST in vertical, identity in horizontal
  IDT,   // H_ADST,             // Identity in vertical, ADST in horizontal
  DCT8,  // V_FLIPADST,         // FLIPADST in vertical, identity in horizontal
  IDT,   // H_FLIPADST,         // Identity in vertical, FLIPADST in horizontal
};

static const int min_class_with_offset[7] = { 4, 3, 2, 0, 0, 0, 0 };

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_COMMON_DATA_H_
