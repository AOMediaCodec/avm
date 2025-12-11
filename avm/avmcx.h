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
#ifndef AVM_AVM_AVMCX_H_
#define AVM_AVM_AVMCX_H_

/*!\defgroup avm_encoder AOMedia AVM/AV2 Encoder
 * \ingroup avm
 *
 * @{
 */
#include "avm/avm.h"
#include "avm/avm_encoder.h"

/*!\file
 * \brief Provides definitions for using AVM or AV2 encoder algorithm within the
 *        avm Codec Interface.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*!\name Algorithm interface for AV2
 *
 * This interface provides the capability to encode raw AV2 streams.
 *@{
 */

/*!\brief A single instance of the AV2 encoder.
 *\deprecated This access mechanism is provided for backwards compatibility;
 * prefer avm_codec_av2_cx().
 */
extern avm_codec_iface_t avm_codec_av2_cx_algo;

/*!\brief The interface to the AV2 encoder.
 */
extern avm_codec_iface_t *avm_codec_av2_cx(void);
/*!@} - end algorithm interface member group */

/*
 * Algorithm Flags
 */

/*!\brief Don't reference the last frame
 *
 * When this flag is set, the encoder will not use the last frame as a
 * predictor. When not set, the encoder will choose whether to use the
 * last frame or not automatically.
 */
#define AVM_EFLAG_NO_REF_LAST (1 << 16)
/*!\brief Don't reference the last2 frame
 *
 * When this flag is set, the encoder will not use the last2 frame as a
 * predictor. When not set, the encoder will choose whether to use the
 * last2 frame or not automatically.
 */
#define AVM_EFLAG_NO_REF_LAST2 (1 << 17)
/*!\brief Don't reference the last3 frame
 *
 * When this flag is set, the encoder will not use the last3 frame as a
 * predictor. When not set, the encoder will choose whether to use the
 * last3 frame or not automatically.
 */
#define AVM_EFLAG_NO_REF_LAST3 (1 << 18)
/*!\brief Don't reference the golden frame
 *
 * When this flag is set, the encoder will not use the golden frame as a
 * predictor. When not set, the encoder will choose whether to use the
 * golden frame or not automatically.
 */
#define AVM_EFLAG_NO_REF_GF (1 << 19)

/*!\brief Don't reference the alternate reference frame
 *
 * When this flag is set, the encoder will not use the alt ref frame as a
 * predictor. When not set, the encoder will choose whether to use the
 * alt ref frame or not automatically.
 */
#define AVM_EFLAG_NO_REF_ARF (1 << 20)
/*!\brief Don't reference the bwd reference frame
 *
 * When this flag is set, the encoder will not use the bwd ref frame as a
 * predictor. When not set, the encoder will choose whether to use the
 * bwd ref frame or not automatically.
 */
#define AVM_EFLAG_NO_REF_BWD (1 << 21)
/*!\brief Don't reference the alt2 reference frame
 *
 * When this flag is set, the encoder will not use the alt2 ref frame as a
 * predictor. When not set, the encoder will choose whether to use the
 * alt2 ref frame or not automatically.
 */
#define AVM_EFLAG_NO_REF_ARF2 (1 << 22)

/*!\brief Don't update reference frames
 *
 * When this flag is set, the encoder will not update all the ref frames with
 * the contents of the current frame.
 */
#define AVM_EFLAG_NO_UPD_ALL (1 << 23)

#if !CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
/*!\brief Disable entropy update
 *
 * When this flag is set, the encoder will not update its internal entropy
 * model based on the entropy of this frame.
 */
#define AVM_EFLAG_NO_UPD_ENTROPY (1 << 26)
#endif  // !CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
/*!\brief Disable ref frame mvs
 *
 * When this flag is set, the encoder will not allow frames to
 * be encoded using mfmv.
 */
#define AVM_EFLAG_NO_REF_FRAME_MVS (1 << 27)
/*!\brief Enable error resilient frame
 *
 * When this flag is set, the encoder will code frames as error
 * resilient.
 */
#define AVM_EFLAG_ERROR_RESILIENT (1 << 28)
/*!\brief Enable s frame mode
 *
 * When this flag is set, the encoder will code frames as an
 * s frame.
 */
#define AVM_EFLAG_SET_S_FRAME (1 << 29)
/*!\brief Force primary_ref_frame to PRIMARY_REF_NONE
 *
 * When this flag is set, the encoder will set a frame's primary_ref_frame
 * to PRIMARY_REF_NONE
 */
#define AVM_EFLAG_SET_PRIMARY_REF_NONE (1 << 30)

/*!\brief AVx encoder control functions
 *
 * This set of macros define the control functions available for AVx
 * encoder interface.
 *
 * \sa #avm_codec_control(avm_codec_ctx_t *ctx, int ctrl_id, ...)
 */
enum avme_enc_control_id {
  /*!\brief Codec control function to set which reference frame encoder can use,
   * int parameter.
   */
  AVME_USE_REFERENCE = 7,

  /*!\brief Codec control function to pass an ROI map to encoder, avm_roi_map_t*
   * parameter.
   */
  AVME_SET_ROI_MAP = 8,

  /*!\brief Codec control function to pass an Active map to encoder,
   * avm_active_map_t* parameter.
   */
  AVME_SET_ACTIVEMAP = 9,

  /* NOTE: enum 10 unused */

  /*!\brief Codec control function to set encoder scaling mode,
   * avm_scaling_mode_t* parameter.
   */
  AVME_SET_SCALEMODE = 11,

  /*!\brief Codec control function to set encoder embedded layer id, unsigned
   * int parameter.
   */
  AVME_SET_MLAYER_ID = 12,

  /*!\brief Codec control function to set encoder internal speed settings,
   * int parameter
   *
   * Changes in this value influences the complexity of algorithms used in
   * encoding process, values greater than 0 will increase encoder speed at
   * the expense of quality.
   *
   * Valid range: 0..9. 0 runs the slowest, and 9 runs the fastest;
   * quality improves as speed decreases (since more compression
   * possibilities are explored).
   *
   * Note: AVM_USAGE_GOOD_QUALITY treats 7..9 the same as 6.
   */
  AVME_SET_CPUUSED = 13,

  /*!\brief Codec control function to enable automatic set and use alf frames,
   * unsigned int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AVME_SET_ENABLEAUTOALTREF = 14,

  /* NOTE: enum 15 unused */

  /*!\brief Codec control function to set sharpness, unsigned int parameter.
   */
  AVME_SET_SHARPNESS = AVME_SET_ENABLEAUTOALTREF + 2,  // 16

  /*!\brief Codec control function to set the threshold for MBs treated static,
   * unsigned int parameter
   */
  AVME_SET_STATIC_THRESHOLD = 17,

  /* NOTE: enum 18 unused */

  /*!\brief Codec control function to get last quantizer chosen by the encoder,
   * int* parameter
   *
   * Return value uses internal quantizer scale defined by the codec.
   */
  AVME_GET_LAST_QUANTIZER = AVME_SET_STATIC_THRESHOLD + 2,  // 19

  /*!\brief Codec control function to set the max no of frames to create arf,
   * unsigned int parameter
   */
  AVME_SET_ARNR_MAXFRAMES = 21,

  /*!\brief Codec control function to set the filter strength for the arf,
   * unsigned int parameter
   */
  AVME_SET_ARNR_STRENGTH = 22,

  /* NOTE: enum 23 unused */

  /*!\brief Codec control function to set visual tuning, avm_tune_metric (int)
   * parameter
   */
  AVME_SET_TUNING = AVME_SET_ARNR_STRENGTH + 2,  // 24

  /*!\brief Codec control function to set constrained / constant quality level,
   * unsigned int parameter
   *
   * Valid range: 0..255
   *
   * \attention For this value to be used avm_codec_enc_cfg_t::rc_end_usage
   *            must be set to #AVM_CQ or #AVM_Q.
   */
  AVME_SET_QP = 25,

