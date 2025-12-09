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

/*!\file
 * \brief Describes the aom image descriptor and associated operations
 *
 */
#ifndef AOM_AOM_AOM_IMAGE_H_
#define AOM_AOM_AOM_IMAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "aom/aom_integer.h"
#include "config/aom_config.h"

/*!\brief Current ABI version number
 *
 * \internal
 * If this file is altered in any way that changes the ABI, this value
 * must be bumped.  Examples include, but are not limited to, changing
 * types, removing or reassigning enums, adding/removing/rearranging
 * fields to structures
 */
#define AOM_IMAGE_ABI_VERSION (9) /**<\hideinitializer*/

#define AOM_IMG_FMT_PLANAR 0x100  /**< Image is a planar format. */
#define AOM_IMG_FMT_UV_FLIP 0x200 /**< V plane precedes U in memory. */
/** 0x400 used to signal alpha channel, skipping for backwards compatibility. */
#define AOM_IMG_FMT_HIGHBITDEPTH 0x800 /**< Image uses 16bit framebuffer. */

/*!\brief List of supported image formats */
typedef enum aom_img_fmt {
  AOM_IMG_FMT_NONE,
  AOM_IMG_FMT_YV12 =
      AOM_IMG_FMT_PLANAR | AOM_IMG_FMT_UV_FLIP | 1, /**< planar YVU */
  AOM_IMG_FMT_I420 = AOM_IMG_FMT_PLANAR | 2,
  AOM_IMG_FMT_AOMYV12 = AOM_IMG_FMT_PLANAR | AOM_IMG_FMT_UV_FLIP |
                        3, /** < planar 4:2:0 format with aom color space */
  AOM_IMG_FMT_AOMI420 = AOM_IMG_FMT_PLANAR | 4,
  AOM_IMG_FMT_I422 = AOM_IMG_FMT_PLANAR | 5,
  AOM_IMG_FMT_I444 = AOM_IMG_FMT_PLANAR | 6,
  AOM_IMG_FMT_I42016 = AOM_IMG_FMT_I420 | AOM_IMG_FMT_HIGHBITDEPTH,
  AOM_IMG_FMT_YV1216 = AOM_IMG_FMT_YV12 | AOM_IMG_FMT_HIGHBITDEPTH,
  AOM_IMG_FMT_I42216 = AOM_IMG_FMT_I422 | AOM_IMG_FMT_HIGHBITDEPTH,
  AOM_IMG_FMT_I44416 = AOM_IMG_FMT_I444 | AOM_IMG_FMT_HIGHBITDEPTH,
} aom_img_fmt_t; /**< alias for enum aom_img_fmt */

/*!\brief List of supported color primaries */
typedef enum aom_color_primaries {
  AOM_CICP_CP_RESERVED_0 = 0,  /**< For future use */
  AOM_CICP_CP_BT_709 = 1,      /**< BT.709 */
  AOM_CICP_CP_UNSPECIFIED = 2, /**< Unspecified */
  AOM_CICP_CP_RESERVED_3 = 3,  /**< For future use */
  AOM_CICP_CP_BT_470_M = 4,    /**< BT.470 System M (historical) */
  AOM_CICP_CP_BT_470_B_G = 5,  /**< BT.470 System B, G (historical) */
  AOM_CICP_CP_BT_601 = 6,      /**< BT.601 */
  AOM_CICP_CP_SMPTE_240 = 7,   /**< SMPTE 240 */
  AOM_CICP_CP_GENERIC_FILM =
      8, /**< Generic film (color filters using illuminant C) */
  AOM_CICP_CP_BT_2020 = 9,      /**< BT.2020, BT.2100 */
  AOM_CICP_CP_XYZ = 10,         /**< SMPTE 428 (CIE 1921 XYZ) */
  AOM_CICP_CP_SMPTE_431 = 11,   /**< SMPTE RP 431-2 */
  AOM_CICP_CP_SMPTE_432 = 12,   /**< SMPTE EG 432-1  */
  AOM_CICP_CP_RESERVED_13 = 13, /**< For future use (values 13 - 21)  */
  AOM_CICP_CP_EBU_3213 = 22,    /**< EBU Tech. 3213-E  */
  AOM_CICP_CP_RESERVED_23 = 23  /**< For future use (values 23 - 255)  */
} aom_color_primaries_t;        /**< alias for enum aom_color_primaries */

