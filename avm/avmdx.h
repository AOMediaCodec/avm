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

/*!\defgroup avm_decoder AOMedia AVM/AV2 Decoder
 * \ingroup avm
 *
 * @{
 */
/*!\file
 * \brief Provides definitions for using AVM or AV2 within the avm Decoder
 *        interface.
 */
#ifndef AVM_AVM_AVMDX_H_
#define AVM_AVM_AVMDX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "config/avm_config.h"

/* Include controls common to both the encoder and decoder */
#include "avm/avm.h"

/*!\name Algorithm interface for AV2
 *
 * This interface provides the capability to decode AV2 streams.
 * @{
 */

/*!\brief A single instance of the AV2 decoder.
 *\deprecated This access mechanism is provided for backwards compatibility;
 * prefer avm_codec_av2_dx().
 */
extern avm_codec_iface_t avm_codec_av2_dx_algo;
/*!\brief The interface to the AV2 decoder.
 */
extern avm_codec_iface_t *avm_codec_av2_dx(void);

/*!@} - end algorithm interface member group */

/** Data structure that stores bit accounting for debug
 */
typedef struct Accounting Accounting;

#ifndef AVM_INSPECTION_H_
/** Callback that inspects decoder frame data.
 */
typedef void (*avm_inspect_cb)(void *decoder, void *ctx);

#endif

/*!\brief Structure to hold inspection callback and context.
 *
 * Defines a structure to hold the inspection callback function and calling
 * context.
 */
typedef struct avm_inspect_init {
  /*! Inspection callback (per frame). */
  avm_inspect_cb inspect_cb;
  /*! Inspection callback (per superblock). */
  avm_inspect_cb inspect_sb_cb;
  /*! Inspection callback (per TIP frame). */
  avm_inspect_cb inspect_tip_cb;

  /*! Inspection context. */
  void *inspect_ctx;
} avm_inspect_init;

/*!\brief Structure to collect a buffer index when inspecting.
 *
 * Defines a structure to hold the buffer and return an index
 * when calling decode from inspect. This enables us to decode
 * non showable sub frames.
 */
typedef struct {
  /*! Pointer for new position in compressed buffer after decoding 1 OBU. */
  const unsigned char *buf;
  /*! Index into reference buffer array to see result of decoding 1 OBU. */
  int idx;
} Av2DecodeReturn;

/*!\brief Max number of tile columns
 *
 * This is the limit of number of tile columns allowed within a frame.
 *
 * Currently same as "MAX_TILE_COLS" in AV2, the maximum that AV2 supports.
 *
 */
#define AVM_MAX_TILE_COLS 64
/*!\brief Max number of tile rows
 *
 * This is the limit of number of tile rows allowed within a frame.
 *
 * Currently same as "MAX_TILE_ROWS" in AV2, the maximum that AV2 supports.
 *
 */
#define AVM_MAX_TILE_ROWS 64

/*!\brief Structure to hold information about tiles in a frame.
 *
 * Defines a structure to hold a frame's tile information, namely
 * number of tile columns, number of tile_rows, and the width and
 * height of each tile.
 */
typedef struct avm_tile_info {
  /*! Indicates the number of tile columns. */
  int tile_columns;
  /*! Indicates the number of tile rows. */
  int tile_rows;
  /*! Indicates the tile widths in units of SB. */
  int tile_widths[AVM_MAX_TILE_COLS];
  /*! Indicates the tile heights in units of SB. */
  int tile_heights[AVM_MAX_TILE_ROWS];
  /*! Indicates the number of tile groups present in a frame. */
  int num_tile_groups;
} avm_tile_info;

/*!\brief Structure to hold information about still image coding.
 *
 * Defines a structure to hold a information regarding still picture
 * and its header type.
 */
typedef struct avm_still_picture_info {
  /*! Video is a single frame still picture */
  int is_still_picture;
  /*! Use reduced header for still picture */
  int is_single_picture_header_flag;
} avm_still_picture_info;

/*!\brief Structure to hold information about S_FRAME.
 *
 * Defines a structure to hold a information regarding S_FRAME
 * and its position.
 */
typedef struct avm_s_frame_info {
  /*! Indicates if current frame is S_FRAME */
  int is_s_frame;
  /*! Indicates if current S_FRAME is present at ALTREF frame*/
  int is_s_frame_at_altref;
} avm_s_frame_info;

/*!\brief Structure to hold information about screen content tools.
 *
 * Defines a structure to hold information about screen content
 * tools, namely: allow_screen_content_tools, allow_intrabc, and
 * force_integer_mv.
 */
typedef struct avm_screen_content_tools_info {
  /*! Are screen content tools allowed */
  int allow_screen_content_tools;
  /*! Is intrabc allowed */
  int allow_intrabc;
  /*! Is integer mv forced */
  int force_integer_mv;
} avm_screen_content_tools_info;

