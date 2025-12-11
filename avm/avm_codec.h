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

///////////////////////////////////////////////////////////////////////////////
// Internal implementation details
///////////////////////////////////////////////////////////////////////////////
//
// There are two levels of interfaces used to access the AVM codec: the
// the avm_codec_iface and the avm_codec_ctx.
//
// 1. avm_codec_iface_t
//    (Related files: avm/avm_codec.h, avm/src/avm_codec.c,
//    avm/internal/avm_codec_internal.h, av2/av2_cx_iface.c,
//    av2/av2_dx_iface.c)
//
// Used to initialize the codec context, which contains the configuration for
// for modifying the encoder/decoder during run-time. See the other
// documentation in this header file for more details. For the most part,
// users will call helper functions, such as avm_codec_iface_name,
// avm_codec_get_caps, etc., to interact with it.
//
// The main purpose of the avm_codec_iface_t is to provide a way to generate
// a default codec config, find out what capabilities the implementation has,
// and create an avm_codec_ctx_t (which is actually used to interact with the
// codec).
//
// Note that the implementations for the AV2 algorithm are located in
// av2/av2_cx_iface.c and av2/av2_dx_iface.c
//
//
// 2. avm_codec_ctx_t
//  (Related files: avm/avm_codec.h, av2/av2_cx_iface.c, av2/av2_dx_iface.c,
//   avm/avmcx.h, avm/avmdx.h, avm/src/avm_encoder.c, avm/src/avm_decoder.c)
//
// The actual interface between user code and the codec. It stores the name
// of the codec, a pointer back to the avm_codec_iface_t that initialized it,
// initialization flags, a config for either encoder or the decoder, and a
// pointer to internal data.
//
// The codec is configured / queried through calls to avm_codec_control,
// which takes a control ID (listed in avmcx.h and avmdx.h) and a parameter.
// In the case of "getter" control IDs, the parameter is modified to have
// the requested value; in the case of "setter" control IDs, the codec's
// configuration is changed based on the parameter. Note that a avm_codec_err_t
// is returned, which indicates if the operation was successful or not.
//
// Note that for the encoder, the avm_codec_alg_priv_t points to the
// the avm_codec_alg_priv structure in av2/av2_cx_iface.c, and for the decoder,
// the struct in av2/av2_dx_iface.c. Variables such as AV2_COMP cpi are stored
// here and also used in the core algorithm.
//
// At the end, avm_codec_destroy should be called for each initialized
// avm_codec_ctx_t.

/*!\defgroup codec Common Algorithm Interface
 * This abstraction allows applications to easily support multiple video
 * formats with minimal code duplication. This section describes the interface
 * common to all codecs (both encoders and decoders).
 * @{
 */

/*!\file
 * \brief Describes the codec algorithm interface to applications.
 *
 * This file describes the interface between an application and a
 * video codec algorithm.
 *
 * An application instantiates a specific codec instance by using
 * avm_codec_dec_init() or avm_codec_enc_init() and a pointer to the
 * algorithm's interface structure:
 *     <pre>
 *     my_app.c:
 *       extern avm_codec_iface_t my_codec;
 *       {
 *           avm_codec_ctx_t algo;
 *           int threads = 4;
 *           avm_codec_dec_cfg_t cfg = { threads, 0, 0 };
 *           res = avm_codec_dec_init(&algo, &my_codec, &cfg, 0);
 *       }
 *     </pre>
 *
 * Once initialized, the instance is managed using other functions from
 * the avm_codec_* family.
 */
#ifndef AVM_AVM_AVM_CODEC_H_
#define AVM_AVM_AVM_CODEC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "config/avm_config.h"

#include "avm/avm_image.h"
#include "avm/avm_integer.h"

/*!\brief Decorator indicating a function is deprecated */
#ifndef AVM_DEPRECATED
#if defined(__GNUC__) && __GNUC__
#define AVM_DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define AVM_DEPRECATED
#else
#define AVM_DEPRECATED
#endif
#endif /* AVM_DEPRECATED */