/*!\brief List of supported transfer functions */
typedef enum aom_transfer_characteristics {
  AOM_CICP_TC_RESERVED_0 = 0,  /**< For future use */
  AOM_CICP_TC_BT_709 = 1,      /**< BT.709 */
  AOM_CICP_TC_UNSPECIFIED = 2, /**< Unspecified */
  AOM_CICP_TC_RESERVED_3 = 3,  /**< For future use */
  AOM_CICP_TC_BT_470_M = 4,    /**< BT.470 System M (historical)  */
  AOM_CICP_TC_BT_470_B_G = 5,  /**< BT.470 System B, G (historical) */
  AOM_CICP_TC_BT_601 = 6,      /**< BT.601 */
  AOM_CICP_TC_SMPTE_240 = 7,   /**< SMPTE 240 M */
  AOM_CICP_TC_LINEAR = 8,      /**< Linear */
  AOM_CICP_TC_LOG_100 = 9,     /**< Logarithmic (100 : 1 range) */
  AOM_CICP_TC_LOG_100_SQRT10 =
      10,                     /**< Logarithmic (100 * Sqrt(10) : 1 range) */
  AOM_CICP_TC_IEC_61966 = 11, /**< IEC 61966-2-4 */
  AOM_CICP_TC_BT_1361 = 12,   /**< BT.1361 */
  AOM_CICP_TC_SRGB = 13,      /**< sRGB or sYCC*/
  AOM_CICP_TC_BT_2020_10_BIT = 14, /**< BT.2020 10-bit systems */
  AOM_CICP_TC_BT_2020_12_BIT = 15, /**< BT.2020 12-bit systems */
  AOM_CICP_TC_SMPTE_2084 = 16,     /**< SMPTE ST 2084, ITU BT.2100 PQ */
  AOM_CICP_TC_SMPTE_428 = 17,      /**< SMPTE ST 428 */
  AOM_CICP_TC_HLG = 18,            /**< BT.2100 HLG, ARIB STD-B67 */
  AOM_CICP_TC_RESERVED_19 = 19     /**< For future use (values 19-255) */
} aom_transfer_characteristics_t;  /**< alias for enum
                                      aom_transfer_characteristics */

/*!\brief List of supported matrix coefficients */
typedef enum aom_matrix_coefficients {
  AOM_CICP_MC_IDENTITY = 0,    /**< Identity matrix */
  AOM_CICP_MC_BT_709 = 1,      /**< BT.709 */
  AOM_CICP_MC_UNSPECIFIED = 2, /**< Unspecified */
  AOM_CICP_MC_RESERVED_3 = 3,  /**< For future use */
  AOM_CICP_MC_FCC = 4,         /**< US FCC 73.628 */
  AOM_CICP_MC_BT_470_B_G = 5,  /**< BT.470 System B, G (historical) */
  AOM_CICP_MC_BT_601 = 6,      /**< BT.601 */
  AOM_CICP_MC_SMPTE_240 = 7,   /**< SMPTE 240 M */
  AOM_CICP_MC_SMPTE_YCGCO = 8, /**< YCgCo */
  AOM_CICP_MC_BT_2020_NCL =
      9, /**< BT.2020 non-constant luminance, BT.2100 YCbCr  */
  AOM_CICP_MC_BT_2020_CL = 10, /**< BT.2020 constant luminance */
  AOM_CICP_MC_SMPTE_2085 = 11, /**< SMPTE ST 2085 YDzDx */
  AOM_CICP_MC_CHROMAT_NCL =
      12, /**< Chromaticity-derived non-constant luminance */
  AOM_CICP_MC_CHROMAT_CL = 13,  /**< Chromaticity-derived constant luminance */
  AOM_CICP_MC_ICTCP = 14,       /**< BT.2100 ICtCp */
  AOM_CICP_MC_IPT_C2 = 15,      /**< IPT-C2  */
  AOM_CICP_MC_YCGCO_RE = 16,    /**< YCgCo-Re */
  AOM_CICP_MC_YCGCO_RO = 17,    /**< YCgCo-Ro */
  AOM_CICP_MC_RESERVED_18 = 18, /**< For future use (values 18-255)  */
} aom_matrix_coefficients_t;    /**< alias for enum aom_matrix_coefficients */