  /*!\brief Codec control function to set max data rate for intra frames,
   * unsigned int parameter
   *
   * This value controls additional clamping on the maximum size of a
   * keyframe. It is expressed as a percentage of the average
   * per-frame bitrate, with the special (and default) value 0 meaning
   * unlimited, or no additional clamping beyond the codec's built-in
   * algorithm.
   *
   * For example, to allocate no more than 4.5 frames worth of bitrate
   * to a keyframe, set this to 450.
   */
  AVME_SET_MAX_INTRA_BITRATE_PCT = 26,

  /*!\brief Codec control function to set number of embedded layers, int
   * parameter
   */
  AVME_SET_NUMBER_MLAYERS = 27,

  /*!\brief Codec control function to set max data rate for inter frames,
   * unsigned int parameter
   *
   * This value controls additional clamping on the maximum size of an
   * inter frame. It is expressed as a percentage of the average
   * per-frame bitrate, with the special (and default) value 0 meaning
   * unlimited, or no additional clamping beyond the codec's built-in
   * algorithm.
   *
   * For example, to allow no more than 4.5 frames worth of bitrate
   * to an inter frame, set this to 450.
   */
  AV2E_SET_MAX_INTER_BITRATE_PCT = AVME_SET_MAX_INTRA_BITRATE_PCT + 2,  // 28

  /*!\brief Boost percentage for Golden Frame in CBR mode, unsigned int
   * parameter
   *
   * This value controls the amount of boost given to Golden Frame in
   * CBR mode. It is expressed as a percentage of the average
   * per-frame bitrate, with the special (and default) value 0 meaning
   * the feature is off, i.e., no golden frame boost in CBR mode and
   * average bitrate target is used.
   *
   * For example, to allow 100% more bits, i.e, 2X, in a golden frame
   * than average frame, set this to 100.
   */
  AV2E_SET_GF_CBR_BOOST_PCT = 29,

  /* NOTE: enum 30 unused */

  /*!\brief Codec control function to set lossless encoding mode, unsigned int
   * parameter
   *
   * AV2 can operate in lossless encoding mode, in which the bitstream
   * produced will be able to decode and reconstruct a perfect copy of
   * input source.
   *
   * - 0 = normal coding mode, may be lossy (default)
   * - 1 = lossless coding mode
   */
  AV2E_SET_LOSSLESS = AV2E_SET_GF_CBR_BOOST_PCT + 2,  // 31

  /*!\brief Codec control function to enable the row based multi-threading
   * of the encoder, unsigned int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ROW_MT = 32,

  /*!\brief Codec control function to set number of tile columns. unsigned int
   * parameter
   *
   * In encoding and decoding, AV2 allows an input image frame be partitioned
   * into separate vertical tile columns, which can be encoded or decoded
   * independently. This enables easy implementation of parallel encoding and
   * decoding. The parameter for this control describes the number of tile
   * columns (in log2 units), which has a valid range of [0, 6]:
   * \verbatim
                 0 = 1 tile column
                 1 = 2 tile columns
                 2 = 4 tile columns
                 .....
                 n = 2**n tile columns
     \endverbatim
   * By default, the value is 0, i.e. one single column tile for entire image.
   */
  AV2E_SET_TILE_COLUMNS = 33,

  /*!\brief Codec control function to set number of tile rows, unsigned int
   * parameter
   *
   * In encoding and decoding, AV2 allows an input image frame be partitioned
   * into separate horizontal tile rows, which can be encoded or decoded
   * independently. The parameter for this control describes the number of tile
   * rows (in log2 units), which has a valid range of [0, 6]:
   * \verbatim
                0 = 1 tile row
                1 = 2 tile rows
                2 = 4 tile rows
                .....
                n = 2**n tile rows
   \endverbatim
   * By default, the value is 0, i.e. one single row tile for entire image.
   */
  AV2E_SET_TILE_ROWS = 34,

  /*!\brief Codec control function to enable RDO modulated by frame temporal
   * dependency, unsigned int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_TPL_MODEL = 35,

  /*!\brief Codec control function to enable temporal filtering on key frame,
   * unsigned int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_KEYFRAME_FILTERING = 36,

  /*!\brief Codec control function to enable frame parallel decoding feature,
   * unsigned int parameter
   *
   * AV2 has a bitstream feature to reduce decoding dependency between frames
   * by turning off backward update of probability context used in encoding
   * and decoding. This allows staged parallel processing of more than one
   * video frames in the decoder. This control function provides a mean to
   * turn this feature on or off for bitstreams produced by encoder.
   *
   * - 0 = disable (default)
   * - 1 = enable
   */
  AV2E_SET_FRAME_PARALLEL_DECODING = 37,

  /*!\brief Codec control function to enable s_frame_mode, int parameter
   *
   * AV2 has a bitstream feature to designate certain frames as S-frames,
   * from where we can switch to a different stream,
   * even though the reference buffers may not be exactly identical.
   *
   * - 0 = disable (default)
   * - 1 = enable
   */
  AV2E_SET_S_FRAME_MODE = 39,

  /*!\brief Codec control function to set adaptive quantization mode, unsigned
   * int parameter
   *
   * AV2 has a segment based feature that allows encoder to adaptively change
   * quantization parameter for each segment within a frame to improve the
   * subjective quality. This control makes encoder operate in one of the
   * several AQ_modes supported.
   *
   * - 0 = disable (default)
   * - 1 = enable
   */
  AV2E_SET_AQ_MODE = 40,

  /*!\brief Codec control function to enable/disable periodic Q boost, unsigned
   * int parameter
   *
   * One AV2 encoder speed feature is to enable quality boost by lowering
   * frame level Q periodically. This control function provides a mean to
   * turn on/off this feature.
   *
   * - 0 = disable (default)
   * - 1 = enable
   */
  AV2E_SET_FRAME_PERIODIC_BOOST = 41,

  /*!\brief Codec control function to set noise sensitivity, unsigned int
   * parameter
   *
   * - 0 = disable (default)
   * - 1 = enable (Y only)
   */
  AV2E_SET_NOISE_SENSITIVITY = 42,

  /*!\brief Codec control function to set content type, avm_tune_content
   * parameter
   *
   *  - AVM_CONTENT_DEFAULT = Regular video content (default)
   *  - AVM_CONTENT_SCREEN  = Screen capture content
   */
  AV2E_SET_TUNE_CONTENT = 43,

  /*!\brief Codec control function to set CDF update mode, unsigned int
   * parameter
   *
   *  - 0: no update
   *  - 1: update on every frame (default)
   *  - 2: selectively update
   */
  AV2E_SET_CDF_UPDATE_MODE = 44,

  /*!\brief Codec control function to set color space info, int parameter
   *
   *  - 0 = For future use
   *  - 1 = BT.709
   *  - 2 = Unspecified (default)
   *  - 3 = For future use
   *  - 4 = BT.470 System M (historical)
   *  - 5 = BT.470 System B, G (historical)
   *  - 6 = BT.601
   *  - 7 = SMPTE 240
   *  - 8 = Generic film (color filters using illuminant C)
   *  - 9 = BT.2020, BT.2100
   *  - 10 = SMPTE 428 (CIE 1921 XYZ)
   *  - 11 = SMPTE RP 431-2
   *  - 12 = SMPTE EG 432-1
   *  - 13..21 = For future use
   *  - 22 = EBU Tech. 3213-E
   *  - 23 = For future use
   */
  AV2E_SET_COLOR_PRIMARIES = 45,

  /*!\brief Codec control function to set transfer function info, int parameter
   *
   * - 0 = For future use
   * - 1 = BT.709
   * - 2 = Unspecified (default)
   * - 3 = For future use
   * - 4 = BT.470 System M (historical)
   * - 5 = BT.470 System B, G (historical)
   * - 6 = BT.601
   * - 7 = SMPTE 240 M
   * - 8 = Linear
   * - 9 = Logarithmic (100 : 1 range)
   * - 10 = Logarithmic (100 * Sqrt(10) : 1 range)
   * - 11 = IEC 61966-2-4
   * - 12 = BT.1361
   * - 13 = sRGB or sYCC
   * - 14 = BT.2020 10-bit systems
   * - 15 = BT.2020 12-bit systems
   * - 16 = SMPTE ST 2084, ITU BT.2100 PQ
   * - 17 = SMPTE ST 428
   * - 18 = BT.2100 HLG, ARIB STD-B67
   * - 19 = For future use
   */
  AV2E_SET_TRANSFER_CHARACTERISTICS = 46,