/*!\brief Structure to hold the external reference frame pointer.
 *
 * Define a structure to hold the external reference frame pointer.
 */
typedef struct av2_ext_ref_frame {
  /*! Start pointer of external references. */
  avm_image_t *img;
  /*! Number of available external references. */
  int num;
} av2_ext_ref_frame_t;

/*!\enum avm_dec_control_id
 * \brief AVM decoder control functions
 *
 * This set of macros define the control functions available for the AVM
 * decoder interface.
 *
 * \sa #avm_codec_control(avm_codec_ctx_t *ctx, int ctrl_id, ...)
 */
enum avm_dec_control_id {
  /*!\brief Codec control function to get info on which reference frames were
   * updated by the last decode, int* parameter
   */
  AVMD_GET_LAST_REF_UPDATES = AVM_DECODER_CTRL_ID_START,

  /*!\brief Codec control function to check if the indicated frame is
    corrupted, int* parameter
  */
  AVMD_GET_FRAME_CORRUPTED,

  /*!\brief Codec control function to get info on which reference frames were
   * used by the last decode, int* parameter
   */
  AVMD_GET_LAST_REF_USED,

  /*!\brief Codec control function to get the dimensions that the current
   * frame is decoded at, int* parameter. This may be different to the
   * intended display size for the frame as specified in the wrapper or frame
   * header (see AV2D_GET_DISPLAY_SIZE).
   */
  AV2D_GET_FRAME_SIZE,

  /*!\brief Codec control function to get the current frame's intended display
   * dimensions (as specified in the wrapper or frame header), int* parameter.
   * This may be different to the decoded dimensions of this frame (see
   * AV2D_GET_FRAME_SIZE).
   */
  AV2D_GET_DISPLAY_SIZE,

  /*!\brief Codec control function to get the bit depth of the stream,
   * unsigned int* parameter
   */
  AV2D_GET_BIT_DEPTH,

  /*!\brief Codec control function to get the image format of the stream,
   * avm_img_fmt_t* parameter
   */
  AV2D_GET_IMG_FORMAT,

  /*!\brief Codec control function to get the size of the tile, unsigned int
    parameter */
  AV2D_GET_TILE_SIZE,

  /*!\brief Codec control function to get the tile count in a tile list, int*
   * parameter
   */
  AV2D_GET_TILE_COUNT,

  /*!\brief Codec control function to set the byte alignment of the planes in
   * the reference buffers, int parameter
   *
   * Valid values are power of 2, from 32 to 1024. A value of 0 sets
   * legacy alignment. I.e. Y plane is aligned to 32 bytes, U plane directly
   * follows Y plane, and V plane directly follows U plane. Default value is 0.
   */
  AV2_SET_BYTE_ALIGNMENT,

  /*!\brief Codec control function to invert the decoding order to from right to
   * left, int parameter
   *
   * The function is used in a test to confirm the decoding independence of tile
   * columns. The function may be used in application where this order
   * of decoding is desired. int parameter
   *
   * TODO(yaowu): Rework the unit test that uses this control, and in a future
   *              release, this test-only control shall be removed.
   */
  AV2_INVERT_TILE_DECODE_ORDER,

  /*!\brief Codec control function to set the skip loop filter flag, int
   * parameter
   *
   * Valid values are integers. The decoder will skip the loop filter
   * when its value is set to nonzero. If the loop filter is skipped the
   * decoder may accumulate decode artifacts. The default value is 0.
   */
  AV2_SET_SKIP_LOOP_FILTER,

  /*!\brief Codec control function to retrieve a pointer to the Accounting
   * struct, takes Accounting** as parameter
   *
   * If called before a frame has been decoded, this returns AVM_CODEC_ERROR.
   * The caller should ensure that AVM_CODEC_OK is returned before attempting
   * to dereference the Accounting pointer.
   *
   * \attention When compiled without --enable-accounting, this returns
   * AVM_CODEC_INCAPABLE.
   */
  AV2_GET_ACCOUNTING,

  /*!\brief Codec control function to get last decoded frame quantizer,
   * int* parameter
   *
   * Returned value uses internal quantizer scale defined by the codec.
   */
  AVMD_GET_LAST_QUANTIZER,

  /*!\brief Codec control function to enable the row based multi-threading of
   * decoding, unsigned int parameter
   *
   * - 0 = disabled
   * - 1 = enabled (default)
   */
  AV2D_SET_ROW_MT,

  /*!\brief Codec control function to indicate which operating point to use,
   * int parameter
   *
   * A scalable stream may define multiple operating points, each of which
   * defines a set of temporal and spatial layers to be processed. The
   * operating point index may take a value between 0 and
   * operating_points_cnt_minus_1 (which is at most 31).
   */
  AV2D_SET_OPERATING_POINT,