/*!\brief List of supported color range */
typedef enum aom_color_range {
  AOM_CR_STUDIO_RANGE = 0, /**< Y [16..235], UV [16..240] */
  AOM_CR_FULL_RANGE = 1    /**< YUV/RGB [0..255] */
} aom_color_range_t;       /**< alias for enum aom_color_range */

/*!\brief List of chroma sample positions, specified by offsets from (0, 0)
 * luma sample */
// clang-format off
typedef enum aom_chroma_sample_position {
  AOM_CSP_UNSPECIFIED = 6,      /**< Unknown or determined by the application */
  AOM_CSP_LEFT = 0,             /**< 4:2:2: horizontal offset 0. */
                                /**< 4:2:0: horizontal offset 0, */
                                /**<        vertical offset 0.5. */
  AOM_CSP_CENTER = 1,           /**< 4:2:2: horizontal offset 0.5. */
                                /**< 4:2:0: horizontal offset 0.5, */
                                /**<        vertical offset 0.5. */
  AOM_CSP_TOPLEFT = 2,          /**< 4:2:2: N/A. */
                                /**< 4:2:0: horizontal offset 0, */
                                /**<        vertical offset 0. */
  AOM_CSP_TOP = 3,              /**< 4:2:2: N/A. */
                                /**< 4:2:0: horizontal offset 0.5, */
                                /**<        vertical offset 0. */
  AOM_CSP_BOTTOMLEFT = 4,       /**< 4:2:2: N/A. */
                                /**< 4:2:0: horizontal offset 0, */
                                /**<        vertical offset 1. */
  AOM_CSP_BOTTOM = 5,           /**< 4:2:2: N/A. */
                                /**< 4:2:0: horizontal offset 0.5, */
                                /**<        vertical offset 1. */
} aom_chroma_sample_position_t; /**< alias for enum aom_chroma_sample_position
                                 */
// clang-format on

#if CONFIG_CWG_F270_CI_OBU
/*!\brief List of Sample aspect ratio.
 This list is specified in H.273 8.6 Sample aspect ratio indicator*/
typedef enum aom_sample_aspect_ratio {
  AOM_SAR_IDC_UNSPECIFIED = 0,  // Unspecified
  AOM_SAR_IDC_1_TO_1 = 1,       // 1:1
  AOM_SAR_IDC_12_TO_11 = 2,     // 12:11
  AOM_SAR_IDC_10_TO_11 = 3,     // 10:11
  AOM_SAR_IDC_16_TO_11 = 4,     // 16:11
  AOM_SAR_IDC_40_TO_33 = 5,     // 40:33
  AOM_SAR_IDC_24_TO_11 = 6,     // 24:11
  AOM_SAR_IDC_20_TO_11 = 7,     // 20:11
  AOM_SAR_IDC_32_TO_11 = 8,     // 32:11
  AOM_SAR_IDC_80_TO_33 = 9,     // 80:33
  AOM_SAR_IDC_18_TO_11 = 10,    // 18:11
  AOM_SAR_IDC_15_TO_11 = 11,    // 15:11
  AOM_SAR_IDC_64_TO_33 = 12,    // 64:33
  AOM_SAR_IDC_160_TO_99 = 13,   // 160:99
  AOM_SAR_IDC_4_TO_3 = 14,      // 4:3
  AOM_SAR_IDC_3_TO_2 = 15,      // 3:2
  AOM_SAR_IDC_2_TO_1 = 16,      // 2:1
  AOM_SAR_IDC_255 = 255         //  EXPLICIT_SAR
} aom_sample_aspect_ratio_t;