  /*!\brief Codec control function to set transfer function info, int parameter
   *
   * - 0 = Identity matrix
   * - 1 = BT.709
   * - 2 = Unspecified (default)
   * - 3 = For future use
   * - 4 = US FCC 73.628
   * - 5 = BT.470 System B, G (historical)
   * - 6 = BT.601
   * - 7 = SMPTE 240 M
   * - 8 = YCgCo
   * - 9 = BT.2020 non-constant luminance, BT.2100 YCbCr
   * - 10 = BT.2020 constant luminance
   * - 11 = SMPTE ST 2085 YDzDx
   * - 12 = Chromaticity-derived non-constant luminance
   * - 13 = Chromaticity-derived constant luminance
   * - 14 = BT.2100 ICtCp
   * - 15 = For future use
   */
  AV2E_SET_MATRIX_COEFFICIENTS = 47,

  /*!\brief Codec control function to set chroma 4:2:2 or 4:2:0 sample position
   * info, avm_chroma_sample_position_t parameter
   *
   * AVM_CSP_UNSPECIFIED is default
   */
  AV2E_SET_CHROMA_SAMPLE_POSITION = 48,

  /*!\brief Codec control function to set minimum interval between GF/ARF
   * frames, unsigned int parameter
   *
   * By default the value is set as 4.
   */
  AV2E_SET_MIN_GF_INTERVAL = 49,

  /*!\brief Codec control function to set minimum interval between GF/ARF
   * frames, unsigned int parameter
   *
   * By default the value is set as 16.
   */
  AV2E_SET_MAX_GF_INTERVAL = 50,

  /*!\brief Codec control function to get an active map back from the encoder,
    avm_active_map_t* parameter
   */
  AV2E_GET_ACTIVEMAP = 51,

  /*!\brief Codec control function to set color range bit, int parameter
   *
   * - 0 = Limited range, 16..235 or HBD equivalent (default)
   * - 1 = Full range, 0..255 or HBD equivalent
   */
  AV2E_SET_COLOR_RANGE = 52,
  /*!\brief Control to set target sequence level index for a certain operating
   * point(OP), int parameter
   * Possible values are in the form of "ABxy"(pad leading zeros if less than
   * 4 digits).
   *  - AB: OP index.
   *  - xy: Target level index for the OP. Can be values 0~23(corresponding to
   *    level 2.0 ~ 7.3) or 24(keep level stats only for level monitoring) or
   *    31(maximum level parameter, no level-based constraints).
   *
   * E.g.:
   * - "0" means target level index 0 for the 0th OP;
   * - "111" means target level index 11 for the 1st OP;
   * - "1021" means target level index 21 for the 10th OP.
   *
   * If the target level is not specified for an OP, the maximum level parameter
   * of 31 is used as default.
   */
  AV2E_SET_TARGET_SEQ_LEVEL_IDX = 54,

  /*!\brief Codec control function to get sequence level index for each
   * operating point. int* parameter. There can be at most 32 operating points.
   * The results will be written into a provided integer array of sufficient
   * size.
   */
  AV2E_GET_SEQ_LEVEL_IDX = 55,

  /*!\brief Codec control function to set intended superblock size, unsigned int
   * parameter
   *
   * By default, the superblock size is determined separately for each
   * frame by the encoder.
   */
  AV2E_SET_SUPERBLOCK_SIZE = 56,

  /*!\brief Codec control function to enable automatic set and use of
   * bwd-pred frames, unsigned int parameter
   *
   * - 0 = disable (default)
   * - 1 = enable
   */
  AVME_SET_ENABLEAUTOBWDREF = 57,

  /*!\brief Codec control function to encode with CDEF, unsigned int parameter
   *
   * CDEF is the constrained directional enhancement filter which is an
   * in-loop filter aiming to remove coding artifacts
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_CDEF = 58,

  /*!\brief Codec control function to encode with Loop Restoration Filter,
   * unsigned int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_RESTORATION = 59,

  /*!\brief Codec control function to force video mode, unsigned int parameter
   *
   * - 0 = do not force video mode (default)
   * - 1 = force video mode even for a single frame
   */
  AV2E_SET_FORCE_VIDEO_MODE = 60,

  /*!\brief Codec control function to enable trellis quantization,
   * unsigned int parameter
   *
   * - 0 = do not apply trellis quantization
   * - 1 = apply trellis quantization in all stages
   * - 2 = apply trellis quantization in only the final encode pass
   * - 3 = disable trellis quantization in estimate_yrd_for_sb
   */
  AV2E_SET_ENABLE_TRELLIS_QUANT = 62,

  /*!\brief Codec control function to encode with quantisation matrices,
   * unsigned int parameter
   *
   * AVM can operate with default quantisation matrices dependent on
   * quantisation level and block type.
   *
   * - 0 = disable (default)
   * - 1 = enable
   */
  AV2E_SET_ENABLE_QM = 63,

  /*!\brief Codec control function to set the min quant matrix flatness,
   * unsigned int parameter
   *
   * AVM can operate with different ranges of quantisation matrices.
   * As quantisation levels increase, the matrices get flatter. This
   * control sets the minimum level of flatness from which the matrices
   * are determined.
   *
   * By default, the encoder sets this minimum at half the available
   * range.
   */
  AV2E_SET_QM_MIN = 64,

  /*!\brief Codec control function to set the max quant matrix flatness,
   * unsigned int parameter
   *
   * AVM can operate with different ranges of quantisation matrices.
   * As quantisation levels increase, the matrices get flatter. This
   * control sets the maximum level of flatness possible.
   *
   * By default, the encoder sets this maximum at the top of the
   * available range.
   */
  AV2E_SET_QM_MAX = 65,

  /*!\brief Codec control function to set the min quant matrix flatness,
   * unsigned int parameter
   *
   * AVM can operate with different ranges of quantisation matrices.
   * As quantisation levels increase, the matrices get flatter. This
   * control sets the flatness for luma (Y).
   *
   * By default, the encoder sets this minimum at half the available
   * range.
   */
  AV2E_SET_QM_Y = 66,

  /*!\brief Codec control function to set the min quant matrix flatness,
   * unsigned int parameter
   *
   * AVM can operate with different ranges of quantisation matrices.
   * As quantisation levels increase, the matrices get flatter. This
   * control sets the flatness for chroma (U).
   *
   * By default, the encoder sets this minimum at half the available
   * range.
   */
  AV2E_SET_QM_U = 67,

  /*!\brief Codec control function to set the min quant matrix flatness,
   * unsigned int parameter
   *
   * AVM can operate with different ranges of quantisation matrices.
   * As quantisation levels increase, the matrices get flatter. This
   * control sets the flatness for chrome (V).
   *
   * By default, the encoder sets this minimum at half the available
   * range.
   */
  AV2E_SET_QM_V = 68,

  /* NOTE: enum 69 unused */

  /*!\brief Codec control function to set a maximum number of tile groups,
   * unsigned int parameter
   *
   * This will set the maximum number of tile groups. This will be
   * overridden if an MTU size is set. The default value is 1.
   */
  AV2E_SET_NUM_TG = 70,

  /*!\brief Codec control function to set an MTU size for a tile group, unsigned
   * int parameter
   *
   * This will set the maximum number of bytes in a tile group. This can be
   * exceeded only if a single tile is larger than this amount.
   *
   * By default, the value is 0, in which case a fixed number of tile groups
   * is used.
   */
  AV2E_SET_MTU = 71,

  /* NOTE: enum 72 unused */

  /*!\brief Codec control function to enable/disable rectangular partitions, int
   * parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_RECT_PARTITIONS = 73,

  /*!\brief Codec control function to enable/disable 1:4 and 4:1 partitions, int
   * parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_1TO4_PARTITIONS = 75,

  /*!\brief Codec control function to set min partition size, int parameter
   *
   * min_partition_size is applied to both width and height of the partition.
   * i.e, both width and height of a partition can not be smaller than
   * the min_partition_size, except the partition at the picture boundary.
   *
   * Valid values: [4, 8, 16, 32, 64, 128]. The default value is 4 for
   * 4x4.
   */
  AV2E_SET_MIN_PARTITION_SIZE = 76,

