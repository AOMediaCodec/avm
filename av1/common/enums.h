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

#if CONFIG_REDUCE_SYMBOL_SIZE
// Macros related to joint shell signaling
#define FIRST_SHELL_CLASS 8
#define SECOND_SHELL_CLASS 8
// Macros related to wedge angle signaling
#define WEDGE_QUADS 4
#define QUAD_WEDGE_ANGLES 5
#endif  // CONFIG_REDUCE_SYMBOL_SIZE

#define ADJUST_SUPER_RES_Q 1

#define NUM_CTX_IS_JOINT 2
#define NUM_OPTIONS_IS_JOINT 2
#define NUM_CTX_NON_JOINT_TYPE INTER_MODE_CONTEXTS
#define NUM_OPTIONS_NON_JOINT_TYPE 5
#define NUM_CTX_JOINT_TYPE 1
#define NUM_OPTIONS_JOINT_TYPE 2

#if CONFIG_WARP_BD_BOX
// maximum warp bound boxes.
#define MAX_WARP_BD_SIZE (1 << (8 - 3))
#define MAX_WARP_BD_SQ (MAX_WARP_BD_SIZE * MAX_WARP_BD_SIZE)
#endif  // CONFIG_WARP_BD_BOX

#if CONFIG_SUBBLK_REF_EXT
#define SUBBLK_REF_EXT_LINES 2
#endif  // CONFIG_SUBBLK_REF_EXT

#if CONFIG_16_FULL_SEARCH_DMVR || CONFIG_24_FULL_SEARCH_DMVR
#define DMVR_SEARCH_EXT_LINES 2
#else
#define DMVR_SEARCH_EXT_LINES 0
#endif  // CONFIG_16_FULL_SEARCH_DMVR || CONFIG_24_FULL_SEARCH_DMVR

#if CONFIG_WARP_PRECISION
#define WARP_STATS_BUFFER_SIZE \
  (MAX_WARP_REF_CANDIDATES * NUM_WARP_PRECISION_MODES)
#endif  // CONFIG_WARP_PRECISION

#if CONFIG_REFINEMV
#define SINGLE_STEP_SEARCH 0
#endif  // CONFIG_REFINEMV

#define AVG_CDF_WEIGHT_PRIMARY 7
#define AVG_CDF_WEIGHT_NON_PRIMARY 1

#define USE_TUNED_ADST4 1
#define USE_TUNED_ADST8 1
#define USE_TUNED_ADST16 1

#if CONFIG_CFL_SIMPLIFICATION
#define NUM_REF_SAM_CFL 8
#endif  // CONFIG_CFL_SIMPLIFICATION

#define DEFAULT_IMP_MSK_WT 0  // default implict masked blending weight

#if CONFIG_BAWP_FIX_DIVISION_16x16_MC
#define BAWP_MAX_REF_NUMB 16
#endif  // CONFIG_BAWP_FIX_DIVISION_16x16_MC
#if CONFIG_PARAKIT_COLLECT_DATA
// @ParaKit: add enum variables to indiciate context groups
enum { EOB_FLAG_CDF16, EOB_FLAG_CDF32, MAX_NUM_CTX_GROUPS };
#endif

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

#define WARP_CU_BANK 1

#if CONFIG_REFINEMV
#define REFINEMV_SUBBLOCK_WIDTH 16
#define REFINEMV_SUBBLOCK_HEIGHT 16
#endif  // CONFIG_REFINEMV

#define IBP_WEIGHT_SIZE_LOG2 4
#define IBP_WEIGHT_SIZE (1 << IBP_WEIGHT_SIZE_LOG2)

#define BUGFIX_AMVD_AMVR 1
// Supported scale modes for JOINT_NEWMV
#define JOINT_NEWMV_SCALE_FACTOR_CNT 5
// Supoorted scale modes for JOINT_AMVDNEWMV
#define JOINT_AMVD_SCALE_FACTOR_CNT 3

