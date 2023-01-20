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

#ifndef AOM_AV1_COMMON_ENUMS_H_
#define AOM_AV1_COMMON_ENUMS_H_

#include "config/aom_config.h"

#include "aom/aom_codec.h"
#include "aom/aom_integer.h"
#include "aom_ports/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! @file */

/*!\cond */

#undef MAX_SB_SIZE
#define BAWP_BUGFIX 1

#if CONFIG_REFINEMV
#define SINGLE_STEP_SEARCH 0
#endif  // CONFIG_REFINEMV

#if CONFIG_WEDGE_MOD_EXT
/*WEDGE_0 is defined in the three o'clock direciton, the angles are defined in
 * the anticlockwise.*/
enum {
  WEDGE_0,
  WEDGE_14,
  WEDGE_27,
  WEDGE_45,
  WEDGE_63,
  WEDGE_90,
  WEDGE_117,
  WEDGE_135,
  WEDGE_153,
  WEDGE_166,
  WEDGE_180,
  WEDGE_194,
  WEDGE_207,
  WEDGE_225,
  WEDGE_243,
  WEDGE_270,
  WEDGE_297,
  WEDGE_315,
  WEDGE_333,
  WEDGE_346,
  WEDGE_ANGLES
} UENUM1BYTE(WedgeDirectionType);

#define H_WEDGE_ANGLES 10
#define NUM_WEDGE_DIST 4
#define MAX_WEDGE_TYPES 68
#define WEDGE_BLD_SIG 1  // 0 for linear blending, 1 for sigmoid blending
#define WEDGE_BLD_LUT_SIZE 128
#endif  // CONFIG_WEDGE_MOD_EXT

#if CONFIG_WARP_REF_LIST && CONFIG_C043_MVP_IMPROVEMENTS
#define WARP_CU_BANK 1
#else
#define WARP_CU_BANK 0
#endif  // CONFIG_WARP_REF_LIST && CONFIG_C043_MVP_IMPROVEMENTS

#if CONFIG_REFINEMV
#define REFINEMV_SUBBLOCK_WIDTH 16
#define REFINEMV_SUBBLOCK_HEIGHT 16
#endif  // CONFIG_REFINEMV

// Cross-Component Sample Offset (CCSO)
#if CONFIG_CCSO
#define CCSO_BLK_SIZE 7
#define CCSO_PADDING_SIZE 5
#if CONFIG_CCSO_EXT
#define CCSO_BAND_NUM 8
#define CCSO_NUM_COMPONENTS 3
#else
#define CCSO_BAND_NUM 1
#define CCSO_NUM_COMPONENTS 2
#endif
#endif
#if CONFIG_ADAPTIVE_MVD
#define IMPROVED_AMVD 1
#else
#define IMPROVED_AMVD 0
#endif  // CONFIG_ADAPTIVE_MVD

#if CONFIG_ADAPTIVE_MVD && CONFIG_FLEX_MVRES
#define BUGFIX_AMVD_AMVR 1
#else
#define BUGFIX_AMVD_AMVR 0
#endif  // CONFIG_ADAPTIVE_MVD && CONFIG_FLEX_MVRES

#if CONFIG_IMPROVED_JMVD
// Supported scale modes for JOINT_NEWMV
#define JOINT_NEWMV_SCALE_FACTOR_CNT 5
// Supoorted scale modes for JOINT_AMVDNEWMV
#define JOINT_AMVD_SCALE_FACTOR_CNT 3
#endif  // CONFIG_IMPROVED_JMVD

// Max superblock size
#define MAX_SB_SIZE_LOG2 7
#define MAX_SB_SIZE (1 << MAX_SB_SIZE_LOG2)
#define MAX_SB_SQUARE (MAX_SB_SIZE * MAX_SB_SIZE)

// Min superblock size
#define MIN_SB_SIZE_LOG2 6

// Pixels per Mode Info (MI) unit
#define MI_SIZE_LOG2 2
#define MI_SIZE (1 << MI_SIZE_LOG2)

// 1/8 pels per Mode Info (MI) unit
#define MI_SUBPEL_SIZE_LOG2 (MI_SIZE_LOG2 + 3)
#define MI_SUBPEL_SIZE (1 << MI_SUBPEL_SIZE_LOG2)

// MI-units per max superblock (MI Block - MIB)
#define MAX_MIB_SIZE_LOG2 (MAX_SB_SIZE_LOG2 - MI_SIZE_LOG2)
#define MAX_MIB_SIZE (1 << MAX_MIB_SIZE_LOG2)

#define MAX_MIB_SQUARE (MAX_MIB_SIZE * MAX_MIB_SIZE)

// MI-units per min superblock
#define MIN_MIB_SIZE_LOG2 (MIN_SB_SIZE_LOG2 - MI_SIZE_LOG2)

// Mask to extract MI offset within max MIB
#define MAX_MIB_MASK (MAX_MIB_SIZE - 1)

// Maximum number of tile rows and tile columns
#define MAX_TILE_ROWS 64
#define MAX_TILE_COLS 64

#define MAX_VARTX_DEPTH 2

#define MI_SIZE_64X64 (64 >> MI_SIZE_LOG2)
#define MI_SIZE_128X128 (128 >> MI_SIZE_LOG2)

#define MAX_PALETTE_SQUARE (64 * 64)
// Maximum number of colors in a palette.
#define PALETTE_MAX_SIZE 8
// Minimum number of colors in a palette.
#define PALETTE_MIN_SIZE 2

#define FRAME_OFFSET_BITS 5
#define MAX_FRAME_DISTANCE ((1 << FRAME_OFFSET_BITS) - 1)

// 4 frame filter levels: y plane vertical, y plane horizontal,
// u plane, and v plane
#define FRAME_LF_COUNT 4
#define DEFAULT_DELTA_LF_MULTI 0
#define MAX_MODE_LF_DELTAS 2

// Semi-Decoupled Partitioning
#define SHARED_PART_SIZE 128
#define PARTITION_STRUCTURE_NUM 2

// Multiple reference line selection for intra prediction
#define MRL_LINE_NUMBER 4
#if CONFIG_AIMC
#define FIRST_MODE_COUNT 13
#define SECOND_MODE_COUNT 16
#define Y_MODE_CONTEXTS 3
#define UV_MODE_CONTEXTS 2
#define INTRA_MODE_SETS 4
#define NON_DIRECTIONAL_MODES_COUNT 5
#endif  // CONFIG_AIMC