  /*!\brief Codec control function to set max partition size, int parameter
   *
   * max_partition_size is applied to both width and height of the partition.
   * i.e, both width and height of a partition can not be larger than
   * the max_partition_size.
   *
   * Valid values:[4, 8, 16, 32, 64, 128] The default value is 128 for
   * 128x128.
   */
  AV2E_SET_MAX_PARTITION_SIZE = 77,

  /*!\brief Codec control function to turn on / off intra edge filter
   * at sequence level, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_INTRA_EDGE_FILTER = 78,

  /*!\brief Codec control function to turn on / off 64-length transforms, int
   * parameter
   *
   * This will enable or disable usage of length 64 transforms in any
   * direction.
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_TX64 = 80,

  /*!\brief Codec control function to turn on / off flip and identity
   * transforms, int parameter
   *
   * This will enable or disable usage of flip and identity transform
   * types in any direction. If enabled, this includes:
   * - FLIPADST_DCT
   * - DCT_FLIPADST
   * - FLIPADST_FLIPADST
   * - ADST_FLIPADST
   * - FLIPADST_ADST
   * - IDTX
   * - V_DCT
   * - H_DCT
   * - V_ADST
   * - H_ADST
   * - V_FLIPADST
   * - H_FLIPADST
   *
   * Valid values:
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_FLIP_IDTX = 81,

  /* Note: enum value 82 unused */

  /* Note: enum value 83 unused */

  /*!\brief Codec control function to turn on / off ref frame mvs (mfmv) usage
   * at sequence level, int parameter
   *
   * \attention If AV2E_SET_ENABLE_ORDER_HINT is 0, then this flag is forced
   * to 0.
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_REF_FRAME_MVS = 84,

  /*!\brief Codec control function to set temporal mv prediction
   * enabling/disabling at frame level, int parameter
   *
   * \attention If AV2E_SET_ENABLE_REF_FRAME_MVS is 0, then this flag is
   * forced to 0.
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ALLOW_REF_FRAME_MVS = 85,

  /* Note: enum value 86 unused */

  /*!\brief Codec control function to turn on / off delta quantization in chroma
   * planes usage for a sequence, int parameter
   *
   * - 0 = disable (default)
   * - 1 = enable
   */
  AV2E_SET_ENABLE_CHROMA_DELTAQ = 87,

  /*!\brief Codec control function to turn on / off masked compound usage
   * (wedge and diff-wtd compound modes) for a sequence, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_MASKED_COMP = 88,

  /*!\brief Codec control function to turn on / off one sided compound usage
   * for a sequence, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_ONESIDED_COMP = 89,

  /*!\brief Codec control function to turn on / off interintra compound
   * for a sequence, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_INTERINTRA_COMP = 90,

  /*!\brief Codec control function to turn on / off smooth inter-intra
   * mode for a sequence, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_SMOOTH_INTERINTRA = 91,

  /*!\brief Codec control function to turn on / off difference weighted
   * compound, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_DIFF_WTD_COMP = 92,

  /*!\brief Codec control function to turn on / off interinter wedge
   * compound, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_INTERINTER_WEDGE = 93,

  /*!\brief Codec control function to turn on / off interintra wedge
   * compound, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_INTERINTRA_WEDGE = 94,

  /*!\brief Codec control function to turn on / off global motion usage
   * for a sequence, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_GLOBAL_MOTION = 95,

  /*!\brief Codec control function to turn on / off local warped motion
   * at sequence level, int parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_WARPED_MOTION = 96,

  /* Note: enum value 97 unused */

  /* Note: enum value 98 unused */

  /*!\brief Codec control function to turn on / off smooth intra modes usage,
   * int parameter
   *
   * This will enable or disable usage of smooth, smooth_h and smooth_v intra
   * modes.
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_SMOOTH_INTRA = 99,

  /*!\brief Codec control function to turn on / off Paeth intra mode usage, int
   * parameter
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_PAETH_INTRA = 100,

  /*!\brief Codec control function to turn on / off CFL uv intra mode usage, int
   * parameter
   *
   * This will enable or disable usage of chroma-from-luma intra mode.
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_CFL_INTRA = 101,

  /* NOTE: enum 102 unused */

  /*!\brief Codec control function to turn on / off overlay frames for
   * filtered ALTREF frames, int parameter
   *
   * This will enable or disable coding of overlay frames for filtered ALTREF
   * frames. When set to 0, overlay frames are not used but show existing frame
   * is used to display the filtered ALTREF frame as is. As a result the decoded
   * frame rate remains the same as the display frame rate. The default is 1.
   */
  AV2E_SET_ENABLE_OVERLAY = 103,

  /*!\brief Codec control function to turn on/off palette mode, int parameter */
  AV2E_SET_ENABLE_PALETTE = 104,

  /*!\brief Codec control function to turn on/off intra block copy mode, int
     parameter */
  AV2E_SET_ENABLE_INTRABC = 105,

  /*!\brief Codec control function to turn on/off intra angle delta, int
     parameter */
  AV2E_SET_ENABLE_ANGLE_DELTA = 106,

  /*!\brief Codec control function to set the delta q mode, unsigned int
   * parameter
   *
   * AV2 supports a delta q mode feature, that allows modulating q per
   * superblock.
   *
   * - 0 = deltaq signaling off
   * - 1 = use modulation to maximize objective quality (default)
   * - 2 = use modulation to maximize perceptual quality
   */
  AV2E_SET_DELTAQ_MODE = 107,

  /*!\brief Values 108-109 are unused.
   */

  /*!\brief Codec control function to enable the extreme motion vector unit
   * test, unsigned int parameter
   *
   * - 0 = off
   * - 1 = MAX_EXTREME_MV
   * - 2 = MIN_EXTREME_MV
   *
   * \note This is only used in motion vector unit test.
   */
  AV2E_ENABLE_MOTION_VECTOR_UNIT_TEST = 110,

  /*!\brief Codec control function to signal picture timing info in the
   * bitstream, avm_timing_info_type_t parameter. Default is
   * AVM_TIMING_UNSPECIFIED.
   */
  AV2E_SET_TIMING_INFO_TYPE = 111,

  /*!\brief Codec control function to add film grain parameters (one of several
   * preset types) info in the bitstream, int parameter
   *
   Valid range: 0..16, 0 is unknown, 1..16 are test vectors
   */
  AV2E_SET_FILM_GRAIN_TEST_VECTOR = 112,

  /*!\brief Codec control function to set the path to the film grain parameters,
   * const char* parameter
   */
  AV2E_SET_FILM_GRAIN_TABLE = 113,

  /*!\brief Sets the noise level, int parameter */
  AV2E_SET_DENOISE_NOISE_LEVEL = 114,

  /*!\brief Sets the denoisers block size, unsigned int parameter */
  AV2E_SET_DENOISE_BLOCK_SIZE = 115,

  /*!\brief Sets the chroma subsampling x value, unsigned int parameter */
  AV2E_SET_CHROMA_SUBSAMPLING_X = 116,

  /*!\brief Sets the chroma subsampling y value, unsigned int parameter */
  AV2E_SET_CHROMA_SUBSAMPLING_Y = 117,

  /*!\brief Control to use a reduced tx type set, int parameter */
  AV2E_SET_REDUCED_TX_TYPE_SET = 118,

  /*!\brief Control to use dct only for intra modes, int parameter */
  AV2E_SET_INTRA_DCT_ONLY = 119,

  /*!\brief Control to use dct only for inter modes, int parameter */
  AV2E_SET_INTER_DCT_ONLY = 120,

  /*!\brief Control to use default tx type only for intra modes, int parameter
   */
  AV2E_SET_INTRA_DEFAULT_TX_ONLY = 121,

  /*!\brief Control to use adaptive quantize_b, int parameter */
  AV2E_SET_QUANT_B_ADAPT = 122,