#ifndef AVM_DECLSPEC_DEPRECATED
#if defined(__GNUC__) && __GNUC__
#define AVM_DECLSPEC_DEPRECATED /**< \copydoc #AVM_DEPRECATED */
#elif defined(_MSC_VER)
/*!\brief \copydoc #AVM_DEPRECATED */
#define AVM_DECLSPEC_DEPRECATED __declspec(deprecated)
#else
#define AVM_DECLSPEC_DEPRECATED /**< \copydoc #AVM_DEPRECATED */
#endif
#endif /* AVM_DECLSPEC_DEPRECATED */

/*!\brief Decorator indicating a function is potentially unused */
#ifdef AVM_UNUSED
#elif defined(__GNUC__) || defined(__clang__)
#define AVM_UNUSED __attribute__((unused))
#else
#define AVM_UNUSED
#endif

/*!\brief Decorator indicating that given struct/union/enum is packed */
#ifndef ATTRIBUTE_PACKED
#if defined(__GNUC__) && __GNUC__
#define ATTRIBUTE_PACKED __attribute__((packed))
#elif defined(_MSC_VER)
#define ATTRIBUTE_PACKED
#else
#define ATTRIBUTE_PACKED
#endif
#endif /* ATTRIBUTE_PACKED */

/*!\brief Current ABI version number
 *
 * \internal
 * If this file is altered in any way that changes the ABI, this value
 * must be bumped.  Examples include, but are not limited to, changing
 * types, removing or reassigning enums, adding/removing/rearranging
 * fields to structures
 */
#define AVM_CODEC_ABI_VERSION (6 + AVM_IMAGE_ABI_VERSION) /**<\hideinitializer*/

/*!\brief Algorithm return codes */
typedef enum {
  /*!\brief Operation completed without error */
  AVM_CODEC_OK,

  /*!\brief Unspecified error */
  AVM_CODEC_ERROR,

  /*!\brief Memory operation failed */
  AVM_CODEC_MEM_ERROR,

  /*!\brief ABI version mismatch */
  AVM_CODEC_ABI_MISMATCH,

  /*!\brief Algorithm does not have required capability */
  AVM_CODEC_INCAPABLE,

  /*!\brief The given bitstream is not supported.
   *
   * The bitstream was unable to be parsed at the highest level. The decoder
   * is unable to proceed. This error \ref SHOULD be treated as fatal to the
   * stream. */
  AVM_CODEC_UNSUP_BITSTREAM,

  /*!\brief Encoded bitstream uses an unsupported feature
   *
   * The decoder does not implement a feature required by the encoder. This
   * return code should only be used for features that prevent future
   * pictures from being properly decoded. This error \ref MAY be treated as
   * fatal to the stream or \ref MAY be treated as fatal to the current GOP.
   */
  AVM_CODEC_UNSUP_FEATURE,

  /*!\brief The coded data for this stream is corrupt or incomplete
   *
   * There was a problem decoding the current frame.  This return code
   * should only be used for failures that prevent future pictures from
   * being properly decoded. This error \ref MAY be treated as fatal to the
   * stream or \ref MAY be treated as fatal to the current GOP. If decoding
   * is continued for the current GOP, artifacts may be present.
   */
  AVM_CODEC_CORRUPT_FRAME,

  /*!\brief An application-supplied parameter is not valid.
   *
   */
  AVM_CODEC_INVALID_PARAM,

  /*!\brief An iterator reached the end of list.
   *
   */
  AVM_CODEC_LIST_END

} avm_codec_err_t;

/*! \brief Codec capabilities bitfield
 *
 *  Each codec advertises the capabilities it supports as part of its
 *  ::avm_codec_iface_t interface structure. Capabilities are extra interfaces
 *  or functionality, and are not required to be supported.
 *
 *  The available flags are specified by AVM_CODEC_CAP_* defines.
 */
typedef long avm_codec_caps_t;
#define AVM_CODEC_CAP_DECODER 0x1 /**< Is a decoder */
#define AVM_CODEC_CAP_ENCODER 0x2 /**< Is an encoder */

/*! \brief Initialization-time Feature Enabling
 *
 *  Certain codec features must be known at initialization time, to allow for
 *  proper memory allocation.
 *
 *  The available flags are specified by AVM_CODEC_USE_* defines.
 */