// Intra Secondary Transform
#define IST_SET_SIZE 14  // IST kernel set size
#define STX_TYPES 4      // 4 sec_tx_types including no IST
#define IST_4x4_WIDTH 16
#define IST_4x4_HEIGHT 8
#define IST_8x8_WIDTH 64
#define IST_8x8_HEIGHT 32

#define FSC_MODES 2
#if CONFIG_ATC_DCTX_ALIGNED
#define FSC_MAXWIDTH 32
#define FSC_MAXHEIGHT 32
#else
#define FSC_MAXWIDTH 16
#define FSC_MAXHEIGHT 16
#endif  // CONFIG_ATC_DCTX_ALIGNED
#define FSC_MINWIDTH 4
#define FSC_MINHEIGHT 4

#define DIST_PRECISION_BITS 4
#define DIST_PRECISION (1 << DIST_PRECISION_BITS)  // 16

#define PROFILE_BITS 3
// The following three profiles are currently defined.
// Profile 0.  8-bit and 10-bit 4:2:0 and 4:0:0 only.
// Profile 1.  8-bit and 10-bit 4:4:4
// Profile 2.  8-bit and 10-bit 4:2:2
//            12-bit  4:0:0, 4:2:2 and 4:4:4
// Since we have three bits for the profiles, it can be extended later.
enum {
  PROFILE_0,
  PROFILE_1,
  PROFILE_2,
  MAX_PROFILES,
} SENUM1BYTE(BITSTREAM_PROFILE);

#define OP_POINTS_CNT_MINUS_1_BITS 5
#define OP_POINTS_IDC_BITS 12

// Note: Some enums use the attribute 'packed' to use smallest possible integer
// type, so that we can save memory when they are used in structs/arrays.

typedef enum ATTRIBUTE_PACKED {
  BLOCK_4X4,
  BLOCK_4X8,
  BLOCK_8X4,
  BLOCK_8X8,
  BLOCK_8X16,
  BLOCK_16X8,
  BLOCK_16X16,
  BLOCK_16X32,
  BLOCK_32X16,
  BLOCK_32X32,
  BLOCK_32X64,
  BLOCK_64X32,
  BLOCK_64X64,
  BLOCK_64X128,
  BLOCK_128X64,
  BLOCK_128X128,
  BLOCK_4X16,
  BLOCK_16X4,
  BLOCK_8X32,
  BLOCK_32X8,
  BLOCK_16X64,
  BLOCK_64X16,
  BLOCK_SIZES_ALL,
  BLOCK_SIZES = BLOCK_4X16,
  BLOCK_INVALID = 255,
  BLOCK_LARGEST = (BLOCK_SIZES - 1)
} BLOCK_SIZE;

static AOM_INLINE BLOCK_SIZE get_larger_sqr_bsize(BLOCK_SIZE bsize) {
  switch (bsize) {
    case BLOCK_4X4:
    case BLOCK_4X8:
    case BLOCK_8X4: return BLOCK_8X8;

    case BLOCK_8X8:
    case BLOCK_8X16:
    case BLOCK_16X8:
    case BLOCK_4X16:
    case BLOCK_16X4: return BLOCK_16X16;

    case BLOCK_16X16:
    case BLOCK_16X32:
    case BLOCK_32X16:
    case BLOCK_8X32:
    case BLOCK_32X8: return BLOCK_32X32;

    case BLOCK_32X32:
    case BLOCK_32X64:
    case BLOCK_64X32:
    case BLOCK_16X64:
    case BLOCK_64X16: return BLOCK_64X64;

    case BLOCK_64X64:
    case BLOCK_64X128:
    case BLOCK_128X64:
    case BLOCK_128X128: return BLOCK_128X128;
    default: return BLOCK_INVALID;
  }
}

enum {
  SHARED_PART = 0,
  LUMA_PART = 1,
  CHROMA_PART = 2,
  TREES_TYPES,
} UENUM1BYTE(TREE_TYPE);

// 4X4, 8X8, 16X16, 32X32, 64X64, 128X128
#define SQR_BLOCK_SIZES 6

#if CONFIG_EXT_RECUR_PARTITIONS
#define KEEP_PARTITION_SPLIT 0
#endif  // CONFIG_EXT_RECUR_PARTITIONS