/*!\brief List of Color description idc */
typedef enum aom_color_description {
  AOM_COLOR_DESC_IDC_EXPLICIT = 0,   // Explicitly signaled
  AOM_COLOR_DESC_IDC_BT709SDR = 1,   // CP=1, TC=1, MC=5
  AOM_COLOR_DESC_IDC_BT2100PQ = 2,   // CP=9, TC=16, MC=9
  AOM_COLOR_DESC_IDC_BT2100HLG = 3,  // CP=9, TC=14, MC=9
  AOM_COLOR_DESC_IDC_SRGB = 4,       // CP=1, TC=13, MC=0
  AOM_COLOR_DESC_IDC_SRGBSYCC = 5,   // CP=1, TC=13, MC=5
} aom_color_description_t;
#endif  // CONFIG_CWG_F270_CI_OBU

/*!\brief List of insert flags for Metadata
 *
 * These flags control how the library treats metadata during encode.
 *
 * While encoding, when metadata is added to an aom_image via
 * aom_img_add_metadata(), the flag passed along with the metadata will
 * determine where the metadata OBU will be placed in the encoded OBU stream.
 * Metadata will be emitted into the output stream within the next temporal unit
 * if it satisfies the specified insertion flag.
 *
 * During decoding, when the library encounters a metadata OBU, it is always
 * flagged as AOM_MIF_ANY_FRAME and emitted with the next output aom_image.
 */
typedef enum aom_metadata_insert_flags {
  AOM_MIF_NON_KEY_FRAME = 0, /**< Adds metadata if it's not keyframe */
  AOM_MIF_KEY_FRAME = 1,     /**< Adds metadata only if it's a keyframe */
  AOM_MIF_ANY_FRAME = 2      /**< Adds metadata to any type of frame */
} aom_metadata_insert_flags_t;

/*!\brief Array of aom_metadata structs for an image. */
typedef struct aom_metadata_array aom_metadata_array_t;

#if CONFIG_METADATA
/*!\brief Metadata necessity indicator
 *
 * Indicates the importance level of the metadata for proper decoding
 * and display of the content.
 */
typedef enum aom_metadata_necessity {
  AOM_NECESSITY_UNDEFINED = 0,
  AOM_NECESSITY_NECESSARY = 1,
  AOM_NECESSITY_ADVISORY = 2,
  AOM_NECESSITY_MIXED = 3,
} aom_metadata_necessity_t;

/*!\brief Metadata application identifier
 *
 * Specifies the target application or device type for which the metadata
 * is intended.
 */
typedef enum aom_metadata_application_id {
  AOM_APPID_UNDEFINED = 0,
  AOM_APPID_MOBILE_OR_TV = 1,
  AOM_APPID_MOBILE = 2,
  AOM_APPID_TV = 3,
  AOM_APPID_HMD = 4,
  AOM_APPID_WEARABLE = 5,
  // 6-15 are reserved for AOM use
  // 16-31 are externally defined
} aom_metadata_application_id_t;

#if CONFIG_SCAN_TYPE_METADATA
/*!\brief Metadata Picture Scan Type
 *
 * Specifies the picture scan type is intended.
 */