// Max superblock size
#define MAX_SB_SIZE_LOG2 8
#define MAX_SB_SIZE (1 << MAX_SB_SIZE_LOG2)
#define MAX_SB_SQUARE (MAX_SB_SIZE * MAX_SB_SIZE)
#define BLOCK_128_MI_SIZE_LOG2 5

// Cross-Component Sample Offset (CCSO)
#define CCSO_BLK_SIZE MAX_SB_SIZE_LOG2
#define CCSO_PADDING_SIZE 5
#define CCSO_BAND_NUM 128
#define CCSO_NUM_COMPONENTS 3

#define MHCCP_MODE_NUM 3
#define MHCCP_CONTEXT_GROUP_SIZE 4
#define LINE_NUM 2
#define MHCCP_NUM_PARAMS 3
#define MHCCP_WINDOW_SIZE 6
#define MHCCP_MAX_REF_SAMPLES \
  (2 * MHCCP_WINDOW_SIZE * (2 * MAX_SB_SIZE + MHCCP_WINDOW_SIZE))
#define MHCCP_DECIM_BITS 16
#define MHCCP_DECIM_ROUND (1 << (MHCCP_DECIM_BITS - 1))

#define FIXED_MULT(x, y) (((x) * (y) + MHCCP_DECIM_ROUND) >> MHCCP_DECIM_BITS)
#define FIXED_DIV(x, y) (((x) << MHCCP_DECIM_BITS) / (y))

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

// The largest block size where we need to construct chroma blocks separately
// from luma blocks is 64x32. With the four way partition, we can get 64x4
// block sizes. So we only need to track results for 16 mi units.
#define MAX_MI_LUMA_SIZE_FOR_SUB_8 (64 >> MI_SIZE_LOG2)
#define SUB_8_BITMASK_T uint16_t
#define SUB_8_BITMASK_SIZE (16)
#define SUB_8_BITMASK_ON (UINT16_MAX)

// Maximum number of tile rows and tile columns
#define MAX_TILE_ROWS 64
#define MAX_TILE_COLS 64

#define MAX_VARTX_DEPTH 2

#define MI_SIZE_64X64 (64 >> MI_SIZE_LOG2)
#define MI_SIZE_128X128 (128 >> MI_SIZE_LOG2)
#define MI_SIZE_256X256 (256 >> MI_SIZE_LOG2)

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

// Multiple reference line selection for intra prediction
#define MRL_LINE_NUMBER 4
#define FIRST_MODE_COUNT 13
#define SECOND_MODE_COUNT 16
#define Y_MODE_CONTEXTS 3
#define INTRA_MODE_SETS 4
#define NON_DIRECTIONAL_MODES_COUNT 5
#define UV_MODE_CONTEXTS 2
#define CFL_CONTEXTS 3

// Intra Secondary Transform
#define IST_SET_SIZE 14  // IST kernel set size
#if CONFIG_IST_SET_FLAG
// Number of directional groups in IST kernels
#define IST_DIR_SIZE (IST_SET_SIZE >> 1)
#endif               // CONFIG_IST_SET_FLAG
#define STX_TYPES 4  // 4 sec_tx_types including no IST
#define IST_4x4_WIDTH 16
#define IST_4x4_HEIGHT 8
#if CONFIG_E124_IST_REDUCE_METHOD4
#define IST_8x8_HEIGHT_RED 20
#endif  // CONFIG_E124_IST_REDUCE_METHOD4
// Note: IST_8x8_WIDTH needs to be a multiple of 4 for sse4 to work
#define IST_8x8_WIDTH_MAX 64
#define IST_8x8_WIDTH 48
#define IST_8x8_HEIGHT_MAX 32
#define IST_8x8_HEIGHT 32
#if CONFIG_F105_IST_MEM_REDUCE
#define IST_ADST_NZ_CNT 20
#endif  // CONFIG_F105_IST_MEM_REDUCE

#define STX_SYNTAX_DEBUG 0
#define STX_COEFF_DEBUG 0