//  Partition types.  R: Recursive
//
//  NONE          HORZ          VERT          SPLIT
//  +-------+     +-------+     +---+---+     +---+---+
//  |       |     |       |     |   |   |     | R | R |
//  |       |     +-------+     |   |   |     +---+---+
//  |       |     |       |     |   |   |     | R | R |
//  +-------+     +-------+     +---+---+     +---+---+
//
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_H_PARTITION
//  HORZ_3                 VERT_3
//  +---------------+       +---+------+---+
//  |               |       |   |      |   |
//  +---------------+       |   |      |   |
//  |       |       |       |   |______|   |
//  |       |       |       |   |      |   |
//  +---------------+       |   |      |   |
//  |               |       |   |      |   |
//  +---------------+       +---+------+---+
#else
//  HORZ_3                 VERT_3
//  +--------------+       +---+------+---+
//  |              |       |   |      |   |
//  +--------------+       |   |      |   |
//  |              |       |   |      |   |
//  |              |       |   |      |   |
//  +--------------+       |   |      |   |
//  |              |       |   |      |   |
//  +--------------+       +---+------+---+
#endif  // CONFIG_H_PARTITION
#if CONFIG_UNEVEN_4WAY
//  HORZ_4A                 HORZ_4B
//  +---------------+       +---------------+
//  |               |       |               |
//  +---------------+       +---------------+
//  |               |       |               |
//  |               |       |               |
//  +---------------+       |               |
//  |               |       |               |
//  |               |       +---------------+
//  |               |       |               |
//  |               |       |               |
//  +---------------+       +---------------+
//  |               |       |               |
//  +---------------+       +---------------+
//
//  VERT_4A                                 VERT_4B
//  +-------------------------+          +-------------------------+
//  |   |      |          |   |          |   |          |      |   |
//  |   |      |          |   |          |   |          |      |   |
//  |   |      |          |   |          |   |          |      |   |
//  +-------------------------+          +-------------------------+
#endif  // CONFIG_UNEVEN_4WAY
#else
//  HORZ_A        HORZ_B        VERT_A        VERT_B
//  +---+---+     +-------+     +---+---+     +---+---+
//  |   |   |     |       |     |   |   |     |   |   |
//  +---+---+     +---+---+     +---+   |     |   +---+
//  |       |     |   |   |     |   |   |     |   |   |
//  +-------+     +---+---+     +---+---+     +---+---+
//
//  HORZ_4        VERT_4
//  +-----+       +-+-+-+
//  +-----+       | | | |
//  +-----+       | | | |
//  +-----+       +-+-+-+
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXT_RECUR_PARTITIONS
enum {
  PARTITION_NONE,
  PARTITION_HORZ,
  PARTITION_VERT,
#if !CONFIG_UNEVEN_4WAY || CONFIG_H_PARTITION
  PARTITION_HORZ_3,  // 3 horizontal sub-partitions with ratios 4:1, 2:1 and 4:1
  PARTITION_VERT_3,  // 3 vertical sub-partitions with ratios 4:1, 2:1 and 4:1
#endif               // !CONFIG_UNEVEN_4WAY || CONFIG_H_PARTITION
#if CONFIG_UNEVEN_4WAY
  PARTITION_HORZ_4A,  // 4 horizontal uneven sub-partitions (1:2:4:1).
  PARTITION_HORZ_4B,  // 4 horizontal uneven sub-partitions (1:4:2:1).
  PARTITION_VERT_4A,  // 4 vertical uneven sub-partitions (1:2:4:1).
  PARTITION_VERT_4B,  // 4 vertical uneven sub-partitions (1:4:2:1).
#endif                // CONFIG_UNEVEN_4WAY
  EXT_PARTITION_TYPES,
  PARTITION_SPLIT = EXT_PARTITION_TYPES,
  PARTITION_TYPES = PARTITION_VERT + 1,
#if !CONFIG_UNEVEN_4WAY
  LIMITED_PARTITION_TYPES = PARTITION_TYPES - 1,
  LIMITED_EXT_PARTITION_TYPES = EXT_PARTITION_TYPES - 1,
#endif  // !CONFIG_UNEVEN_4WAY
  PARTITION_INVALID = 255
} UENUM1BYTE(PARTITION_TYPE);
#else   // CONFIG_EXT_RECUR_PARTITIONS
enum {
  PARTITION_NONE,
  PARTITION_HORZ,
  PARTITION_VERT,
  PARTITION_SPLIT,
  PARTITION_HORZ_A,  // HORZ split and the top partition is split again
  PARTITION_HORZ_B,  // HORZ split and the bottom partition is split again
  PARTITION_VERT_A,  // VERT split and the left partition is split again
  PARTITION_VERT_B,  // VERT split and the right partition is split again
  PARTITION_HORZ_4,  // 4:1 horizontal partition
  PARTITION_VERT_4,  // 4:1 vertical partition
  EXT_PARTITION_TYPES,
  PARTITION_TYPES = PARTITION_SPLIT + 1,
  PARTITION_INVALID = 255
} UENUM1BYTE(PARTITION_TYPE);
#endif  // CONFIG_EXT_RECUR_PARTITIONS

// Rectangular partition types.
enum {
  HORZ = 0,
  VERT,
  NUM_RECT_PARTS,
  RECT_INVALID = NUM_RECT_PARTS
} UENUM1BYTE(RECT_PART_TYPE);

#if CONFIG_UNEVEN_4WAY
// Uneven 4-way partition types.
enum {
  UNEVEN_4A = 0,
  UNEVEN_4B,
  NUM_UNEVEN_4WAY_PARTS,
} UENUM1BYTE(UNEVEN_4WAY_PART_TYPE);
#endif  // CONFIG_UNEVEN_4WAY

typedef char PARTITION_CONTEXT;
#define PARTITION_PLOFFSET 4  // number of probability models per block size

#if CONFIG_EXT_RECUR_PARTITIONS
#define PARTITION_BLOCK_SIZES BLOCK_SIZES
#else
#define PARTITION_BLOCK_SIZES 5
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#define PARTITION_CONTEXTS (PARTITION_BLOCK_SIZES * PARTITION_PLOFFSET)

// block transform size
enum {
  TX_4X4,             // 4x4 transform
  TX_8X8,             // 8x8 transform
  TX_16X16,           // 16x16 transform
  TX_32X32,           // 32x32 transform
  TX_64X64,           // 64x64 transform
  TX_4X8,             // 4x8 transform
  TX_8X4,             // 8x4 transform
  TX_8X16,            // 8x16 transform
  TX_16X8,            // 16x8 transform
  TX_16X32,           // 16x32 transform
  TX_32X16,           // 32x16 transform
  TX_32X64,           // 32x64 transform
  TX_64X32,           // 64x32 transform
  TX_4X16,            // 4x16 transform
  TX_16X4,            // 16x4 transform
  TX_8X32,            // 8x32 transform
  TX_32X8,            // 32x8 transform
  TX_16X64,           // 16x64 transform
  TX_64X16,           // 64x16 transform
  TX_SIZES_ALL,       // Includes rectangular transforms
  TX_SIZES = TX_4X8,  // Does NOT include rectangular transforms
  TX_SIZES_LARGEST = TX_64X64,
  TX_INVALID = 255  // Invalid transform size
} UENUM1BYTE(TX_SIZE);

#if CONFIG_NEW_TX_PARTITION
//  Baseline transform partition types
//
//  Square:
//  NONE           SPLIT
//  +-------+      +---+---+
//  |       |      |   |   |
//  |       |      +---+---+
//  |       |      |   |   |
//  +-------+      +---+---+
//
//
//  Rectangular:
//  NONE                  SPLIT
//  +--------------+      +-------+-------+
//  |              |      |       |       |
//  |              |      +       +       +
//  |              |      |       |       |
//  +--------------+      +-------+-------+
//
//  Extended transform partition types (square and rect are the same)
//
//  NONE           SPLIT
//  +-------+      +---+---+
//  |       |      |   |   |
//  |       |      +---+---+
//  |       |      |   |   |
//  +-------+      +---+---+
//
//  HORZ                 VERT
//  +-------+      +---+---+
//  |       |      |   |   |
//  +-------+      |   |   |
//  |       |      |   |   |
//  +-------+      +---+---+
//
enum {
  TX_PARTITION_NONE,
  TX_PARTITION_SPLIT,
  TX_PARTITION_HORZ,
  TX_PARTITION_VERT,
  TX_PARTITION_TYPES,
  TX_PARTITION_TYPES_INTRA = TX_PARTITION_TYPES,
  TX_PARTITION_INVALID = 255
} UENUM1BYTE(TX_PARTITION_TYPE);
#endif  // CONFIG_NEW_TX_PARTITION