  /*!\brief Control to select maximum height for the GF group pyramid structure,
   * unsigned int parameter
   *
   * Valid range: 0..4
   */
  AV2E_SET_GF_MAX_PYRAMID_HEIGHT = 123,

  /*!\brief Control to select maximum reference frames allowed per frame, int
   * parameter
   *
   * Valid range: 1..7
   */
  AV2E_SET_MAX_REFERENCE_FRAMES = 124,

  /*!\brief Control to use reduced set of single and compound references, int
     parameter */
  AV2E_SET_REDUCED_REFERENCE_SET = 125,

  /* NOTE: enums 126-139 unused */
  /* NOTE: Need a gap in enum values to avoud conflict with 128, 129, 130 */

  /*!\brief Control to set frequency of the cost updates for coefficients,
   * unsigned int parameter
   *
   * - 0 = update at SB level (default)
   * - 1 = update at SB row level in tile
   * - 2 = update at tile level
   * - 3 = turn off
   */
  AV2E_SET_COEFF_COST_UPD_FREQ = 140,

  /*!\brief Control to set frequency of the cost updates for mode, unsigned int
   * parameter
   *
   * - 0 = update at SB level (default)
   * - 1 = update at SB row level in tile
   * - 2 = update at tile level
   * - 3 = turn off
   */
  AV2E_SET_MODE_COST_UPD_FREQ = 141,

  /*!\brief Control to set frequency of the cost updates for motion vectors,
   * unsigned int parameter
   *
   * - 0 = update at SB level (default)
   * - 1 = update at SB row level in tile
   * - 2 = update at tile level
   * - 3 = turn off
   */
  AV2E_SET_MV_COST_UPD_FREQ = 142,

  /*!\brief Control to set bit mask that specifies which tier each of the 32
   * possible operating points conforms to, unsigned int parameter
   *
   * - 0 = main tier (default)
   * - 1 = high tier
   */
  AV2E_SET_TIER_MASK = 143,

  /*!\brief Control to set minimum compression ratio, unsigned int parameter
   * Take integer values. If non-zero, encoder will try to keep the compression
   * ratio of each frame to be higher than the given value divided by 100.
   * E.g. 850 means minimum compression ratio of 8.5.
   */
  AV2E_SET_MIN_CR = 144,

  /* NOTE: enums 145-152 unused */

  /*!\brief Codec control function to set the path to the VMAF model used when
   * tuning the encoder for VMAF, const char* parameter
   */
  AV2E_SET_VMAF_MODEL_PATH = 153,

  /*!\brief Value 154 is unused.
   */

  /*!\brief Codec control function to enable the superblock multipass unit test
   * in AV2 to ensure that the encoder does not leak state between different
   * passes. unsigned int parameter.
   *
   * - 0 = disable (default)
   * - 1 = enable
   *
   * \note This is only used in sb_multipass unit test.
   */
  AV2E_ENABLE_SB_MULTIPASS_UNIT_TEST = 155,

  /*!\brief Control to select minimum height for the GF group pyramid structure,
   * unsigned int parameter
   *
   * Valid values: 0..4
   */
  AV2E_SET_GF_MIN_PYRAMID_HEIGHT = 156,

  /*!\brief Control to set average complexity of the corpus in the case of
   * single pass vbr based on LAP, unsigned int parameter
   */
  AV2E_SET_VBR_CORPUS_COMPLEXITY_LAP = 157,

  /*!\brief Control to set the subgop config string.
   */
  AV2E_SET_SUBGOP_CONFIG_STR = 158,

  /*!\brief Control to set the subgop config path.
   */
  AV2E_SET_SUBGOP_CONFIG_PATH = 159,

  /*!\brief Control to get baseline gf interval
   */
  AV2E_GET_BASELINE_GF_INTERVAL = 160,

  /*!\brief Codec control function to encode with deblocking, unsigned int
   * parameter
   *
   * deblocking is the in-loop filter aiming to smooth blocky artifacts
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_DEBLOCKING = 161,

  /*!\brief Control to get frame type
   */
  AV2E_GET_FRAME_TYPE = 162,

  /*!\brief Control to enable subgop stats
   */
  AV2E_ENABLE_SUBGOP_STATS = 163,

  /*!\brief Control to get sub gop config
   */
  AV2E_GET_SUB_GOP_CONFIG = 164,

  /*!\brief Control to get frame info
   */
  AV2E_GET_FRAME_INFO = 165,
  /*!\brief Control to set frame output order derivation method
   */
  AV2E_SET_FRAME_OUTPUT_ORDER_DERIVATION = 166,
  /*!\brief Control to enable intra_dip mode
   */
  AV2E_SET_ENABLE_INTRA_DIP = 167,
  /*!\brief Control to enable CDF averaging for context initialization
   */
  AV2E_SET_ENABLE_CDF_AVERAGING = 168,
  /*!\brief Control to enable BRU
   */
  AV2E_SET_ENABLE_BRU = 169,
  /*!\brief Control to get enable BRU
   */
  AV2E_GET_ENABLE_BRU = 170,
  /*!\brief Control to set the user defined quantization matrices for a level,
   * const avm_user_defined_qm_t* parameter
   */
  AV2E_SET_USER_DEFINED_QMATRIX = 171,

  /*!\brief Control to set the number of frame multi quantization matrices for
   * unit test, unsigned int parameter
   *
   * \note This is only used in quantization matrix unit test.
   */
  AV2E_SET_FRAME_MULTI_QMATRIX_UNIT_TEST = 172,

  /*!\brief Codec control function to encode with GDF, unsigned int parameter
   *
   * GDF is the guided detail filter which is an
   * in-loop filter aiming to remove coding artifacts
   *
   * - 0 = disable
   * - 1 = enable (default)
   */
  AV2E_SET_ENABLE_GDF = 173,

  /*!\brief Control to select block size in film grain synthesis
   *
   * Valid range: 0..1, 0 is 16x16 block size (default), 1 is 32x32 block size
   */
  AV2E_SET_FILM_GRAIN_BLOCK_SIZE = 174,
#if CONFIG_F356_SEF_DOH
  /*!\brief Control to set leaf node frames to be show existing frames with
   * derive_order_hint = 0
   *
   * \note This is only used in show exsiting frame with order hint signalled
   * test
   */
  AV2E_SET_SEF_WITH_ORDER_HINT_TEST = 175,
#endif
  /*!\brief Signal multiple sequence headers that are the same as the one
   * created
   *
   * \note This is used to test the case multiple sequence headers are signalled
   */
  AV2E_SET_MULTI_SEQ_HEADER_TEST = 176,
};

/*!\brief avm 1-D scaling mode
 *
 * This set of constants define 1-D avm scaling modes
 */
typedef enum avm_scaling_mode_1d {
  AVME_NORMAL = 0,
  AVME_FOURFIVE = 1,
  AVME_THREEFIVE = 2,
  AVME_THREEFOUR = 3,
  AVME_ONEFOUR = 4,
  AVME_ONEEIGHT = 5,
  AVME_ONETWO = 6
} AVM_SCALING_MODE;

/*!\brief Max number of segments
 *
 * This is the limit of number of segments allowed within a frame.
 *
 * Currently same as "MAX_SEGMENTS" in AV2, the maximum that AV2 supports.
 *
 */

#define AVM_MAX_SEGMENTS 16

/*!\brief  avm region of interest map
 *
 * These defines the data structures for the region of interest map
 *
 * TODO(yaowu): create a unit test for ROI map related APIs
 *
 */
typedef struct avm_roi_map {
  /*! An id between 0 and 7 for each 8x8 region within a frame. */
  unsigned char *roi_map;
  unsigned int rows;             /**< Number of rows. */
  unsigned int cols;             /**< Number of columns. */
  int delta_q[AVM_MAX_SEGMENTS]; /**< Quantizer deltas. */
  /*! Static breakout threshold for each segment. */
  unsigned int static_threshold[AVM_MAX_SEGMENTS];
} avm_roi_map_t;

/*!\brief  avm active region map
 *
 * These defines the data structures for active region map
 *
 */

typedef struct avm_active_map {
  /*!\brief specify an on (1) or off (0) each 16x16 region within a frame */
  unsigned char *active_map;
  unsigned int rows; /**< number of rows */
  unsigned int cols; /**< number of cols */
} avm_active_map_t;