typedef long avm_codec_flags_t;

/*!\brief Time Stamp Type
 *
 * An integer, which when multiplied by the stream's time base, provides
 * the absolute time of a sample.
 */
typedef int64_t avm_codec_pts_t;

/*!\brief Codec interface structure.
 *
 * Contains function pointers and other data private to the codec
 * implementation. This structure is opaque to the application. Common
 * functions used with this structure:
 *   - avm_codec_iface_name(avm_codec_iface_t *iface): get the
 *     name of the codec
 *   - avm_codec_get_caps(avm_codec_iface_t *iface): returns
 *     the capabilities of the codec
 *   - avm_codec_enc_config_default: generate the default config for
 *     initializing the encoder (see documentation in avm_encoder.h)
 *   - avm_codec_dec_init, avm_codec_enc_init: initialize the codec context
 *     structure (see documentation on avm_codec_ctx).
 *
 * To get access to the AV2 encoder and decoder, use avm_codec_av2_cx() and
 *  avm_codec_av2_dx().
 */
typedef const struct avm_codec_iface avm_codec_iface_t;

/*!\brief Codec private data structure.
 *
 * Contains data private to the codec implementation. This structure is opaque
 * to the application.
 */
typedef struct avm_codec_priv avm_codec_priv_t;

/*!\brief Compressed Frame Flags
 *
 * This type represents a bitfield containing information about a compressed
 * frame that may be useful to an application. The most significant 16 bits
 * can be used by an algorithm to provide additional detail, for example to
 * support frame types that are codec specific (MPEG-1 D-frames for example)
 */
typedef uint32_t avm_codec_frame_flags_t;
#define AVM_FRAME_IS_KEY 0x1 /**< frame is the start of a GOP */
/*!\brief frame can be dropped without affecting the stream (no future frame
 * depends on this one) */
#define AVM_FRAME_IS_DROPPABLE 0x2
/*!\brief this is an INTRA_ONLY frame */
#define AVM_FRAME_IS_INTRAONLY 0x10
/*!\brief this is an S-frame */
#define AVM_FRAME_IS_SWITCH 0x20
/*!\brief this is a key-frame dependent recovery-point frame */
#define AVM_FRAME_IS_DELAYED_RANDOM_ACCESS_POINT 0x80
/*!\brief this frame has coded film frain params */
#define AVM_FRAME_HAS_FILM_GRAIN_PARAMS 0x100

/*!\brief Iterator
 *
 * Opaque storage used for iterating over lists.
 */
typedef const void *avm_codec_iter_t;

/*!\brief Codec context structure
 *
 * All codecs \ref MUST support this context structure fully. In general,
 * this data should be considered private to the codec algorithm, and
 * not be manipulated or examined by the calling application. Applications
 * may reference the 'name' member to get a printable description of the
 * algorithm.
 */
typedef struct avm_codec_ctx {
  const char *name;             /**< Printable interface name */
  avm_codec_iface_t *iface;     /**< Interface pointers */
  avm_codec_err_t err;          /**< Last returned error */
  const char *err_detail;       /**< Detailed info, if available */
  avm_codec_flags_t init_flags; /**< Flags passed at init time */
  union {
    /**< Decoder Configuration Pointer */
    const struct avm_codec_dec_cfg *dec;
    /**< Encoder Configuration Pointer */
    const struct avm_codec_enc_cfg *enc;
    const void *raw;
  } config;               /**< Configuration pointer aliasing union */
  avm_codec_priv_t *priv; /**< Algorithm private storage */
} avm_codec_ctx_t;

/*!\brief Bit depth for codec
 * *
 * This enumeration determines the bit depth of the codec.
 */
typedef enum avm_bit_depth {
  AVM_BITS_8 = 8,   /**<  8 bits */
  AVM_BITS_10 = 10, /**< 10 bits */
  AVM_BITS_12 = 12, /**< 12 bits */
} avm_bit_depth_t;

#if CONFIG_CWG_E242_BITDEPTH
/*!\brief Bit depth index
 * *
 * The index correponds with each bitepth
 */