#define FSC_MODES 2
#define FSC_MAXWIDTH 32
#define FSC_MAXHEIGHT 32
#define FSC_MINWIDTH 4
#define FSC_MINHEIGHT 4

#define DIST_PRECISION_BITS 4
#define DIST_PRECISION (1 << DIST_PRECISION_BITS)  // 16

#if CONFIG_IBC_SUBPEL_PRECISION
#define IBC_TOP_INTERP_BORDER 0
#define IBC_LEFT_INTERP_BORDER 0
#define IBC_RIGHT_INTERP_BORDER 1
#define IBC_BOTTOM_INTERP_BORDER 1
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#if CONFIG_24_FULL_SEARCH_DMVR
#define DMVR_SEARCH_NUM_NEIGHBORS 24
#elif CONFIG_16_FULL_SEARCH_DMVR
#define DMVR_SEARCH_NUM_NEIGHBORS 16
#endif  // CONFIG_24_FULL_SEARCH_DMVR

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
  BLOCK_128X256,
  BLOCK_256X128,
  BLOCK_256X256,
  BLOCK_4X16,
  BLOCK_16X4,
  BLOCK_8X32,
  BLOCK_32X8,
  BLOCK_16X64,
  BLOCK_64X16,
  BLOCK_4X32,
  BLOCK_32X4,
  BLOCK_8X64,
  BLOCK_64X8,
  BLOCK_4X64,
  BLOCK_64X4,
  BLOCK_SIZES_ALL,
  BLOCK_MAX = BLOCK_256X256,
  BLOCK_SIZES = BLOCK_4X32,
  BLOCK_INVALID = 255,
  BLOCK_LARGEST = BLOCK_MAX
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

enum {
  INTRA_REGION = 0,
  MIXED_INTER_INTRA_REGION = 1,
  REGION_TYPES = 2,
} UENUM1BYTE(REGION_TYPE);

// 4X4, 8X8, 16X16, 32X32, 64X64, 128X128, 256X256
#define SQR_BLOCK_SIZES 7

//  Partition types.  R: Recursive
//
//  NONE          HORZ          VERT          SPLIT
//  +-------+     +-------+     +---+---+     +---+---+
//  |       |     |       |     |   |   |     | R | R |
//  |       |     +-------+     |   |   |     +---+---+
//  |       |     |       |     |   |   |     | R | R |
//  +-------+     +-------+     +---+---+     +---+---+
//
//  HORZ_3                 VERT_3
//  +---------------+       +---+------+---+
//  |               |       |   |      |   |
//  +---------------+       |   |      |   |
//  |       |       |       |   |______|   |
//  |       |       |       |   |      |   |
//  +---------------+       |   |      |   |
//  |               |       |   |      |   |
//  +---------------+       +---+------+---+
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
enum {
  PARTITION_NONE,
  PARTITION_HORZ,
  PARTITION_VERT,
  PARTITION_HORZ_3,  // 3 horizontal sub-partitions with ratios 4:1, 2:1 and 4:1
  PARTITION_VERT_3,  // 3 vertical sub-partitions with ratios 4:1, 2:1 and 4:1
  PARTITION_HORZ_4A,  // 4 horizontal uneven sub-partitions (1:2:4:1).
  PARTITION_HORZ_4B,  // 4 horizontal uneven sub-partitions (1:4:2:1).
  PARTITION_VERT_4A,  // 4 vertical uneven sub-partitions (1:2:4:1).
  PARTITION_VERT_4B,  // 4 vertical uneven sub-partitions (1:4:2:1).
  PARTITION_SPLIT,
  EXT_PARTITION_TYPES = PARTITION_SPLIT,
  ALL_PARTITION_TYPES = EXT_PARTITION_TYPES + 1,
  PARTITION_TYPES = PARTITION_VERT + 1,
  PARTITION_INVALID = 255
} UENUM1BYTE(PARTITION_TYPE);