  /*!\brief Codec control function to indicate whether to output one frame per
   * temporal unit (the default), or one frame per spatial layer. int parameter
   *
   * In a scalable stream, each temporal unit corresponds to a single "frame"
   * of video, and within a temporal unit there may be multiple spatial layers
   * with different versions of that frame.
   * For video playback, only the highest-quality version (within the
   * selected operating point) is needed, but for some use cases it is useful
   * to have access to multiple versions of a frame when they are available.
   */
  AV2D_SET_OUTPUT_ALL_LAYERS,

  /*!\brief Codec control function to set an avm_inspect_cb callback that is
   * invoked each time a frame is decoded, avm_inspect_init* parameter
   *
   * \attention When compiled without --enable-inspection, this
   * returns AVM_CODEC_INCAPABLE.
   */
  AV2_SET_INSPECTION_CALLBACK,

  /*!\brief Codec control function to set the skip film grain flag, int
   * parameter
   *
   * Valid values are integers. The decoder will skip the film grain when its
   * value is set to nonzero. The default value is 0.
   */
  AV2D_SET_SKIP_FILM_GRAIN,

  AV2D_SET_RANDOM_ACCESS,

  AV2D_SET_BRU_OPT_MODE,

  AVM_DECODER_CTRL_ID_MAX,

  /*!\brief Codec control function to check the presence of forward key frames
   */
  AVMD_GET_FWD_KF_PRESENT,

  /*!\brief Codec control function to get the frame flags of the previous frame
   * decoded. This will return a flag of type avm_codec_frame_flags_t.
   */
  AVMD_GET_FRAME_FLAGS,

  /*!\brief Codec control function to check the presence of altref frames */
  AVMD_GET_ALTREF_PRESENT,

  /*!\brief Codec control function to get tile information of the previous frame
   * decoded. This will return a struct of type avm_tile_info.
   */
  AVMD_GET_TILE_INFO,

  /*!\brief Codec control function to get screen content tools information.
   * It returns a struct of type avm_screen_content_tools_info, which contains
   * the header flags allow_screen_content_tools, allow_intrabc, and
   * force_integer_mv.
   */
  AVMD_GET_SCREEN_CONTENT_TOOLS_INFO,

  /*!\brief Codec control function to get the still picture coding information
   */
  AVMD_GET_STILL_PICTURE,

  /*!\brief Codec control function to get superblock size.
   * It returns an integer, indicating the superblock size
   * read from the sequence header(0 for BLOCK_64X64 and
   * 1 for BLOCK_128X128)
   */
  AVMD_GET_SB_SIZE,

  /*!\brief Codec control function to check if the previous frame
   * decoded has show existing frame flag set.
   */
  AVMD_GET_SHOW_EXISTING_FRAME_FLAG,

  /*!\brief Codec control function to get the S_FRAME coding information
   */
  AVMD_GET_S_FRAME_INFO,

  /*!\brief Codec control function to get the frame information
   */
  AVMD_GET_FRAME_INFO,

  /*!\brief Codec control function to enable subgop stats
   */
  AV2D_ENABLE_SUBGOP_STATS,

  /*!\brief Codec control function to advance output_frames_offset by given step
   */
  AVMD_INCR_OUTPUT_FRAMES_OFFSET,
};

/*!\cond */
/*!\brief AVM decoder control function parameter type
 *
 * Defines the data types that AVMD control functions take.
 *
 * \note Additional common controls are defined in avm.h.
 *
 * \note For each control ID "X", a macro-define of
 * AVM_CTRL_X is provided. It is used at compile time to determine
 * if the control ID is supported by the libavm library available,
 * when the libavm version cannot be controlled.
 */
AVM_CTRL_USE_TYPE(AVMD_GET_LAST_REF_UPDATES, int *)
#define AVM_CTRL_AVMD_GET_LAST_REF_UPDATES

AVM_CTRL_USE_TYPE(AVMD_GET_FRAME_CORRUPTED, int *)
#define AVM_CTRL_AVMD_GET_FRAME_CORRUPTED

AVM_CTRL_USE_TYPE(AVMD_GET_LAST_REF_USED, int *)
#define AVM_CTRL_AVMD_GET_LAST_REF_USED

AVM_CTRL_USE_TYPE(AVMD_GET_LAST_QUANTIZER, int *)
#define AVM_CTRL_AVMD_GET_LAST_QUANTIZER

AVM_CTRL_USE_TYPE(AVMD_GET_FWD_KF_PRESENT, int *)
#define AVM_CTRL_AVMD_GET_FWD_KF_PRESENT

AVM_CTRL_USE_TYPE(AVMD_GET_ALTREF_PRESENT, int *)
#define AVM_CTRL_AVMD_GET_ALTREF_PRESENT