typedef enum aom_pic_scan_type_t {
  AOM_SCAN_TYPE_UNSPECIFIED = 0,
  AOM_SCAN_TYPE_PROGRESSIVE = 1,  // PROGRESSIVE_FRAME_PICTURE_SAMPLES
  AOM_SCAN_TYPE_INTERLACE = 2,    // INTERLACE_FIELD_PICTURE_SAMPLES
  AOM_SCAN_TYPE_INTERLACE_COMPLEMENTARY =
      3,  // INTERLACE_COMPLEMENTARY_FIELD_PAIR_PICTURE_SAMPLES
  AOM_NUM_SCAN_TYPES = 4,
} aom_pic_scan_type_t;

/*!\brief Metadata Picture Structure type
 *
 * Specifies the picture type.
 */
typedef enum aom_pic_struct_type_t {
  AOM_PIC_FRAME = 0,
  AOM_PIC_TOP_FIELD = 1,
  AOM_PIC_BOTTOM_FIELD = 2,
  AOM_PIC_TOP_BOTTOM_FIELD = 3,
  AOM_PIC_BOTTOM_TOP_FIELD = 4,
  AOM_PIC_TOP_BOTTOM_TOP_FIELD = 5,     // Top field, bottom field, top field
  AOM_PIC_BOTTOM_TOP_BOTTOM_FIELD = 6,  // Bottom field, top field, bottom field
  AOM_PIC_FRAME_DOUBLING = 7,
  AOM_PIC_FRAME_TRIPLING = 8,
  AOM_PIC_TOP_PREV_BOTTOM_FIELD =
      9,  // Top field paired with previous bottomr field in output order
  AOM_PIC_BOTTOM_PREV_TOP_FIELD =
      10,  // Bottom field paried with previous top field in output order
  AOM_PIC_TOP_NEXT_TOP_FIELD =
      11,  // Top field paired with next bottom field is output order
  AOM_PIC_BOTTOM_NEXT_TOP_FIELD =
      12,  // Bottom field paired with next top field in output order
  AOM_NUM_PIC_STRUCT_TYPE = 13,
} aom_pic_struct_type_t;

/*!\brief Picture Struct Metadata payload. */
typedef struct aom_metadata_pic_struct_t {
  aom_pic_struct_type_t mps_pic_struct_type;    /**< picture struct*/
  aom_pic_scan_type_t mps_source_scan_type_idc; /**< source scan type*/
  int mps_duplicate_flag;                       /**< frame duplicate */
} aom_metadata_pic_struct_t;
#endif  // CONFIG_SCAN_TYPE_METADATA

#if CONFIG_CWG_F430
/*!\brief Temporal Point Info Metadata payload.
 *
 * Contains the frame presentation time for decoder model timing
 */
typedef struct aom_metadata_temporal_point_info_t {
  uint32_t mtpi_frame_presentation_time; /**< Frame presentation time in clock
                                            ticks*/
  int mtpi_frame_presentation_length;    /**< Frame presentation time length*/
} aom_metadata_temporal_point_info_t;
#endif  // CONFIG_CWG_F430

/*!\brief Metadata persistence behavior
 *
 * Defines how long the metadata should remain valid and applicable
 * to subsequent frames in the bitstream.
 */
typedef enum aom_metadata_persistence {
  AOM_GLOBAL_PERSISTENCE = 0,
  AOM_BASIC_PERSISTENCE = 1,
  AOM_NO_PERSISTENCE = 2,
  AOM_ENHANCED_PERSISTENCE = 3,
  // 4-15 are reserved for AOM use
} aom_metadata_persistence_t;
/*!\brief Metadata layer applicability
 *
 * Specifies which layers or layer groups the metadata applies to
 * in scalable video coding scenarios.
 */
typedef enum aom_metadata_layer {
  AOM_LAYER_UNSPECIFIED = 0,
  AOM_LAYER_GLOBAL = 1,
  AOM_LAYER_CURRENT = 2,
  AOM_LAYER_VALUES = 3,
  // 4-15 are reserved for AOM use
} aom_metadata_layer_t;
#endif  // CONFIG_METADATA