// Rectangular partition types.
enum {
  HORZ = 0,
  VERT,
  NUM_RECT_PARTS,
  RECT_INVALID = NUM_RECT_PARTS
} UENUM1BYTE(RECT_PART_TYPE);

// Uneven 4-way partition types.
enum {
  UNEVEN_4A = 0,
  UNEVEN_4B,
  NUM_UNEVEN_4WAY_PARTS,
} UENUM1BYTE(UNEVEN_4WAY_PART_TYPE);

typedef char PARTITION_CONTEXT;
#define PARTITION_PLOFFSET 4  // number of probability models per block size

#define PARTITION_BLOCK_SIZES BLOCK_SIZES
#define SQUARE_SPLIT_CONTEXTS (2 * PARTITION_PLOFFSET)

// Extended SDP is only allowed for block samples >= 64 and <= 1024. The allowed
// block size group is 64, 128, 256, 512, 1024, so the number of block size
// group is 5 in total.
#define INTER_SDP_BSIZE_GROUP 4
#define INTER_SDP_MAX_BLOCK_SIZE 64

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
  TX_4X32,            // 4x32 transform
  TX_32X4,            // 32x4 transform
  TX_8X64,            // 8x64 transform
  TX_64X8,            // 64x8 transform
  TX_4X64,            // 4x64 transform
  TX_64X4,            // 64x4 transform
  TX_SIZES_ALL,       // Includes rectangular transforms
  TX_SIZES = TX_4X8,  // Does NOT include rectangular transforms
  TX_SIZES_LARGEST = TX_64X64,
  TX_INVALID = 255  // Invalid transform size
} UENUM1BYTE(TX_SIZE);

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
//  HORZ            VERT
//  +-------+      +---+---+
//  |       |      |   |   |
//  +-------+      |   |   |
//  |       |      |   |   |
//  +-------+      +---+---+
//
//  HORZ4          VERT4
//  +-------+      +--+--+--+--+
//  |       |      |  |  |  |  |
//  +-------+      |  |  |  |  |
//  |       |      |  |  |  |  |
//  +-------+      |  |  |  |  |
//  |       |      +--+--+--+--+
//  +-------+
//  |       |
//  +-------+
//
//  HORZ5          VERT5
//  +---+---+      +--+----+--+
//  |   |   |      |  |    |  |
//  +---+---+      +--+    +--+
//  |       |      |  |    |  |
//  |       |      +--+----+--+
//  +---+---+
//  |   |   |
//  +---+---+
enum {
  TX_PARTITION_NONE,
  TX_PARTITION_SPLIT,
  TX_PARTITION_HORZ,
  TX_PARTITION_VERT,
  TX_PARTITION_HORZ4,
  TX_PARTITION_VERT4,
  TX_PARTITION_HORZ5,
  TX_PARTITION_VERT5,
  TX_PARTITION_TYPES,
  TX_PARTITION_TYPES_INTRA = TX_PARTITION_TYPES,
  TX_PARTITION_INVALID = 255
} UENUM1BYTE(TX_PARTITION_TYPE);

#define TX_PARTITION_TYPE_NUM (TX_PARTITION_TYPES - 1)
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#define TX_PARTITION_TYPE_NUM_VERT_AND_HORZ 14
#define TX_PARTITION_TYPE_NUM_VERT_OR_HORZ 3
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
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
#define MAX_TRELLIS 1024

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
  DDT_1D,
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
} UENUM2BYTE(TX_TYPE);

#if CONFIG_CORE_TX
enum {
  DCT2,
  IDT,
  DST7,
  DCT8,
  DDTX,
  FDDT,
} UENUM2BYTE(TX1D_TYPE);
#endif  // CONFIG_CORE_TX

#if CONFIG_IST_REDUCTION
#define IST_REDUCE_SET_SIZE 4  // reduced set size for IST
#if CONFIG_F105_IST_MEM_REDUCE
#define IST_REDUCE_SET_SIZE_ADST_ADST 4
#endif  // CONFIG_F105_IST_MEM_REDUCE
#endif  // CONFIG_IST_REDUCTION