#define TX_SIZE_LUMA_MIN (TX_4X4)
/* We don't need to code a transform size unless the allowed size is at least
   one more than the minimum. */
#define TX_SIZE_CTX_MIN (TX_SIZE_LUMA_MIN + 1)

// Maximum tx_size categories
#define MAX_TX_CATS (TX_SIZES - TX_SIZE_CTX_MIN)
#define MAX_TX_DEPTH 2

#define MAX_TX_SIZE_LOG2 (6)
#define MAX_TX_SIZE (1 << MAX_TX_SIZE_LOG2)
#define MIN_TX_SIZE_LOG2 2
#define MIN_TX_SIZE (1 << MIN_TX_SIZE_LOG2)
#define MAX_TX_SQUARE (MAX_TX_SIZE * MAX_TX_SIZE)

// Pad 4 extra columns to remove horizontal availability check.
#define TX_PAD_HOR_LOG2 2
#define TX_PAD_HOR 4
// Pad 6 extra rows (2 on top and 4 on bottom) to remove vertical availability
// check.
#define TX_PAD_LEFT 4
#define TX_PAD_RIGHT 4
#define TX_PAD_TOP 4
#define TX_PAD_BOTTOM 4
#define TX_PAD_VER (TX_PAD_TOP + TX_PAD_BOTTOM)
// Pad 16 extra bytes to avoid reading overflow in SIMD optimization.
#define TX_PAD_END 16
#define TX_PAD_2D ((32 + TX_PAD_HOR) * (32 + TX_PAD_VER) + TX_PAD_END)

// Number of maxium size transform blocks in the maximum size superblock
#define MAX_TX_BLOCKS_IN_MAX_SB_LOG2 ((MAX_SB_SIZE_LOG2 - MAX_TX_SIZE_LOG2) * 2)
#define MAX_TX_BLOCKS_IN_MAX_SB (1 << MAX_TX_BLOCKS_IN_MAX_SB_LOG2)

// frame transform mode
enum {
  ONLY_4X4,         // use only 4x4 transform
  TX_MODE_LARGEST,  // transform size is the largest possible for pu size
  TX_MODE_SELECT,   // transform specified for each block
  TX_MODES,
} UENUM1BYTE(TX_MODE);

// 1D tx types
enum {
  DCT_1D,
  ADST_1D,
  FLIPADST_1D,
  IDTX_1D,
  TX_TYPES_1D,
} UENUM1BYTE(TX_TYPE_1D);

enum {
  DCT_DCT,            // DCT in both horizontal and vertical
  ADST_DCT,           // ADST in vertical, DCT in horizontal
  DCT_ADST,           // DCT in vertical, ADST in horizontal
  ADST_ADST,          // ADST in both directions
  FLIPADST_DCT,       // FLIPADST in vertical, DCT in horizontal
  DCT_FLIPADST,       // DCT in vertical, FLIPADST in horizontal
  FLIPADST_FLIPADST,  // FLIPADST in both directions
  ADST_FLIPADST,      // ADST in vertical, FLIPADST in horizontal
  FLIPADST_ADST,      // FLIPADST in vertical, ADST in horizontal
  IDTX,               // Identity in both directions
  V_DCT,              // DCT in vertical, identity in horizontal
  H_DCT,              // Identity in vertical, DCT in horizontal
  V_ADST,             // ADST in vertical, identity in horizontal
  H_ADST,             // Identity in vertical, ADST in horizontal
  V_FLIPADST,         // FLIPADST in vertical, identity in horizontal
  H_FLIPADST,         // Identity in vertical, FLIPADST in horizontal
  TX_TYPES,
  DCT_ADST_TX_MASK = 0x000F,  // Either DCT or ADST in each direction
} UENUM1BYTE(TX_TYPE);

#if CONFIG_CROSS_CHROMA_TX
#define CCTX_CONTEXTS 3

// Drop C2 channel for some cctx_types.
#define CCTX_C2_DROPPED 0
#if CCTX_C2_DROPPED
#define CCTX_DROP_30 1
#define CCTX_DROP_60 1
#define CCTX_DROP_45 1
#endif  // CCTX_C2_DROPPED

enum {
  CCTX_NONE,     // No cross chroma transform
  CCTX_45,       // 45 degrees rotation (Haar transform)
  CCTX_30,       // 30 degrees rotation
  CCTX_60,       // 60 degrees rotation
  CCTX_MINUS45,  // -45 degrees rotation
  CCTX_MINUS30,  // -30 degrees rotation
  CCTX_MINUS60,  // -60 degrees rotation
  CCTX_TYPES,
  CCTX_START = CCTX_NONE + 1,
} UENUM1BYTE(CctxType);
#endif  // CONFIG_CROSS_CHROMA_TX

enum {
  REG_REG,
  REG_SMOOTH,
  REG_SHARP,
  SMOOTH_REG,
  SMOOTH_SMOOTH,
  SMOOTH_SHARP,
  SHARP_REG,
  SHARP_SMOOTH,
  SHARP_SHARP,
} UENUM1BYTE(DUAL_FILTER_TYPE);

enum {
  // DCT only
  EXT_TX_SET_DCTONLY,
  // DCT + Identity only
  EXT_TX_SET_DCT_IDTX,
  // Discrete Trig transforms w/o flip (4) + Identity (1)
  EXT_TX_SET_DTT4_IDTX,
  // Discrete Trig transforms w/o flip (4) + Identity (1) + 1D Hor/vert DCT (2)
  EXT_TX_SET_DTT4_IDTX_1DDCT,
  // Discrete Trig transforms w/ flip (9) + Identity (1) + 1D Hor/Ver DCT (2)
  EXT_TX_SET_DTT9_IDTX_1DDCT,
  // Discrete Trig transforms w/ flip (9) + Identity (1) + 1D Hor/Ver (6)
  EXT_TX_SET_ALL16,
#if CONFIG_ATC_NEWTXSETS
  EXT_NEW_TX_SET,
#endif  // CONFIG_ATC_NEWTXSETS
  EXT_TX_SET_TYPES
} UENUM1BYTE(TxSetType);