/*!\brief Metadata payload. */
typedef struct aom_metadata {
  uint32_t type;                           /**< Metadata type */
  uint8_t *payload;                        /**< Metadata payload data */
  size_t sz;                               /**< Metadata payload size */
  aom_metadata_insert_flags_t insert_flag; /**< Metadata insertion flag */
#if CONFIG_METADATA
  uint8_t is_suffix;                            /**< Metadata suffix flag */
  aom_metadata_necessity_t necessity_idc;       /**< Metadata necessity */
  aom_metadata_application_id_t application_id; /**< Metadata application id */
  uint8_t cancel_flag;                          /**< Metadata cancel flag */
  uint8_t priority;                             /**< Metadata priority */
  aom_metadata_persistence_t persistence_idc;   /**< Metadata persistence */
  aom_metadata_layer_t layer_idc;               /**< Metadata layers mode */
  uint32_t xlayer_map;                          /**< Metadata x_layer mapping */
  uint8_t mlayer_map[31];                       /**< Metadata m_layer mapping */
#endif                                          // CONFIG_METADATA

} aom_metadata_t;

/**\brief Image Descriptor */
typedef struct aom_image {
  aom_img_fmt_t fmt;                 /**< Image Format */
  aom_color_primaries_t cp;          /**< CICP Color Primaries */
  aom_transfer_characteristics_t tc; /**< CICP Transfer Characteristics */
  aom_matrix_coefficients_t mc;      /**< CICP Matrix Coefficients */
  int monochrome;                    /**< Whether image is monochrome */
  aom_chroma_sample_position_t csp;  /**< chroma sample position */
  aom_color_range_t range;           /**< Color Range */

  /* Image storage dimensions */
  unsigned int w;         /**< Stored image width */
  unsigned int h;         /**< Stored image height */
  unsigned int bit_depth; /**< Stored image bit-depth */

#if CONFIG_CROP_WIN_CWG_F220
  /* Cropping dimensions */
  int w_conf_win_enabled_flag;  /**< conformance window enable flag */
  int w_conf_win_left_offset;   /**< Conformance window left offset */
  int w_conf_win_right_offset;  /**< Conformance window right offset  */
  int w_conf_win_top_offset;    /**< Conformance window top offset */
  int w_conf_win_bottom_offset; /**< Conformance window bottom offset */
  int max_width;                /**< Conformance window max width */
  int max_height;               /**< Conformance window max height */
  int crop_width;               /**< Conformance window width */
  int crop_height;              /**< Conformance window height */
#endif                          // CONFIG_CROP_WIN_CWG_F220

  /* Image display dimensions */
  unsigned int d_w; /**< Displayed image width */
  unsigned int d_h; /**< Displayed image height */

  /* Image intended rendering dimensions */
  unsigned int r_w; /**< Intended rendering image width */
  unsigned int r_h; /**< Intended rendering image height */

  /* Chroma subsampling info */
  unsigned int x_chroma_shift; /**< subsampling order, X */
  unsigned int y_chroma_shift; /**< subsampling order, Y */

/* Image data pointers. */
#define AOM_PLANE_PACKED 0  /**< To be used for all packed formats */
#define AOM_PLANE_Y 0       /**< Y (Luminance) plane */
#define AOM_PLANE_U 1       /**< U (Chroma) plane */
#define AOM_PLANE_V 2       /**< V (Chroma) plane */
  unsigned char *planes[3]; /**< pointer to the top left pixel for each plane */
  int stride[3];            /**< stride between rows for each plane */
  size_t sz;                /**< data size */

  int bps; /**< bits per sample (for packed formats) */

  int tlayer_id; /**< tlayer id of image */
  int mlayer_id; /**< mlayer id of image */
  int xlayer_id; /**< xlayer id of image */

  /*!\brief The following member may be set by the application to associate
   * data with this image.
   */
  void *user_priv;

  /* The following members should be treated as private. */
  unsigned char *img_data; /**< private */
  int img_data_owner;      /**< private */
  int self_allocd;         /**< private */

  aom_metadata_array_t
      *metadata; /**< Metadata payloads associated with the image. */

  void *fb_priv; /**< Frame buffer data associated with the image. */
} aom_image_t;   /**< alias for struct aom_image */