AVM_CTRL_USE_TYPE(AVMD_GET_FRAME_FLAGS, int *)
#define AVM_CTRL_AVMD_GET_FRAME_FLAGS

AVM_CTRL_USE_TYPE(AVMD_GET_TILE_INFO, avm_tile_info *)
#define AVM_CTRL_AVMD_GET_TILE_INFO

AVM_CTRL_USE_TYPE(AVMD_GET_SCREEN_CONTENT_TOOLS_INFO,
                  avm_screen_content_tools_info *)
#define AVM_CTRL_AVMD_GET_SCREEN_CONTENT_TOOLS_INFO

AVM_CTRL_USE_TYPE(AVMD_GET_STILL_PICTURE, avm_still_picture_info *)
#define AVM_CTRL_AVMD_GET_STILL_PICTURE

AVM_CTRL_USE_TYPE(AVMD_GET_SB_SIZE, avm_superblock_size_t *)
#define AVMD_CTRL_AVMD_GET_SB_SIZE

AVM_CTRL_USE_TYPE(AVMD_GET_SHOW_EXISTING_FRAME_FLAG, int *)
#define AVMD_CTRL_AVMD_GET_SHOW_EXISTING_FRAME_FLAG

AVM_CTRL_USE_TYPE(AVMD_GET_S_FRAME_INFO, avm_s_frame_info *)
#define AVMD_CTRL_AVMD_GET_S_FRAME_INFO

AVM_CTRL_USE_TYPE(AVMD_GET_FRAME_INFO, void *)
#define AVMD_CTRL_AVMD_GET_FRAME_INFO

AVM_CTRL_USE_TYPE(AVMD_INCR_OUTPUT_FRAMES_OFFSET, int)
#define AVMD_CTRL_AVMD_INCR_OUTPUT_FRAMES_OFFSET

AVM_CTRL_USE_TYPE(AV2D_ENABLE_SUBGOP_STATS, unsigned int)
#define AVMD_CTRL_AV2D_ENABLE_SUBGOP_STATS

AVM_CTRL_USE_TYPE(AV2D_GET_DISPLAY_SIZE, int *)
#define AVM_CTRL_AV2D_GET_DISPLAY_SIZE

AVM_CTRL_USE_TYPE(AV2D_GET_BIT_DEPTH, unsigned int *)
#define AVM_CTRL_AV2D_GET_BIT_DEPTH

AVM_CTRL_USE_TYPE(AV2D_GET_IMG_FORMAT, avm_img_fmt_t *)
#define AVM_CTRL_AV2D_GET_IMG_FORMAT

AVM_CTRL_USE_TYPE(AV2D_GET_TILE_SIZE, unsigned int *)
#define AVM_CTRL_AV2D_GET_TILE_SIZE

AVM_CTRL_USE_TYPE(AV2D_GET_TILE_COUNT, unsigned int *)
#define AVM_CTRL_AV2D_GET_TILE_COUNT

AVM_CTRL_USE_TYPE(AV2D_GET_FRAME_SIZE, int *)
#define AVM_CTRL_AV2D_GET_FRAME_SIZE

AVM_CTRL_USE_TYPE(AV2_INVERT_TILE_DECODE_ORDER, int)
#define AVM_CTRL_AV2_INVERT_TILE_DECODE_ORDER

AVM_CTRL_USE_TYPE(AV2_GET_ACCOUNTING, Accounting **)
#define AVM_CTRL_AV2_GET_ACCOUNTING

AVM_CTRL_USE_TYPE(AV2D_SET_ROW_MT, unsigned int)
#define AVM_CTRL_AV2D_SET_ROW_MT

AVM_CTRL_USE_TYPE(AV2D_SET_SKIP_FILM_GRAIN, int)
#define AVM_CTRL_AV2D_SET_SKIP_FILM_GRAIN

AVM_CTRL_USE_TYPE(AV2D_SET_RANDOM_ACCESS, int)
#define AVM_CTRL_AV2D_SET_RANDOM_ACCESS

AVM_CTRL_USE_TYPE(AV2D_SET_BRU_OPT_MODE, int)
#define AVM_CTRL_AV2D_SET_BRU_OPT_MODE

AVM_CTRL_USE_TYPE(AV2D_SET_OPERATING_POINT, int)
#define AVM_CTRL_AV2D_SET_OPERATING_POINT

AVM_CTRL_USE_TYPE(AV2D_SET_OUTPUT_ALL_LAYERS, int)
#define AVM_CTRL_AV2D_SET_OUTPUT_ALL_LAYERS

AVM_CTRL_USE_TYPE(AV2_SET_INSPECTION_CALLBACK, avm_inspect_init *)
#define AVM_CTRL_AV2_SET_INSPECTION_CALLBACK
/*!\endcond */
/*! @} - end defgroup avm_decoder */
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_AVMDX_H_