#if CONFIG_ATC_DCTX_ALIGNED
#define EOB_TX_CTXS 3
#endif                       // CONFIG_ATC_DCTX_ALIGNED
#define EXT_TX_SIZES 4       // number of sizes that use extended transforms
#define EXT_TX_SETS_INTER 4  // Sets of transform selections for INTER
#if CONFIG_ATC_NEWTXSETS && !CONFIG_ATC_REDUCED_TXSET
#define EXT_TX_SETS_INTRA 2  // Sets of transform selections for INTRA
#else
#define EXT_TX_SETS_INTRA 3  // Sets of transform selections for INTRA
#endif  // CONFIG_ATC_NEWTXSETS && !CONFIG_ATC_REDUCED_TXSET

#if CONFIG_ATC_NEWTXSETS
#define INTRA_TX_SET1 7
#if CONFIG_ATC_REDUCED_TXSET
#define INTRA_TX_SET2 2
#endif  // CONFIG_ATC_REDUCED_TXSET
#else
#define INTRA_TX_SET1 6
#define INTRA_TX_SET2 4
#endif  // CONFIG_ATC_NEWTXSETS

enum {
  UNIDIR_COMP_REFERENCE,
  BIDIR_COMP_REFERENCE,
  COMP_REFERENCE_TYPES,
} UENUM1BYTE(COMP_REFERENCE_TYPE);

enum { PLANE_TYPE_Y, PLANE_TYPE_UV, PLANE_TYPES } UENUM1BYTE(PLANE_TYPE);

#if CONFIG_CFL_IMPROVEMENTS
#define CFL_ALPHABET_SIZE_LOG2 3
#else
#define CFL_ALPHABET_SIZE_LOG2 4
#endif  // CONFIG_CFL_IMPROVEMENTS
#define CFL_ALPHABET_SIZE (1 << CFL_ALPHABET_SIZE_LOG2)
#define CFL_MAGS_SIZE ((2 << CFL_ALPHABET_SIZE_LOG2) + 1)
#define CFL_IDX_U(idx) (idx >> CFL_ALPHABET_SIZE_LOG2)
#define CFL_IDX_V(idx) (idx & (CFL_ALPHABET_SIZE - 1))

enum { CFL_PRED_U, CFL_PRED_V, CFL_PRED_PLANES } UENUM1BYTE(CFL_PRED_TYPE);

enum {
  CFL_SIGN_ZERO,
  CFL_SIGN_NEG,
  CFL_SIGN_POS,
  CFL_SIGNS
} UENUM1BYTE(CFL_SIGN_TYPE);

enum {
  CFL_DISALLOWED,
  CFL_ALLOWED,
  CFL_ALLOWED_TYPES
} UENUM1BYTE(CFL_ALLOWED_TYPE);

// CFL_SIGN_ZERO,CFL_SIGN_ZERO is invalid
#define CFL_JOINT_SIGNS (CFL_SIGNS * CFL_SIGNS - 1)
// CFL_SIGN_U is equivalent to (js + 1) / 3 for js in 0 to 8
#define CFL_SIGN_U(js) (((js + 1) * 11) >> 5)
// CFL_SIGN_V is equivalent to (js + 1) % 3 for js in 0 to 8
#define CFL_SIGN_V(js) ((js + 1) - CFL_SIGNS * CFL_SIGN_U(js))

// There is no context when the alpha for a given plane is zero.
// So there are 2 fewer contexts than joint signs.
#define CFL_ALPHA_CONTEXTS (CFL_JOINT_SIGNS + 1 - CFL_SIGNS)
#define CFL_CONTEXT_U(js) (js + 1 - CFL_SIGNS)
// Also, the contexts are symmetric under swapping the planes.
#define CFL_CONTEXT_V(js) \
  (CFL_SIGN_V(js) * CFL_SIGNS + CFL_SIGN_U(js) - CFL_SIGNS)

enum {
  PALETTE_MAP,
  COLOR_MAP_TYPES,
} UENUM1BYTE(COLOR_MAP_TYPE);

enum {
  TWO_COLORS,
  THREE_COLORS,
  FOUR_COLORS,
  FIVE_COLORS,
  SIX_COLORS,
  SEVEN_COLORS,
  EIGHT_COLORS,
  PALETTE_SIZES
} UENUM1BYTE(PALETTE_SIZE);

enum {
  PALETTE_COLOR_ONE,
  PALETTE_COLOR_TWO,
  PALETTE_COLOR_THREE,
  PALETTE_COLOR_FOUR,
  PALETTE_COLOR_FIVE,
  PALETTE_COLOR_SIX,
  PALETTE_COLOR_SEVEN,
  PALETTE_COLOR_EIGHT,
  PALETTE_COLORS
} UENUM1BYTE(PALETTE_COLOR);