enum {
  AVM_BITDEPTH_0 = 0,        /**< 10 bits */
  AVM_BITDEPTH_1 = 1,        /**< 8 bits */
  AVM_BITDEPTH_2 = 2,        /**< 12 bits */
  AVM_NUM_SUPPORTED_BITDEPTH /**<number of supported bitdepth>*/
};

/*!\brief Return the bitdepth
 *
 * Return the bitdepth corresponding to index
 */
int av2_get_bitdepth_from_index(uint32_t bitdepth_lut_idx);
#endif  // CONFIG_CWG_E242_BITDEPTH

/*!\brief Superblock size selection.
 *
 * Defines the superblock size used for encoding. The superblock size can
 * either be fixed at 64x64 or 128x128 pixels, or it can be dynamically
 * selected by the encoder for each frame.
 */
typedef enum avm_superblock_size {
  AVM_SUPERBLOCK_SIZE_64X64,   /**< Always use 64x64 superblocks. */
  AVM_SUPERBLOCK_SIZE_128X128, /**< Always use 128x128 superblocks. */
  AVM_SUPERBLOCK_SIZE_256X256, /**< Always use 256x256 superblocks. */
  AVM_SUPERBLOCK_SIZE_DYNAMIC  /**< Select superblock size dynamically. */
} avm_superblock_size_t;

/*
 * Library Version Number Interface
 *
 * For example, see the following sample return values:
 *     avm_codec_version()           (1<<16 | 2<<8 | 3)
 *     avm_codec_version_str()       "v1.2.3-rc1-16-gec6a1ba"
 *     avm_codec_version_extra_str() "rc1-16-gec6a1ba"
 */

/*!\brief Return the version information (as an integer)
 *
 * Returns a packed encoding of the library version number. This will only
 * include the major.minor.patch component of the version number. Note that this
 * encoded value should be accessed through the macros provided, as the encoding
 * may change in the future.
 *
 */
int avm_codec_version(void);

/*!\brief Return the major version number */
#define avm_codec_version_major() ((avm_codec_version() >> 16) & 0xff)

/*!\brief Return the minor version number */
#define avm_codec_version_minor() ((avm_codec_version() >> 8) & 0xff)

/*!\brief Return the patch version number */
#define avm_codec_version_patch() ((avm_codec_version() >> 0) & 0xff)

/*!\brief Return the version information (as a string)
 *
 * Returns a printable string containing the full library version number. This
 * may contain additional text following the three digit version number, as to
 * indicate release candidates, prerelease versions, etc.
 *
 */
const char *avm_codec_version_str(void);

/*!\brief Return the version information (as a string)
 *
 * Returns a printable "extra string". This is the component of the string
 * returned by avm_codec_version_str() following the three digit version number.
 *
 */
const char *avm_codec_version_extra_str(void);

/*!\brief Return the build configuration
 *
 * Returns a printable string containing an encoded version of the build
 * configuration. This may be useful to avm support.
 *
 */
const char *avm_codec_build_config(void);

/*!\brief Return the name for a given interface
 *
 * Returns a human readable string for name of the given codec interface.
 *
 * \param[in]    iface     Interface pointer
 *
 */
const char *avm_codec_iface_name(avm_codec_iface_t *iface);

/*!\brief Convert error number to printable string
 *
 * Returns a human readable string for the last error returned by the
 * algorithm. The returned error will be one line and will not contain
 * any newline characters.
 *
 *
 * \param[in]    err     Error number.
 *
 */
const char *avm_codec_err_to_string(avm_codec_err_t err);

/*!\brief Retrieve error synopsis for codec context
 *
 * Returns a human readable string for the last error returned by the
 * algorithm. The returned error will be one line and will not contain
 * any newline characters.
 *
 *
 * \param[in]    ctx     Pointer to this instance's context.
 *
 */
const char *avm_codec_error(avm_codec_ctx_t *ctx);

/*!\brief Retrieve detailed error information for codec context
 *
 * Returns a human readable string providing detailed information about
 * the last error.
 *
 * \param[in]    ctx     Pointer to this instance's context.
 *
 * \retval NULL
 *     No detailed information is available.
 */
const char *avm_codec_error_detail(avm_codec_ctx_t *ctx);