/*!\brief Open a descriptor, allocating storage for the underlying image
 *
 * Returns a descriptor for storing an image of the given format. The
 * storage for the image is allocated on the heap.
 *
 * \param[in]    img       Pointer to storage for descriptor. If this parameter
 *                         is NULL, the storage for the descriptor will be
 *                         allocated on the heap.
 * \param[in]    fmt       Format for the image
 * \param[in]    d_w       Width of the image
 * \param[in]    d_h       Height of the image
 * \param[in]    align     Alignment, in bytes, of the image buffer and
 *                         each row in the image (stride).
 *
 * \return Returns a pointer to the initialized image descriptor. If the img
 *         parameter is non-null, the value of the img parameter will be
 *         returned.
 */
aom_image_t *aom_img_alloc(aom_image_t *img, aom_img_fmt_t fmt,
                           unsigned int d_w, unsigned int d_h,
                           unsigned int align);

/*!\brief Open a descriptor, using existing storage for the underlying image
 *
 * Returns a descriptor for storing an image of the given format. The
 * storage for the image has been allocated elsewhere, and a descriptor is
 * desired to "wrap" that storage.
 *
 * \param[in]    img       Pointer to storage for descriptor. If this parameter
 *                         is NULL, the storage for the descriptor will be
 *                         allocated on the heap.
 * \param[in]    fmt       Format for the image
 * \param[in]    d_w       Width of the image
 * \param[in]    d_h       Height of the image
 * \param[in]    align     Alignment, in bytes, of each row in the image
 *                         (stride).
 * \param[in]    img_data  Storage to use for the image
 *
 * \return Returns a pointer to the initialized image descriptor. If the img
 *         parameter is non-null, the value of the img parameter will be
 *         returned.
 */
aom_image_t *aom_img_wrap(aom_image_t *img, aom_img_fmt_t fmt, unsigned int d_w,
                          unsigned int d_h, unsigned int align,
                          unsigned char *img_data);

/*!\brief Open a descriptor, allocating storage for the underlying image with a
 * border
 *
 * Returns a descriptor for storing an image of the given format and its
 * borders. The storage for the image is allocated on the heap.
 *
 * \param[in]    img        Pointer to storage for descriptor. If this parameter
 *                          is NULL, the storage for the descriptor will be
 *                          allocated on the heap.
 * \param[in]    fmt        Format for the image
 * \param[in]    d_w        Width of the image
 * \param[in]    d_h        Height of the image
 * \param[in]    align      Alignment, in bytes, of the image buffer and
 *                          each row in the image (stride).
 * \param[in]    size_align Alignment, in pixels, of the image width and height.
 * \param[in]    border     A border that is padded on four sides of the image.
 *
 * \return Returns a pointer to the initialized image descriptor. If the img
 *         parameter is non-null, the value of the img parameter will be
 *         returned.
 */
aom_image_t *aom_img_alloc_with_border(aom_image_t *img, aom_img_fmt_t fmt,
                                       unsigned int d_w, unsigned int d_h,
                                       unsigned int align,
                                       unsigned int size_align,
                                       unsigned int border);

/*!\brief Set the rectangle identifying the displayed portion of the image
 *
 * Updates the displayed rectangle (aka viewport) on the image surface to
 * match the specified coordinates and size.
 *
 * \param[in]    img       Image descriptor
 * \param[in]    x         leftmost column
 * \param[in]    y         topmost row
 * \param[in]    w         width
 * \param[in]    h         height
 * \param[in]    border    A border that is padded on four sides of the image.
 *
 * \return 0 if the requested rectangle is valid, nonzero otherwise.
 */
int aom_img_set_rect(aom_image_t *img, unsigned int x, unsigned int y,
                     unsigned int w, unsigned int h, unsigned int border);