// Note: All directional predictors must be between V_PRED and D67_PRED (both
// inclusive).
enum {
  DC_PRED,        // Average of above and left pixels
  V_PRED,         // Vertical
  H_PRED,         // Horizontal
  D45_PRED,       // Directional 45  degree
  D135_PRED,      // Directional 135 degree
  D113_PRED,      // Directional 113 degree
  D157_PRED,      // Directional 157 degree
  D203_PRED,      // Directional 203 degree
  D67_PRED,       // Directional 67  degree
  SMOOTH_PRED,    // Combination of horizontal and vertical interpolation
  SMOOTH_V_PRED,  // Vertical interpolation
  SMOOTH_H_PRED,  // Horizontal interpolation
  PAETH_PRED,     // Predict from the direction of smallest gradient
  NEARMV,
  GLOBALMV,
  NEWMV,
#if IMPROVED_AMVD
  AMVDNEWMV,
#endif  // IMPROVED_AMVD

#if CONFIG_WARPMV
  WARPMV,  // WARPMV mode
#endif     // CONFIG_WARPMV
           // Compound ref compound modes
  NEAR_NEARMV,
  NEAR_NEWMV,
  NEW_NEARMV,
  GLOBAL_GLOBALMV,
  NEW_NEWMV,
#if CONFIG_JOINT_MVD
  JOINT_NEWMV,
#endif  // CONFIG_JOINT_MVD
#if IMPROVED_AMVD && CONFIG_JOINT_MVD
  JOINT_AMVDNEWMV,
#endif  // IMPROVED_AMVD && CONFIG_JOINT_MVD
#if CONFIG_OPTFLOW_REFINEMENT
  NEAR_NEARMV_OPTFLOW,
  NEAR_NEWMV_OPTFLOW,
  NEW_NEARMV_OPTFLOW,
  NEW_NEWMV_OPTFLOW,
#if CONFIG_JOINT_MVD
  JOINT_NEWMV_OPTFLOW,
#endif  // CONFIG_JOINT_MVD
#if IMPROVED_AMVD && CONFIG_JOINT_MVD
  JOINT_AMVDNEWMV_OPTFLOW,
#endif  // IMPROVED_AMVD && CONFIG_JOINT_MVD
#endif  // CONFIG_OPTFLOW_REFINEMENT
  MB_MODE_COUNT,
  INTRA_MODE_START = DC_PRED,
  INTRA_MODE_END = NEARMV,
  DIR_MODE_START = V_PRED,
  DIR_MODE_END = D67_PRED + 1,
  INTRA_MODE_NUM = INTRA_MODE_END - INTRA_MODE_START,
  SINGLE_INTER_MODE_START = NEARMV,
  SINGLE_INTER_MODE_END = NEAR_NEARMV,
  SINGLE_INTER_MODE_NUM = SINGLE_INTER_MODE_END - SINGLE_INTER_MODE_START,
  COMP_INTER_MODE_START = NEAR_NEARMV,
  COMP_INTER_MODE_END = MB_MODE_COUNT,
  COMP_INTER_MODE_NUM = COMP_INTER_MODE_END - COMP_INTER_MODE_START,
#if CONFIG_OPTFLOW_REFINEMENT
  COMP_OPTFLOW_MODE_START = NEAR_NEARMV_OPTFLOW,
  INTER_COMPOUND_REF_TYPES = COMP_OPTFLOW_MODE_START - COMP_INTER_MODE_START,
#endif  // CONFIG_OPTFLOW_REFINEMENT
  INTER_MODE_START = NEARMV,
  INTER_MODE_END = MB_MODE_COUNT,
  INTRA_MODES = PAETH_PRED + 1,   // PAETH_PRED has to be the last intra mode.
  INTRA_INVALID = MB_MODE_COUNT,  // For uv_mode in inter blocks
  MODE_INVALID = 255
} UENUM1BYTE(PREDICTION_MODE);

// TODO(ltrudeau) Do we really want to pack this?
// TODO(ltrudeau) Do we match with PREDICTION_MODE?
enum {
  UV_DC_PRED,        // Average of above and left pixels
  UV_V_PRED,         // Vertical
  UV_H_PRED,         // Horizontal
  UV_D45_PRED,       // Directional 45  degree
  UV_D135_PRED,      // Directional 135 degree
  UV_D113_PRED,      // Directional 113 degree
  UV_D157_PRED,      // Directional 157 degree
  UV_D203_PRED,      // Directional 203 degree
  UV_D67_PRED,       // Directional 67  degree
  UV_SMOOTH_PRED,    // Combination of horizontal and vertical interpolation
  UV_SMOOTH_V_PRED,  // Vertical interpolation
  UV_SMOOTH_H_PRED,  // Horizontal interpolation
  UV_PAETH_PRED,     // Predict from the direction of smallest gradient
  UV_CFL_PRED,       // Chroma-from-Luma
  UV_INTRA_MODES,
  UV_MODE_INVALID,  // For uv_mode in inter blocks
} UENUM1BYTE(UV_PREDICTION_MODE);

#if CONFIG_IMPROVED_CFL
enum {
  CFL_EXPLICIT,       // av1 cfl
  CFL_DERIVED_ALPHA,  // implicit CfL mode with derived scaling factor
  CFL_TYPE_COUNT,     // CfL mode type count
} UENUM1BYTE(CFL_TYPE);
#endif

// Number of top model rd to store for pruning y modes in intra mode decision
#define TOP_INTRA_MODEL_COUNT 4
// Total number of luma intra prediction modes (include both directional and
// non-directional modes)
#define LUMA_MODE_COUNT 61

enum {
  SIMPLE_TRANSLATION,
#if CONFIG_EXTENDED_WARP_PREDICTION
  INTERINTRA,
#endif            // CONFIG_EXTENDED_WARP_PREDICTION
  OBMC_CAUSAL,    // 2-sided OBMC
  WARPED_CAUSAL,  // Warp estimation from spatial MVs
#if CONFIG_EXTENDED_WARP_PREDICTION
  WARP_DELTA,   // Directly-signaled warp model
  WARP_EXTEND,  // Extension of an existing warp model into another block
#endif
  MOTION_MODES
} UENUM1BYTE(MOTION_MODE);

enum {
  II_DC_PRED,
  II_V_PRED,
  II_H_PRED,
  II_SMOOTH_PRED,
  INTERINTRA_MODES
} UENUM1BYTE(INTERINTRA_MODE);

enum {
  COMPOUND_AVERAGE,
  COMPOUND_WEDGE,
  COMPOUND_DIFFWTD,
  COMPOUND_TYPES,
  MASKED_COMPOUND_TYPES = 2,
} UENUM1BYTE(COMPOUND_TYPE);

enum {
  FILTER_DC_PRED,
  FILTER_V_PRED,
  FILTER_H_PRED,
  FILTER_D157_PRED,
  FILTER_PAETH_PRED,
  FILTER_INTRA_MODES,
} UENUM1BYTE(FILTER_INTRA_MODE);

enum {
  SEQ_LEVEL_2_0,
  SEQ_LEVEL_2_1,
  SEQ_LEVEL_2_2,
  SEQ_LEVEL_2_3,
  SEQ_LEVEL_3_0,
  SEQ_LEVEL_3_1,
  SEQ_LEVEL_3_2,
  SEQ_LEVEL_3_3,
  SEQ_LEVEL_4_0,
  SEQ_LEVEL_4_1,
  SEQ_LEVEL_4_2,
  SEQ_LEVEL_4_3,
  SEQ_LEVEL_5_0,
  SEQ_LEVEL_5_1,
  SEQ_LEVEL_5_2,
  SEQ_LEVEL_5_3,
  SEQ_LEVEL_6_0,
  SEQ_LEVEL_6_1,
  SEQ_LEVEL_6_2,
  SEQ_LEVEL_6_3,
  SEQ_LEVEL_7_0,
  SEQ_LEVEL_7_1,
  SEQ_LEVEL_7_2,
  SEQ_LEVEL_7_3,
  SEQ_LEVELS,
  SEQ_LEVEL_MAX = 31
} UENUM1BYTE(AV1_LEVEL);