#if !CONFIG_REDUCE_CCTX_CTX
#define CCTX_CONTEXTS 3
#endif  // !CONFIG_REDUCE_CCTX_CTX

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

enum { FWD_TXFM, INV_TXFM, TXFM_DIRECTIONS } UENUM1BYTE(TXFM_DIRECTION);

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
  // DCT_DCT + ADST_DCT/DCT_ADST + FLIPADST_DCT/DCT_FLIPADST + H_DCT/V_DCT
  EXT_TX_SET_LONG_SIDE_64,
  // DCT_DCT + Identity + ADST_DCT/DCT_ADST + FLIPADST_DCT/DCT_FLIPADST +
  // H_DCT/V_DCT
  EXT_TX_SET_LONG_SIDE_32,

  // Discrete Trig transforms w/o flip (4) + Identity (1)
  EXT_TX_SET_DTT4_IDTX,
  // Discrete Trig transforms w/o flip (4) + Identity (1) + 1D Hor/vert DCT (2)
  EXT_TX_SET_DTT4_IDTX_1DDCT,
  // Discrete Trig transforms w/ flip (9) + Identity (1) + 1D Hor/Ver DCT (2)
  EXT_TX_SET_DTT9_IDTX_1DDCT,
  // Discrete Trig transforms w/ flip (9) + Identity (1) + 1D Hor/Ver (6)
  EXT_TX_SET_ALL16,
  EXT_NEW_TX_SET,
  EXT_TX_SET_TYPES
} UENUM1BYTE(TxSetType);

#define EOB_TX_CTXS 3
#define EXT_TX_SIZES 4       // number of sizes that use extended transforms
#define EXT_TX_SETS_INTER 4  // Sets of transform selections for INTER
#define INTER_TX_SET1 16
#define INTER_TX_SET2 12
#define INTER_TX_SET3 2

#define EXT_TX_SETS_INTRA 3  // Sets of transform selections for INTRA
#define INTRA_TX_SET1 7
#define INTRA_TX_SET2 2

enum {
  UNIDIR_COMP_REFERENCE,
  BIDIR_COMP_REFERENCE,
  COMP_REFERENCE_TYPES,
} UENUM1BYTE(COMP_REFERENCE_TYPE);

enum { PLANE_TYPE_Y, PLANE_TYPE_UV, PLANE_TYPES } UENUM1BYTE(PLANE_TYPE);

#define CFL_ALPHABET_SIZE_LOG2 3
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
// TODO Extend work to add more support SDP-CFL latency
// reduction at transform level
#if CONFIG_SDP_CFL_LATENCY_FIX
enum {
  CFL_DISALLOWED_FOR_CHROMA,
  CFL_ALLOWED_FOR_CHROMA,
  CFL_ALLOWED_TYPES_FOR_SDP
} UENUM1BYTE(CFL_ALLOWED_FOR_SDP_TYPE);
#endif  // CONFIG_SDP_CFL_LATENCY_FIX

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

#define SEP_COMP_DRL_SIZE 3

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
  WARPMV,      // WARPMV mode (original WARPMV)
  WARP_NEWMV,  // WARP_NEWMV mode (original warp modes under NEWMV)
               // Compound ref compound modes
  NEAR_NEARMV,
  NEAR_NEWMV,
  NEW_NEARMV,
  GLOBAL_GLOBALMV,
  NEW_NEWMV,
  JOINT_NEWMV,
  NEAR_NEARMV_OPTFLOW,
  NEAR_NEWMV_OPTFLOW,
  NEW_NEARMV_OPTFLOW,
  NEW_NEWMV_OPTFLOW,
  JOINT_NEWMV_OPTFLOW,
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
  COMP_OPTFLOW_MODE_START = NEAR_NEARMV_OPTFLOW,
  INTER_COMPOUND_REF_TYPES = COMP_OPTFLOW_MODE_START - COMP_INTER_MODE_START,
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