/*!\brief  avm image scaling mode
 *
 * This defines the data structure for image scaling mode
 *
 */
typedef struct avm_scaling_mode {
  AVM_SCALING_MODE h_scaling_mode; /**< horizontal scaling mode */
  AVM_SCALING_MODE v_scaling_mode; /**< vertical scaling mode   */
} avm_scaling_mode_t;

/*!brief user-defined quantization matrices for a given level */
typedef struct avm_user_defined_qm {
  int level;      /**< QM level in the range of 0..14 */
  int num_planes; /**< Number of planes: 1 or 3 */
  // Index for the qm_8x8, qm_8x4, qm_4x8 arrays: 0:Y, 1:U, 2:V
  // qm_8x8[i], qm_8x4[i], qm_4x8[i] each point to a flattened matrix in
  // row-major order.
  const uint8_t *qm_8x8[3]; /**< width=8, height=8 */
  const uint8_t *qm_8x4[3]; /**< width=8, height=4 */
  const uint8_t *qm_4x8[3]; /**< width=4, height=8 */
} avm_user_defined_qm_t;

/*!brief AV2 encoder content type */
typedef enum {
  AVM_CONTENT_DEFAULT,
  AVM_CONTENT_SCREEN,
  AVM_CONTENT_INVALID
} avm_tune_content;

/*!brief AV2 encoder timing info type signaling */
typedef enum {
  AVM_TIMING_UNSPECIFIED,
  AVM_TIMING_EQUAL,
  AVM_TIMING_DEC_MODEL
} avm_timing_info_type_t;

/*!\brief Model tuning parameters
 *
 * Changes the encoder to tune for certain types of input material.
 *
 */
typedef enum {
  AVM_TUNE_PSNR = 0,
  AVM_TUNE_SSIM = 1,
  /* NOTE: enums 2 and 3 unused */
  AVM_TUNE_VMAF_WITH_PREPROCESSING = 4,
  AVM_TUNE_VMAF_WITHOUT_PREPROCESSING = 5,
  AVM_TUNE_VMAF_MAX_GAIN = 6,
  AVM_TUNE_VMAF_NEG_MAX_GAIN = 7,
} avm_tune_metric;

/*!\cond */
/*!\brief Encoder control function parameter type
 *
 * Defines the data types that AVME/AV2E control functions take.
 *
 * \note Additional common controls are defined in avm.h.
 *
 * \note For each control ID "X", a macro-define of
 * AVM_CTRL_X is provided. It is used at compile time to determine
 * if the control ID is supported by the libavm library available,
 * when the libavm version cannot be controlled.
 */
AVM_CTRL_USE_TYPE(AVME_USE_REFERENCE, int)
#define AVM_CTRL_AVME_USE_REFERENCE

AVM_CTRL_USE_TYPE(AVME_SET_ROI_MAP, avm_roi_map_t *)
#define AVM_CTRL_AVME_SET_ROI_MAP

AVM_CTRL_USE_TYPE(AVME_SET_ACTIVEMAP, avm_active_map_t *)
#define AVM_CTRL_AVME_SET_ACTIVEMAP

AVM_CTRL_USE_TYPE(AVME_SET_SCALEMODE, avm_scaling_mode_t *)
#define AVM_CTRL_AVME_SET_SCALEMODE

AVM_CTRL_USE_TYPE(AVME_SET_MLAYER_ID, unsigned int)
#define AVM_CTRL_AVME_SET_MLAYER_ID

AVM_CTRL_USE_TYPE(AVME_SET_CPUUSED, int)
#define AVM_CTRL_AVME_SET_CPUUSED

AVM_CTRL_USE_TYPE(AVME_SET_ENABLEAUTOALTREF, unsigned int)
#define AVM_CTRL_AVME_SET_ENABLEAUTOALTREF

AVM_CTRL_USE_TYPE(AVME_SET_ENABLEAUTOBWDREF, unsigned int)
#define AVM_CTRL_AVME_SET_ENABLEAUTOBWDREF

AVM_CTRL_USE_TYPE(AVME_SET_SHARPNESS, unsigned int)
#define AVM_CTRL_AVME_SET_SHARPNESS

AVM_CTRL_USE_TYPE(AVME_SET_STATIC_THRESHOLD, unsigned int)
#define AVM_CTRL_AVME_SET_STATIC_THRESHOLD

AVM_CTRL_USE_TYPE(AVME_SET_ARNR_MAXFRAMES, unsigned int)
#define AVM_CTRL_AVME_SET_ARNR_MAXFRAMES

AVM_CTRL_USE_TYPE(AVME_SET_ARNR_STRENGTH, unsigned int)
#define AVM_CTRL_AVME_SET_ARNR_STRENGTH

AVM_CTRL_USE_TYPE(AVME_SET_TUNING, int) /* avm_tune_metric */
#define AVM_CTRL_AVME_SET_TUNING

AVM_CTRL_USE_TYPE(AVME_SET_QP, unsigned int)
#define AVM_CTRL_AVME_SET_QP

AVM_CTRL_USE_TYPE(AV2E_SET_ROW_MT, unsigned int)
#define AVM_CTRL_AV2E_SET_ROW_MT

AVM_CTRL_USE_TYPE(AV2E_SET_TILE_COLUMNS, unsigned int)
#define AVM_CTRL_AV2E_SET_TILE_COLUMNS

AVM_CTRL_USE_TYPE(AV2E_SET_TILE_ROWS, unsigned int)
#define AVM_CTRL_AV2E_SET_TILE_ROWS

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_TPL_MODEL, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_TPL_MODEL

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_KEYFRAME_FILTERING, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_KEYFRAME_FILTERING

AVM_CTRL_USE_TYPE(AVME_GET_LAST_QUANTIZER, int *)
#define AVM_CTRL_AVME_GET_LAST_QUANTIZER

AVM_CTRL_USE_TYPE(AVME_SET_MAX_INTRA_BITRATE_PCT, unsigned int)
#define AVM_CTRL_AVME_SET_MAX_INTRA_BITRATE_PCT

AVM_CTRL_USE_TYPE(AVME_SET_MAX_INTER_BITRATE_PCT, unsigned int)
#define AVM_CTRL_AVME_SET_MAX_INTER_BITRATE_PCT

AVM_CTRL_USE_TYPE(AVME_SET_NUMBER_MLAYERS, int)
#define AVME_CTRL_AVME_SET_NUMBER_MLAYERS

AVM_CTRL_USE_TYPE(AV2E_SET_GF_CBR_BOOST_PCT, unsigned int)
#define AVM_CTRL_AV2E_SET_GF_CBR_BOOST_PCT

AVM_CTRL_USE_TYPE(AV2E_SET_LOSSLESS, unsigned int)
#define AVM_CTRL_AV2E_SET_LOSSLESS

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_DEBLOCKING, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_DEBLOCKING

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_CDEF, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_CDEF

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_GDF, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_GDF

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_RESTORATION, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_RESTORATION

AVM_CTRL_USE_TYPE(AV2E_SET_FORCE_VIDEO_MODE, unsigned int)
#define AVM_CTRL_AV2E_SET_FORCE_VIDEO_MODE

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_TRELLIS_QUANT, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_TRELLIS_QUANT

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_QM, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_QM

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_DIST_8X8, unsigned int)
#define AVM_CTRL_AV2E_SET_ENABLE_DIST_8X8

AVM_CTRL_USE_TYPE(AV2E_SET_QM_MIN, unsigned int)
#define AVM_CTRL_AV2E_SET_QM_MIN

AVM_CTRL_USE_TYPE(AV2E_SET_QM_MAX, unsigned int)
#define AVM_CTRL_AV2E_SET_QM_MAX

AVM_CTRL_USE_TYPE(AV2E_SET_QM_Y, unsigned int)
#define AVM_CTRL_AV2E_SET_QM_Y

AVM_CTRL_USE_TYPE(AV2E_SET_QM_U, unsigned int)
#define AVM_CTRL_AV2E_SET_QM_U