#define LEVEL_BITS 5

#define DIRECTIONAL_MODES 8
#define MAX_ANGLE_DELTA 3
#define ANGLE_STEP 3

#if CONFIG_AIMC
// Total delta angles for one nominal directional mode
#define TOTAL_ANGLE_DELTA_COUNT 7
#endif

#if CONFIG_WARPMV
// The warpmv mode is signalled as a separate flag
// So the number of remaining modes to be signalled is (SINGLE_INTER_MODE_NUM-1)
#define INTER_SINGLE_MODES (SINGLE_INTER_MODE_NUM - 1)
#else
#define INTER_SINGLE_MODES SINGLE_INTER_MODE_NUM
#endif  // CONFIG_WARPMV
#define INTER_COMPOUND_MODES COMP_INTER_MODE_NUM

#if CONFIG_SKIP_MODE_ENHANCEMENT
#define SKIP_CONTEXTS 6
#else
#define SKIP_CONTEXTS 3
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
#define SKIP_MODE_CONTEXTS 3

#if CONFIG_NEW_CONTEXT_MODELING
#define INTRABC_CONTEXTS 3
#endif  // CONFIG_NEW_CONTEXT_MODELING

#define COMP_GROUP_IDX_CONTEXTS 12

#define NMV_CONTEXTS 3
#define NEWMV_MODE_CONTEXTS 6
#define GLOBALMV_MODE_CONTEXTS 2
#define REFMV_MODE_CONTEXTS 6

#define ISREFMV_MODE_CONTEXTS 2
#define MIN_MAX_DRL_BITS 1
#define MAX_MAX_DRL_BITS 7
#if CONFIG_C076_INTER_MOD_CTX
#define INTER_SINGLE_MODE_CONTEXTS (NEWMV_MODE_CONTEXTS * ISREFMV_MODE_CONTEXTS)
#else
#define INTER_SINGLE_MODE_CONTEXTS \
  (NEWMV_MODE_CONTEXTS * GLOBALMV_MODE_CONTEXTS * ISREFMV_MODE_CONTEXTS)
#endif  // CONFIG_C076_INTER_MOD_CTX
#if CONFIG_C076_INTER_MOD_CTX
#define DRL_MODE_CONTEXTS NEWMV_MODE_CONTEXTS
#else
#define DRL_MODE_CONTEXTS (NEWMV_MODE_CONTEXTS * GLOBALMV_MODE_CONTEXTS)
#endif  // CONFIG_C076_INTER_MOD_CTX

#if CONFIG_WARPMV
#define WARPMV_MODE_CONTEXT 10
#endif  // CONFIG_WARPMV

#if CONFIG_BVP_IMPROVEMENT
#define MAX_REF_BV_STACK_SIZE 4
#endif  // CONFIG_BVP_IMPROVEMENT

#define GLOBALMV_OFFSET 3
#define REFMV_OFFSET 4

#define NEWMV_CTX_MASK ((1 << GLOBALMV_OFFSET) - 1)
#define GLOBALMV_CTX_MASK ((1 << (REFMV_OFFSET - GLOBALMV_OFFSET)) - 1)
#define REFMV_CTX_MASK ((1 << (8 - REFMV_OFFSET)) - 1)

#define COMP_NEWMV_CTXS 5
#if CONFIG_C076_INTER_MOD_CTX
#define INTER_COMPOUND_MODE_CONTEXTS 6
#else
#define INTER_COMPOUND_MODE_CONTEXTS 8
#endif  // CONFIG_C076_INTER_MOD_CTX

#if CONFIG_CWP
// Number of supported factors for compound weighted prediction
#define MAX_CWP_NUM 5
// maximum value for the supported factors
#define CWP_MAX 20
// minimum value for the supported factors
#define CWP_MIN -4
// Weighting factor for simple averge prediction
#define CWP_EQUAL 8
#define CWP_WEIGHT_BITS 4
#define MAX_CWP_CONTEXTS 2
#endif

#define DELTA_Q_SMALL 3
#define DELTA_Q_PROBS (DELTA_Q_SMALL)
#define DEFAULT_DELTA_Q_RES_PERCEPTUAL 4
#define DEFAULT_DELTA_Q_RES_OBJECTIVE 4

#define DELTA_LF_SMALL 3
#define DELTA_LF_PROBS (DELTA_LF_SMALL)
#define DEFAULT_DELTA_LF_RES 2

#define MAX_MV_REF_CANDIDATES 2
#define MAX_REF_MV_STACK_SIZE 8
#define USABLE_REF_MV_STACK_SIZE (MAX_REF_MV_STACK_SIZE)
#define REF_CAT_LEVEL 640

#if CONFIG_WARP_REF_LIST
#define MAX_WARP_REF_CANDIDATES 4
#define WARP_REF_CONTEXTS 1
#endif  // CONFIG_WARP_REF_LIST

#if CONFIG_CONTEXT_DERIVATION
#define INTRA_INTER_SKIP_TXFM_CONTEXTS 2
#endif  // CONFIG_CONTEXT_DERIVATION
#define INTRA_INTER_CONTEXTS 4
#define COMP_INTER_CONTEXTS 5
#define REF_CONTEXTS 3

#define COMP_REF_TYPE_CONTEXTS 5
#define UNI_COMP_REF_CONTEXTS 3

#if CONFIG_NEW_TX_PARTITION
#define TXFM_PARTITION_INTER_CONTEXTS ((TX_SIZES - TX_8X8) * 6 - 3)
#else
#define TXFM_PARTITION_CONTEXTS ((TX_SIZES - TX_8X8) * 6 - 3)
#endif  // CONFIG_NEW_TX_PARTITION
typedef uint8_t TXFM_CONTEXT;

#if CONFIG_TIP
#define TIP_CONTEXTS 3
#endif  // CONFIG_TIP