enum {
  CFL_EXPLICIT,       // av1 cfl
  CFL_DERIVED_ALPHA,  // implicit CfL mode with derived scaling factor
  CFL_MULTI_PARAM_V,  // multi hypothesis cross component vertical prediction
  CFL_MULTI_PARAM_H,  // multi hypothesis cross component horizontal prediction
  CFL_TYPE_COUNT,     // CfL mode type count
} UENUM1BYTE(CFL_TYPE);

// Number of top model rd to store for pruning y modes in intra mode decision
#define TOP_INTRA_MODEL_COUNT 6
#define TOP_TX_PART_COUNT 4
#define TOP_INTER_TX_PART_COUNT 8
// Total number of luma intra prediction modes (include both directional and
// non-directional modes)
#define LUMA_MODE_COUNT 61

enum {
  SIMPLE_TRANSLATION,
  INTERINTRA,
  WARP_CAUSAL,  // Warp estimation from spatial MVs
  WARP_DELTA,   // Directly-signaled warp model
  WARP_EXTEND,  // Extension of an existing warp model into another block
  MOTION_MODES
} UENUM1BYTE(MOTION_MODE);

#if CONFIG_F107_GRADIENT_SIMPLIFY
#define OPFL_GRAD_UNIT_LOG2 4
#else
#define OPFL_GRAD_UNIT_LOG2 8
#endif  // CONFIG_F107_GRADIENT_SIMPLIFY

#if CONFIG_COMPOUND_WARP_CAUSAL
#define COMPOUND_WARP_LINE_BUFFER_REDUCTION 1
#endif

#define OPFL_GRAD_UNIT (1 << OPFL_GRAD_UNIT_LOG2)

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

#if CONFIG_D149_CTX_MODELING_OPT && CONFIG_COMPOUND_WARP_CAUSAL
#define NO_D149_FOR_WARP_CAUSAL 1
#else
#define NO_D149_FOR_WARP_CAUSAL 0
#endif  // CONFIG_D149_CTX_MODELING_OPT && CONFIG_COMPOUND_WARP_CAUSAL
// Total delta angles for one nominal directional mode
#define TOTAL_ANGLE_DELTA_COUNT 7

// The warpmv and warpmv_new mode is signalled as a separate flag
// So the number of remaining modes to be signalled is (SINGLE_INTER_MODE_NUM-2)
#define INTER_SINGLE_MODES (SINGLE_INTER_MODE_NUM - 2)

#define INTER_COMPOUND_MODES COMP_INTER_MODE_NUM

#define SKIP_CONTEXTS 6
#define SKIP_MODE_CONTEXTS 3

#if CONFIG_NEW_CONTEXT_MODELING
#define INTRABC_CONTEXTS 3
#endif  // CONFIG_NEW_CONTEXT_MODELING

#if CONFIG_IBC_SUBPEL_PRECISION
#define NUM_ALLOWED_BV_PRECISIONS 2
#define NUM_BV_PRECISION_CONTEXTS 1
#endif  // CONFIG_IBC_SUBPEL_PRECISION
#define COMP_GROUP_IDX_CONTEXTS 12

#define MIN_MAX_DRL_BITS 1
#define MAX_MAX_DRL_BITS 7

#if !CONFIG_OPT_INTER_MODE_CTX
#define NMV_CONTEXTS 3
#define NEWMV_MODE_CONTEXTS 6
#define GLOBALMV_MODE_CONTEXTS 2
#define REFMV_MODE_CONTEXTS 6