/* REQUIRED FUNCTIONS
 *
 * The following functions are required to be implemented for all codecs.
 * They represent the base case functionality expected of all codecs.
 */

/*!\brief Destroy a codec instance
 *
 * Destroys a codec context, freeing any associated memory buffers.
 *
 * \param[in] ctx   Pointer to this instance's context
 *
 * \retval #AVM_CODEC_OK
 *     The codec algorithm initialized.
 * \retval #AVM_CODEC_MEM_ERROR
 *     Memory allocation failed.
 */
avm_codec_err_t avm_codec_destroy(avm_codec_ctx_t *ctx);

/*!\brief Get the capabilities of an algorithm.
 *
 * Retrieves the capabilities bitfield from the algorithm's interface.
 *
 * \param[in] iface   Pointer to the algorithm interface
 *
 */
avm_codec_caps_t avm_codec_get_caps(avm_codec_iface_t *iface);

/*!\name Codec Control
 *
 * The avm_codec_control function exchanges algorithm specific data with the
 * codec instance. Additionally, the macro AVM_CODEC_CONTROL_TYPECHECKED is
 * provided, which will type-check the parameter against the control ID before
 * calling avm_codec_control - note that this macro requires the control ID
 * to be directly encoded in it, e.g.,
 * AVM_CODEC_CONTROL_TYPECHECKED(&ctx, AVME_SET_CPUUSED, 8).
 *
 * The codec control IDs can be found in avm.h, avmcx.h, and avmdx.h
 * (defined as avm_com_control_id, avme_enc_control_id, and avm_dec_control_id).
 * @{
 */
/*!\brief Algorithm Control
 *
 * avm_codec_control takes a context, a control ID, and a third parameter
 * (with varying type). If the context is non-null and an error occurs,
 * ctx->err will be set to the same value as the return value.
 *
 * \param[in]     ctx              Pointer to this instance's context
 * \param[in]     ctrl_id          Algorithm specific control identifier
 *
 * \retval #AVM_CODEC_OK
 *     The control request was processed.
 * \retval #AVM_CODEC_ERROR
 *     The control request was not processed.
 * \retval #AVM_CODEC_INVALID_PARAM
 *     The data was not valid.
 */
avm_codec_err_t avm_codec_control(avm_codec_ctx_t *ctx, int ctrl_id, ...);

/*!\brief Key & Value API
 *
 * avm_codec_set_option() takes a context, a key (option name) and a value. If
 * the context is non-null and an error occurs, ctx->err will be set to the same
 * value as the return value.
 *
 * \param[in]     ctx              Pointer to this instance's context
 * \param[in]     name             The name of the option (key)
 * \param[in]     value            The value of the option
 *
 * \retval #AVM_CODEC_OK
 *     The value of the option was set.
 * \retval #AVM_CODEC_INVALID_PARAM
 *     The data was not valid.
 * \retval #AVM_CODEC_ERROR
 *     The option was not successfully set.
 */
avm_codec_err_t avm_codec_set_option(avm_codec_ctx_t *ctx, const char *name,
                                     const char *value);

/*!\brief avm_codec_control wrapper macro (adds type-checking, less flexible)
 *
 * This macro allows for type safe conversions across the variadic parameter
 * to avm_codec_control(). However, it requires the explicit control ID
 * be passed in (it cannot be passed in via a variable) -- otherwise a compiler
 * error will occur. After the type checking, it calls avm_codec_control.
 */
#define AVM_CODEC_CONTROL_TYPECHECKED(ctx, id, data) \
  avm_codec_control_typechecked_##id(ctx, id, data) /**<\hideinitializer*/

/*!\brief Creates typechecking mechanisms for avm_codec_control
 *
 * It defines a static function with the correctly typed arguments as a wrapper
 * to the type-unsafe avm_codec_control function. It also creates a typedef
 * for each type.
 */
#define AVM_CTRL_USE_TYPE(id, typ)                           \
  static avm_codec_err_t avm_codec_control_typechecked_##id( \
      avm_codec_ctx_t *, int, typ) AVM_UNUSED;               \
  static avm_codec_err_t avm_codec_control_typechecked_##id( \
      avm_codec_ctx_t *ctx, int ctrl, typ data) {            \
    return avm_codec_control(ctx, ctrl, data);               \
  } /**<\hideinitializer*/                                   \
  typedef typ avm_codec_control_type_##id;