AVM_CTRL_USE_TYPE(AV2E_SET_QM_V, unsigned int)
#define AVM_CTRL_AV2E_SET_QM_V

AVM_CTRL_USE_TYPE(AV2E_SET_USER_DEFINED_QMATRIX, const avm_user_defined_qm_t *)
#define AVM_CTRL_AV2E_SET_USER_DEFINED_QMATRIX

AVM_CTRL_USE_TYPE(AV2E_SET_FRAME_MULTI_QMATRIX_UNIT_TEST, unsigned int)
#define AVM_CTRL_AV2E_SET_FRAME_MULTI_QMATRIX_UNIT_TEST

#if CONFIG_F356_SEF_DOH
AVM_CTRL_USE_TYPE(AV2E_SET_SEF_WITH_ORDER_HINT_TEST, unsigned int)
#define AVM_CTRL_SET_SEF_WITH_ORDER_HINT_TEST
#endif  // CONFIG_F356_SEF_DOH

AVM_CTRL_USE_TYPE(AV2E_SET_MULTI_SEQ_HEADER_TEST, unsigned int)
#define AVM_CTRL_SET_MULTI_SEQ_HEADER_TEST

AVM_CTRL_USE_TYPE(AV2E_SET_NUM_TG, unsigned int)
#define AVM_CTRL_AV2E_SET_NUM_TG

AVM_CTRL_USE_TYPE(AV2E_SET_MTU, unsigned int)
#define AVM_CTRL_AV2E_SET_MTU

AVM_CTRL_USE_TYPE(AV2E_SET_TIMING_INFO_TYPE, int) /* avm_timing_info_type_t */
#define AVM_CTRL_AV2E_SET_TIMING_INFO_TYPE

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_RECT_PARTITIONS, int)
#define AVM_CTRL_AV2E_SET_ENABLE_RECT_PARTITIONS

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_1TO4_PARTITIONS, int)
#define AVM_CTRL_AV2E_SET_ENABLE_1TO4_PARTITIONS

AVM_CTRL_USE_TYPE(AV2E_SET_MIN_PARTITION_SIZE, int)
#define AVM_CTRL_AV2E_SET_MIN_PARTITION_SIZE

AVM_CTRL_USE_TYPE(AV2E_SET_MAX_PARTITION_SIZE, int)
#define AVM_CTRL_AV2E_SET_MAX_PARTITION_SIZE

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_INTRA_EDGE_FILTER, int)
#define AVM_CTRL_AV2E_SET_ENABLE_INTRA_EDGE_FILTER

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_TX64, int)
#define AVM_CTRL_AV2E_SET_ENABLE_TX64

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_FLIP_IDTX, int)
#define AVM_CTRL_AV2E_SET_ENABLE_FLIP_IDTX

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_REF_FRAME_MVS, int)
#define AVM_CTRL_AV2E_SET_ENABLE_REF_FRAME_MVS

AVM_CTRL_USE_TYPE(AV2E_SET_ALLOW_REF_FRAME_MVS, int)
#define AVM_CTRL_AV2E_SET_ALLOW_REF_FRAME_MVS

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_CHROMA_DELTAQ, int)
#define AVM_CTRL_AV2E_SET_ENABLE_CHROMA_DELTAQ

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_MASKED_COMP, int)
#define AVM_CTRL_AV2E_SET_ENABLE_MASKED_COMP

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_ONESIDED_COMP, int)
#define AVM_CTRL_AV2E_SET_ENABLE_ONESIDED_COMP

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_INTERINTRA_COMP, int)
#define AVM_CTRL_AV2E_SET_ENABLE_INTERINTRA_COMP

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_SMOOTH_INTERINTRA, int)
#define AVM_CTRL_AV2E_SET_ENABLE_SMOOTH_INTERINTRA

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_DIFF_WTD_COMP, int)
#define AVM_CTRL_AV2E_SET_ENABLE_DIFF_WTD_COMP

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_INTERINTER_WEDGE, int)
#define AVM_CTRL_AV2E_SET_ENABLE_INTERINTER_WEDGE

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_INTERINTRA_WEDGE, int)
#define AVM_CTRL_AV2E_SET_ENABLE_INTERINTRA_WEDGE

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_GLOBAL_MOTION, int)
#define AVM_CTRL_AV2E_SET_ENABLE_GLOBAL_MOTION

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_WARPED_MOTION, int)
#define AVM_CTRL_AV2E_SET_ENABLE_WARPED_MOTION

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_INTRA_DIP, int)
#define AVM_CTRL_AV2E_SET_ENABLE_INTRA_DIP

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_SMOOTH_INTRA, int)
#define AVM_CTRL_AV2E_SET_ENABLE_SMOOTH_INTRA

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_PAETH_INTRA, int)
#define AVM_CTRL_AV2E_SET_ENABLE_PAETH_INTRA

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_CFL_INTRA, int)
#define AVM_CTRL_AV2E_SET_ENABLE_CFL_INTRA

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_OVERLAY, int)
#define AVM_CTRL_AV2E_SET_ENABLE_OVERLAY

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_PALETTE, int)
#define AVM_CTRL_AV2E_SET_ENABLE_PALETTE

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_INTRABC, int)
#define AVM_CTRL_AV2E_SET_ENABLE_INTRABC

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_ANGLE_DELTA, int)
#define AVM_CTRL_AV2E_SET_ENABLE_ANGLE_DELTA

AVM_CTRL_USE_TYPE(AV2E_SET_FRAME_PARALLEL_DECODING, unsigned int)
#define AVM_CTRL_AV2E_SET_FRAME_PARALLEL_DECODING

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_CDF_AVERAGING, int)
#define AVM_CTRL_AV2E_SET_ENABLE_CDF_AVERAGING

AVM_CTRL_USE_TYPE(AV2E_SET_S_FRAME_MODE, int)
#define AVM_CTRL_AV2E_SET_S_FRAME_MODE

AVM_CTRL_USE_TYPE(AV2E_SET_AQ_MODE, unsigned int)
#define AVM_CTRL_AV2E_SET_AQ_MODE

AVM_CTRL_USE_TYPE(AV2E_SET_DELTAQ_MODE, unsigned int)
#define AVM_CTRL_AV2E_SET_DELTAQ_MODE

AVM_CTRL_USE_TYPE(AV2E_SET_FRAME_PERIODIC_BOOST, unsigned int)
#define AVM_CTRL_AV2E_SET_FRAME_PERIODIC_BOOST

AVM_CTRL_USE_TYPE(AV2E_SET_NOISE_SENSITIVITY, unsigned int)
#define AVM_CTRL_AV2E_SET_NOISE_SENSITIVITY

AVM_CTRL_USE_TYPE(AV2E_SET_TUNE_CONTENT, int) /* avm_tune_content */
#define AVM_CTRL_AV2E_SET_TUNE_CONTENT

AVM_CTRL_USE_TYPE(AV2E_SET_COLOR_PRIMARIES, int)
#define AVM_CTRL_AV2E_SET_COLOR_PRIMARIES

AVM_CTRL_USE_TYPE(AV2E_SET_TRANSFER_CHARACTERISTICS, int)
#define AVM_CTRL_AV2E_SET_TRANSFER_CHARACTERISTICS

AVM_CTRL_USE_TYPE(AV2E_SET_MATRIX_COEFFICIENTS, int)
#define AVM_CTRL_AV2E_SET_MATRIX_COEFFICIENTS

AVM_CTRL_USE_TYPE(AV2E_SET_CHROMA_SAMPLE_POSITION, int)
#define AVM_CTRL_AV2E_SET_CHROMA_SAMPLE_POSITION

AVM_CTRL_USE_TYPE(AV2E_SET_MIN_GF_INTERVAL, unsigned int)
#define AVM_CTRL_AV2E_SET_MIN_GF_INTERVAL

AVM_CTRL_USE_TYPE(AV2E_SET_MAX_GF_INTERVAL, unsigned int)
#define AVM_CTRL_AV2E_SET_MAX_GF_INTERVAL

AVM_CTRL_USE_TYPE(AV2E_GET_ACTIVEMAP, avm_active_map_t *)
#define AVM_CTRL_AV2E_GET_ACTIVEMAP