#define ISREFMV_MODE_CONTEXTS 2
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
#endif  // !CONFIG_OPT_INTER_MODE_CTX
#if CONFIG_WARP_EXTEND_SIMPLIFICATION
#define WARPMV_MODE_CONTEXT 5
#else
#define WARPMV_MODE_CONTEXT 8
#endif  // CONFIG_WARP_EXTEND_SIMPLIFICATION
#if CONFIG_IBC_BV_IMPROVEMENT
#define MAX_REF_BV_STACK_SIZE 4
#if CONFIG_IBC_MAX_DRL
#define MIN_MAX_IBC_DRL_BITS 1
#define MAX_MAX_IBC_DRL_BITS (MAX_REF_BV_STACK_SIZE - 1)
#endif  // CONFIG_IBC_MAX_DRL
#endif  // CONFIG_IBC_BV_IMPROVEMENT

#if CONFIG_OPFL_CTX_OPT
#define OPFL_MODE_CONTEXTS 2
#endif  // CONFIG_OPFL_CTX_OPT

#if CONFIG_OPT_INTER_MODE_CTX
#define INTER_MODE_CONTEXTS 5
#define DRL_MODE_CONTEXTS INTER_MODE_CONTEXTS

#define INTER_COMPOUND_SAME_REFS_TYPES (INTER_COMPOUND_REF_TYPES - 2)

#else
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
#endif  // CONFIG_OPT_INTER_MODE_CTX

// Explicit BAWP scaling factor counts
#define EXPLICIT_BAWP_SCALE_CNT 2
// Explicit BAWP scaling factor context counts
#define BAWP_SCALES_CTX_COUNT 3
// The allowed value range for bawp_flag
#define BAWP_OPTION_CNT 4

#define AMVD_MODE_CONTEXTS 3
#define NUM_AMVD_MODES 9

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

#if CONFIG_TIP_ENHANCEMENT
// Number of supported factors for TIP weights
#define MAX_TIP_WTD_NUM 8
#define TIP_EQUAL_WTD 8
#define TIP_SINGLE_WTD 16
#endif  // CONFIG_TIP_ENHANCEMENT

#if CONFIG_VQ_MVD_CODING
#define MAX_AMVD_INDEX 8
#endif  // CONFIG_VQ_MVD_CODING

#define DELTA_Q_SMALL 7
#define DELTA_Q_SMALL_MINUS_2 (DELTA_Q_SMALL - 2)
#define DELTA_Q_PROBS (DELTA_Q_SMALL)
#define DEFAULT_DELTA_Q_RES_PERCEPTUAL 4
#define DEFAULT_DELTA_Q_RES_OBJECTIVE 4

#define DELTA_LF_SMALL 3
#define DELTA_LF_PROBS (DELTA_LF_SMALL)
#define DEFAULT_DELTA_LF_RES 2

#define MAX_MV_REF_CANDIDATES 2
#define MAX_REF_MV_STACK_SIZE 8
#define USABLE_REF_MV_STACK_SIZE (MAX_REF_MV_STACK_SIZE)

#define REF_CAT_LEVEL 0

#define MAX_WARP_REF_CANDIDATES 4
#define WARP_REF_CONTEXTS 1

#if CONFIG_CONTEXT_DERIVATION
#define INTRA_INTER_SKIP_TXFM_CONTEXTS 2
#endif  // CONFIG_CONTEXT_DERIVATION
#define INTRA_INTER_CONTEXTS 4
#define COMP_INTER_CONTEXTS 5
#define REF_CONTEXTS 3

#define COMP_REF_TYPE_CONTEXTS 5
#define UNI_COMP_REF_CONTEXTS 3

// Group size from mapping block size to tx partition context
#define TXFM_SPLIT_GROUP 9
#define TXFM_PARTITION_GROUP 17
typedef uint16_t TXFM_CONTEXT;

typedef uint8_t INTRA_REGION_CONTEXT;

#define TIP_CONTEXTS 3

#define WARP_CAUSAL_MODE_CTX 4

#define TIP_PRED_MODES 2
#define WARP_EXTEND_CTX 3

#define INTER_REFS_PER_FRAME 7