/*!@} end Codec Control group */

/*!\brief OBU types. */
typedef enum ATTRIBUTE_PACKED {
  OBU_SEQUENCE_HEADER = 1,
  OBU_TEMPORAL_DELIMITER,
#if CONFIG_MULTI_FRAME_HEADER
  OBU_MULTI_FRAME_HEADER,
#endif  // CONFIG_MULTI_FRAME_HEADER
#if CONFIG_F024_KEYOBU
  OBU_CLK,
  OBU_OLK,
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_F024_KEYOBU
  OBU_LEADING_TILE_GROUP,
  OBU_REGULAR_TILE_GROUP,
#else
  OBU_TILE_GROUP,
#endif  // CONFIG_F024_KEYOBU
#if !CONFIG_METADATA
  OBU_METADATA,
#else
  OBU_METADATA_SHORT,
  OBU_METADATA_GROUP,
#endif  // CONFIG_METADATA
  OBU_SWITCH,
#if CONFIG_F024_KEYOBU
  OBU_LEADING_SEF,
  OBU_REGULAR_SEF,
#else
  OBU_SEF,
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_F024_KEYOBU
  OBU_LEADING_TIP,
  OBU_REGULAR_TIP,
#else
  OBU_TIP,
#endif  // CONFIG_F024_KEYOBU
  OBU_BUFFER_REMOVAL_TIMING,
  OBU_LAYER_CONFIGURATION_RECORD,
  OBU_ATLAS_SEGMENT,
  OBU_OPERATING_POINT_SET,
  OBU_BRIDGE_FRAME,
  // Multi-stream decoder operation OBU
  OBU_MSDO,
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  OBU_RAS_FRAME,
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_F255_QMOBU
  OBU_QM,
#endif  // CONFIG_F255_QMOBU
#if CONFIG_F153_FGM_OBU
  OBU_FGM,
#endif  // CONFIG_F153_FGM_OBU
#if CONFIG_CWG_F270_CI_OBU
  OBU_CONTENT_INTERPRETATION,
#endif  // CONFIG_CWG_F270_CI_OBU
  OBU_PADDING,
#if CONFIG_F024_KEYOBU
  NUM_OBU_TYPES
#endif
} OBU_TYPE;

/*!\brief OBU metadata types. */
typedef enum {
  OBU_METADATA_TYPE_AVM_RESERVED_0 = 0,
  OBU_METADATA_TYPE_HDR_CLL = 1,
  OBU_METADATA_TYPE_HDR_MDCV = 2,
  OBU_METADATA_TYPE_SCALABILITY = 3,
  OBU_METADATA_TYPE_ITUT_T35 = 4,
  OBU_METADATA_TYPE_TIMECODE = 5,
  OBU_METADATA_TYPE_DECODED_FRAME_HASH = 6,
  OBU_METADATA_TYPE_BANDING_HINTS = 7,
#if CONFIG_ICC_METADATA
  OBU_METADATA_TYPE_ICC_PROFILE = 8,
#endif  // CONFIG_ICC_METADATA
#if CONFIG_SCAN_TYPE_METADATA
  OBU_METADATA_TYPE_SCAN_TYPE = 9,
#endif  // CONFIG_SCAN_TYPE_METADATA
#if CONFIG_CWG_F430
  OBU_METADATA_TYPE_TEMPORAL_POINT_INFO = 10,
#endif  // CONFIG_CWG_F430
#if CONFIG_ICC_METADATA
  NUM_OBU_METADATA_TYPES,
#endif  // CONFIG_ICC_METADATA
} OBU_METADATA_TYPE;

/*!\brief Returns string representation of OBU_TYPE.
 *
 * \param[in]     type            The OBU_TYPE to convert to string.
 */
const char *avm_obu_type_to_string(OBU_TYPE type);

/*!@} - end defgroup codec*/
#ifdef __cplusplus
}
#endif
#endif  // AVM_AVM_AVM_CODEC_H_