/*!\brief Flip the image vertically (top for bottom)
 *
 * Adjusts the image descriptor's pointers and strides to make the image
 * be referenced upside-down.
 *
 * \param[in]    img       Image descriptor
 */
void aom_img_flip(aom_image_t *img);

/*!\brief Close an image descriptor
 *
 * Frees all allocated storage associated with an image descriptor.
 *
 * \param[in]    img       Image descriptor
 */
void aom_img_free(aom_image_t *img);

/*!\brief Get the width of a plane
 *
 * Get the width of a plane of an image
 *
 * \param[in]    img       Image descriptor
 * \param[in]    plane     Plane index
 */
int aom_img_plane_width(const aom_image_t *img, int plane);

/*!\brief Get the height of a plane
 *
 * Get the height of a plane of an image
 *
 * \param[in]    img       Image descriptor
 * \param[in]    plane     Plane index
 */
int aom_img_plane_height(const aom_image_t *img, int plane);

/*!\brief Add metadata to image.
 *
 * Adds metadata to aom_image_t.
 * Function makes a copy of the provided data parameter.
 * Metadata insertion point is controlled by insert_flag.
 *
 * \param[in]    img          Image descriptor
 * \param[in]    type         Metadata type
 * \param[in]    data         Metadata contents
 * \param[in]    sz           Metadata contents size
 * \param[in]    insert_flag  Metadata insert flag
 *
 * \return Returns 0 on success. If img or data is NULL, sz is 0, or memory
 * allocation fails, it returns -1.
 */
int aom_img_add_metadata(aom_image_t *img, uint32_t type, const uint8_t *data,
                         size_t sz, aom_metadata_insert_flags_t insert_flag);

/*!\brief Return a metadata payload stored within the image metadata array.
 *
 * Gets the metadata (aom_metadata_t) at the indicated index in the image
 * metadata array.
 *
 * \param[in] img          Pointer to image descriptor to get metadata from
 * \param[in] index        Metadata index to get from metadata array
 *
 * \return Returns a const pointer to the selected metadata, if img and/or index
 * is invalid, it returns NULL.
 */
const aom_metadata_t *aom_img_get_metadata(const aom_image_t *img,
                                           size_t index);

/*!\brief Return the number of metadata blocks within the image.
 *
 * Gets the number of metadata blocks contained within the provided image
 * metadata array.
 *
 * \param[in] img          Pointer to image descriptor to get metadata number
 * from.
 *
 * \return Returns the size of the metadata array. If img or metadata is NULL,
 * it returns 0.
 */
size_t aom_img_num_metadata(const aom_image_t *img);

/*!\brief Remove metadata from image.
 *
 * Removes all metadata in image metadata list and sets metadata list pointer
 * to NULL.
 *
 * \param[in]    img       Image descriptor
 */
void aom_img_remove_metadata(aom_image_t *img);

/*!\brief Allocate memory for aom_metadata struct.
 *
 * Allocates storage for the metadata payload, sets its type and copies the
 * payload data into the aom_metadata struct. A metadata payload buffer of size
 * sz is allocated and sz bytes are copied from data into the payload buffer.
 *
 * \param[in]    type         Metadata type
 * \param[in]    data         Metadata data pointer
 * \param[in]    sz           Metadata size
 * \param[in]    insert_flag  Metadata insert flag
 *
 * \return Returns the newly allocated aom_metadata struct. If data is NULL,
 * sz is 0, or memory allocation fails, it returns NULL.
 */
aom_metadata_t *aom_img_metadata_alloc(uint32_t type, const uint8_t *data,
                                       size_t sz,
                                       aom_metadata_insert_flags_t insert_flag);

/*!\brief Free metadata struct.
 *
 * Free metadata struct and its buffer.
 *
 * \param[in]    metadata       Metadata struct pointer
 */
void aom_img_metadata_free(aom_metadata_t *metadata);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AOM_AOM_IMAGE_H_