#if CONFIG_EXTRA_DPB
// log 2 of max 8 references per-frame (7 inter + 1 intra)
// log2(INTER_REFS_PER_FRAME + 1)
#define MAX_REFS_PER_FRAME_LOG2 3
#endif  // CONFIG_EXTRA_DPB

#define REGULAR_REF_FRAMES \
  (INTER_REFS_PER_FRAME +  \
   1)  //  the original size of the decoded picture buffers
#if CONFIG_EXTRA_DPB
#define REF_FRAMES 16
#else
#define REF_FRAMES (INTER_REFS_PER_FRAME + 1)
#endif  // CONFIG_EXTRA_DPB

// NOTE: A limited number of unidirectional reference pairs can be signalled for
//       compound prediction. The use of skip mode, on the other hand, makes it
//       possible to have a reference pair not listed for explicit signaling.
#define MODE_CTX_REF_FRAMES                                \
  (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 3) / 2 + \
   2)  // additional combinations for the same reference of compound mode

// With k=INTER_REFS_PER_FRAMES, indices 0 to k-1 represent rank 1 to rank k
// references. The next k(k-1)/2 indices are left for compound reference types
// (there are k choose 2 compound combinations). Then, index for intra frame is
// defined as k+k(k-1)/2.
#define INTRA_FRAME                                    \
  (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 3) / \
   2)  // additional combinations for the same reference of compound mode
// Used for indexing into arrays that contain reference data for
// inter and intra.
#define INTRA_FRAME_INDEX INTER_REFS_PER_FRAME
#define NONE_FRAME INVALID_IDX
#define AOM_REFFRAME_ALL ((1 << INTER_REFS_PER_FRAME) - 1)

#define REF_FRAMES_LOG2 3
#define REFRESH_FRAME_ALL ((1 << REF_FRAMES) - 1)

// REF_FRAMES for the cm->ref_frame_map array, 1 scratch frame for the new
// frame in cm->cur_frame, INTER_REFS_PER_FRAME for scaled references on the
// encoder in the cpi->scaled_ref_buf array.
#define FRAME_BUFFERS (REF_FRAMES + 1 + INTER_REFS_PER_FRAME)

#define FWD_RF_OFFSET(ref) (ref - LAST_FRAME)
#define BWD_RF_OFFSET(ref) (ref - BWDREF_FRAME)

#define TOTAL_COMP_REFS (FWD_REFS * BWD_REFS + TOTAL_UNIDIR_COMP_REFS)

#define COMP_REFS (FWD_REFS * BWD_REFS + UNIDIR_COMP_REFS)

#define TIP_FRAME (MODE_CTX_REF_FRAMES - 1)
#define TIP_FRAME_INDEX (INTER_REFS_PER_FRAME + 1)
#if CONFIG_EXTRA_DPB
#define SINGLE_REF_FRAMES (INTER_REFS_PER_FRAME + 2)
#define MAX_COMPOUND_REF_INDEX (SINGLE_REF_FRAMES - 1)
#else
#define EXTREF_FRAMES (REF_FRAMES + 1)
#define SINGLE_REF_FRAMES EXTREF_FRAMES
#endif  // CONFIG_EXTRA_DPB

// Note: It includes single and compound references. So, it can take values from
// NONE_FRAME to (MODE_CTX_REF_FRAMES - 1). Hence, it is not defined as an enum.
typedef int8_t MV_REFERENCE_FRAME;

#define MAX_LR_FLEX_SWITCHABLE_BITS 3

/*!\endcond */

/*!\enum RestorationType
 * \brief This enumeration defines various restoration types supported
 */
typedef enum {
  RESTORE_NONE,          /**< No restoration */
  RESTORE_PC_WIENER,     /**< Pixel-classified Wiener restoration */
  RESTORE_WIENER_NONSEP, /**< Nonseparable Wiener restoration */
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
#define IBP_WEIGHT_MAX 256
#define IBP_WEIGHT_REF IBP_WEIGHT_MAX
typedef uint16_t IbpWeightsType;

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

/*!\endcond */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ENUMS_H_