AVM_CTRL_USE_TYPE(AV2E_SET_COLOR_RANGE, int)
#define AVM_CTRL_AV2E_SET_COLOR_RANGE
AVM_CTRL_USE_TYPE(AV2E_SET_SUPERBLOCK_SIZE, unsigned int)
#define AVM_CTRL_AV2E_SET_SUPERBLOCK_SIZE

AVM_CTRL_USE_TYPE(AV2E_GET_SEQ_LEVEL_IDX, int *)
#define AVM_CTRL_AV2E_GET_SEQ_LEVEL_IDX

AVM_CTRL_USE_TYPE(AV2E_GET_BASELINE_GF_INTERVAL, int *)
#define AVM_CTRL_AV2E_GET_BASELINE_GF_INTERVAL

AVM_CTRL_USE_TYPE(AV2E_GET_SUB_GOP_CONFIG, void *)
#define AVM_CTRL_AV2E_GET_SUB_GOP_CONFIG

AVM_CTRL_USE_TYPE(AV2E_GET_FRAME_TYPE, void *)
#define AVM_CTRL_AV2E_GET_FRAME_TYPE

AVM_CTRL_USE_TYPE(AV2E_GET_FRAME_INFO, void *)
#define AVM_CTRL_AV2E_GET_FRAME_INFO

AVM_CTRL_USE_TYPE(AV2E_ENABLE_SUBGOP_STATS, unsigned int)
#define AVM_CTRL_AV2E_ENABLE_SUBGOP_INFO

AVM_CTRL_USE_TYPE(AV2E_ENABLE_MOTION_VECTOR_UNIT_TEST, unsigned int)
#define AVM_CTRL_AV2E_ENABLE_MOTION_VECTOR_UNIT_TEST

AVM_CTRL_USE_TYPE(AV2E_SET_VMAF_MODEL_PATH, const char *)
#define AVM_CTRL_AV2E_SET_VMAF_MODEL_PATH

AVM_CTRL_USE_TYPE(AV2E_SET_FILM_GRAIN_TEST_VECTOR, int)
#define AVM_CTRL_AV2E_SET_FILM_GRAIN_TEST_VECTOR

AVM_CTRL_USE_TYPE(AV2E_SET_FILM_GRAIN_TABLE, const char *)
#define AVM_CTRL_AV2E_SET_FILM_GRAIN_TABLE

AVM_CTRL_USE_TYPE(AV2E_SET_FILM_GRAIN_BLOCK_SIZE, int)
#define AVM_CTRL_AV2E_SET_FILM_GRAIN_BLOCK_SIZE

AVM_CTRL_USE_TYPE(AV2E_SET_CDF_UPDATE_MODE, unsigned int)
#define AVM_CTRL_AV2E_SET_CDF_UPDATE_MODE

AVM_CTRL_USE_TYPE(AV2E_SET_DENOISE_NOISE_LEVEL, int)
#define AVM_CTRL_AV2E_SET_DENOISE_NOISE_LEVEL

AVM_CTRL_USE_TYPE(AV2E_SET_DENOISE_BLOCK_SIZE, unsigned int)
#define AVM_CTRL_AV2E_SET_DENOISE_BLOCK_SIZE

AVM_CTRL_USE_TYPE(AV2E_SET_CHROMA_SUBSAMPLING_X, unsigned int)
#define AVM_CTRL_AV2E_SET_CHROMA_SUBSAMPLING_X

AVM_CTRL_USE_TYPE(AV2E_SET_CHROMA_SUBSAMPLING_Y, unsigned int)
#define AVM_CTRL_AV2E_SET_CHROMA_SUBSAMPLING_Y

AVM_CTRL_USE_TYPE(AV2E_SET_REDUCED_TX_TYPE_SET, int)
#define AVM_CTRL_AV2E_SET_REDUCED_TX_TYPE_SET

AVM_CTRL_USE_TYPE(AV2E_SET_INTRA_DCT_ONLY, int)
#define AVM_CTRL_AV2E_SET_INTRA_DCT_ONLY

AVM_CTRL_USE_TYPE(AV2E_SET_INTER_DCT_ONLY, int)
#define AVM_CTRL_AV2E_SET_INTER_DCT_ONLY

AVM_CTRL_USE_TYPE(AV2E_SET_INTRA_DEFAULT_TX_ONLY, int)
#define AVM_CTRL_AV2E_SET_INTRA_DEFAULT_TX_ONLY

AVM_CTRL_USE_TYPE(AV2E_SET_QUANT_B_ADAPT, int)
#define AVM_CTRL_AV2E_SET_QUANT_B_ADAPT

AVM_CTRL_USE_TYPE(AV2E_SET_GF_MIN_PYRAMID_HEIGHT, unsigned int)
#define AVM_CTRL_AV2E_SET_GF_MIN_PYRAMID_HEIGHT

AVM_CTRL_USE_TYPE(AV2E_SET_GF_MAX_PYRAMID_HEIGHT, unsigned int)
#define AVM_CTRL_AV2E_SET_GF_MAX_PYRAMID_HEIGHT

AVM_CTRL_USE_TYPE(AV2E_SET_MAX_REFERENCE_FRAMES, int)
#define AVM_CTRL_AV2E_SET_MAX_REFERENCE_FRAMES

AVM_CTRL_USE_TYPE(AV2E_SET_REDUCED_REFERENCE_SET, int)
#define AVM_CTRL_AV2E_SET_REDUCED_REFERENCE_SET

AVM_CTRL_USE_TYPE(AV2E_SET_COEFF_COST_UPD_FREQ, unsigned int)
#define AVM_CTRL_AV2E_SET_COEFF_COST_UPD_FREQ

AVM_CTRL_USE_TYPE(AV2E_SET_MODE_COST_UPD_FREQ, unsigned int)
#define AVM_CTRL_AV2E_SET_MODE_COST_UPD_FREQ

AVM_CTRL_USE_TYPE(AV2E_SET_MV_COST_UPD_FREQ, unsigned int)
#define AVM_CTRL_AV2E_SET_MV_COST_UPD_FREQ

AVM_CTRL_USE_TYPE(AV2E_SET_TARGET_SEQ_LEVEL_IDX, int)
#define AVM_CTRL_AV2E_SET_TARGET_SEQ_LEVEL_IDX

AVM_CTRL_USE_TYPE(AV2E_SET_TIER_MASK, unsigned int)
#define AVM_CTRL_AV2E_SET_TIER_MASK

AVM_CTRL_USE_TYPE(AV2E_SET_MIN_CR, unsigned int)
#define AVM_CTRL_AV2E_SET_MIN_CR

AVM_CTRL_USE_TYPE(AV2E_ENABLE_SB_MULTIPASS_UNIT_TEST, unsigned int)
#define AVM_CTRL_AV2E_ENABLE_SB_MULTIPASS_UNIT_TEST

AVM_CTRL_USE_TYPE(AV2E_SET_VBR_CORPUS_COMPLEXITY_LAP, unsigned int)
#define AVM_CTRL_AV2E_SET_VBR_CORPUS_COMPLEXITY_LAP

AVM_CTRL_USE_TYPE(AV2E_SET_SUBGOP_CONFIG_STR, const char *)
#define AVM_CTRL_AV2E_SET_SUBGOP_CONFIG_STR

AVM_CTRL_USE_TYPE(AV2E_SET_SUBGOP_CONFIG_PATH, const char *)
#define AVM_CTRL_AV2E_SET_SUBGOP_CONFIG_PATH

AVM_CTRL_USE_TYPE(AV2E_SET_FRAME_OUTPUT_ORDER_DERIVATION, int)
#define AVM_CTRL_AV2E_SET_FRAME_OUTPUT_ORDER_DERIVATION

AVM_CTRL_USE_TYPE(AV2E_SET_ENABLE_BRU, int)
#define AVM_CTRL_AV2E_SET_ENABLE_BRU
AVM_CTRL_USE_TYPE(AV2E_GET_ENABLE_BRU, int *)
#define AVM_CTRL_AV2E_GET_ENABLE_BRU

/*!\endcond */
/*! @} - end defgroup avm_encoder */
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_AVMCX_H_