#define INTER_REFS_PER_FRAME 7
#define REF_FRAMES (INTER_REFS_PER_FRAME + 1)
// NOTE: A limited number of unidirectional reference pairs can be signalled for
//       compound prediction. The use of skip mode, on the other hand, makes it
//       possible to have a reference pair not listed for explicit signaling.
#if CONFIG_TIP
#if CONFIG_ALLOW_SAME_REF_COMPOUND
#define MODE_CTX_REF_FRAMES                                \
  (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 3) / 2 + \
   2)  // additional combinations for the same reference of compound mode
#else
#define MODE_CTX_REF_FRAMES \
  (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 1) / 2 + 2)
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
#else
#if CONFIG_ALLOW_SAME_REF_COMPOUND
#define MODE_CTX_REF_FRAMES \
  (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 3) / 2 + 1)
#else
#define MODE_CTX_REF_FRAMES \
  (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 1) / 2 + 1)
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
#endif  // CONFIG_TIP
// With k=INTER_REFS_PER_FRAMES, indices 0 to k-1 represent rank 1 to rank k
// references. The next k(k-1)/2 indices are left for compound reference types
// (there are k choose 2 compound combinations). Then, index for intra frame is
// defined as k+k(k-1)/2.
#if CONFIG_ALLOW_SAME_REF_COMPOUND
#define INTRA_FRAME                                    \
  (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 3) / \
   2)  // additional combinations for the same reference of compound mode
#else
#define INTRA_FRAME (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 1) / 2)
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
// Used for indexing into arrays that contain reference data for
// inter and intra.
#define INTRA_FRAME_INDEX INTER_REFS_PER_FRAME
#define NONE_FRAME INVALID_IDX
#define AOM_REFFRAME_ALL ((1 << INTER_REFS_PER_FRAME) - 1)

#define REF_FRAMES_LOG2 3

// REF_FRAMES for the cm->ref_frame_map array, 1 scratch frame for the new
// frame in cm->cur_frame, INTER_REFS_PER_FRAME for scaled references on the
// encoder in the cpi->scaled_ref_buf array.
#define FRAME_BUFFERS (REF_FRAMES + 1 + INTER_REFS_PER_FRAME)

#define FWD_RF_OFFSET(ref) (ref - LAST_FRAME)
#define BWD_RF_OFFSET(ref) (ref - BWDREF_FRAME)

#define TOTAL_COMP_REFS (FWD_REFS * BWD_REFS + TOTAL_UNIDIR_COMP_REFS)

#define COMP_REFS (FWD_REFS * BWD_REFS + UNIDIR_COMP_REFS)

#if CONFIG_TIP
#define TIP_FRAME (MODE_CTX_REF_FRAMES - 1)
#define TIP_FRAME_INDEX (INTER_REFS_PER_FRAME + 1)
#define EXTREF_FRAMES (REF_FRAMES + 1)
#endif  // CONFIG_TIP

#if CONFIG_TIP
#define SINGLE_REF_FRAMES EXTREF_FRAMES
#else
#define SINGLE_REF_FRAMES REF_FRAMES
#endif  // CONFIG_TIP

// Note: It includes single and compound references. So, it can take values from
// NONE_FRAME to (MODE_CTX_REF_FRAMES - 1). Hence, it is not defined as an enum.
typedef int8_t MV_REFERENCE_FRAME;

#if CONFIG_LR_FLEX_SYNTAX
#define MAX_LR_FLEX_SWITCHABLE_BITS 4
#endif  // CONFIG_LR_FLEX_SYNTAX

/*!\endcond */

/*!\enum RestorationType
 * \brief This enumeration defines various restoration types supported
 */
typedef enum {
  RESTORE_NONE,    /**< No restoration */
  RESTORE_WIENER,  /**< Separable Wiener restoration */
  RESTORE_SGRPROJ, /**< Selfguided restoration */
#if CONFIG_PC_WIENER
  RESTORE_PC_WIENER, /**< Pixel-classified Wiener restoration */
#endif               // CONFIG_PC_WIENER
#if CONFIG_WIENER_NONSEP
  RESTORE_WIENER_NONSEP, /**< Nonseparable Wiener restoration */
#endif                   // CONFIG_WIENER_NONSEP
  RESTORE_SWITCHABLE,    /**< Switchable restoration */
  RESTORE_SWITCHABLE_TYPES = RESTORE_SWITCHABLE, /**< Num Switchable types */
  RESTORE_TYPES = RESTORE_SWITCHABLE + 1,        /**< Num Restore types */
} RestorationType;

/*!\cond */
// Picture prediction structures (0-12 are predefined) in scalability metadata.
enum {
  SCALABILITY_L1T2 = 0,
  SCALABILITY_L1T3 = 1,
  SCALABILITY_L2T1 = 2,
  SCALABILITY_L2T2 = 3,
  SCALABILITY_L2T3 = 4,
  SCALABILITY_S2T1 = 5,
  SCALABILITY_S2T2 = 6,
  SCALABILITY_S2T3 = 7,
  SCALABILITY_L2T1h = 8,
  SCALABILITY_L2T2h = 9,
  SCALABILITY_L2T3h = 10,
  SCALABILITY_S2T1h = 11,
  SCALABILITY_S2T2h = 12,
  SCALABILITY_S2T3h = 13,
  SCALABILITY_SS = 14
} UENUM1BYTE(SCALABILITY_STRUCTURES);

#define SUPERRES_SCALE_BITS 3
#define SUPERRES_SCALE_DENOMINATOR_MIN (SCALE_NUMERATOR + 1)

// In large_scale_tile coding, external references are used.
#define MAX_EXTERNAL_REFERENCES 128
#define MAX_TILES 512

#define DIR_MODES_0_90 17
#define IBP_WEIGHT_SHIFT 8
#define IBP_WEIGHT_MAX 255

#if CONFIG_WARP_REF_LIST
/*!\enum Warp projection type
 * \brief This enumeration defines various warp projection type supported
 */
typedef enum {
  PROJ_GLOBAL_MOTION,  /**< block is from global motion */
  PROJ_SPATIAL,        /**< Project from spatial neighborhood */
  PROJ_PARAM_BANK,     /**< Project from circular buffer */
  PROJ_DEFAULT,        /**< Default values */
  WARP_PROJ_TYPES = 4, /**< Num projection types */
} WarpProjectionType;
#endif  // CONFIG_WARP_REF_LIST

/*!\endcond */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ENUMS_H_
