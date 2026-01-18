/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "av2/common/annexA.h"
#include "avm/internal/avm_codec_internal.h"
#include "config/avm_config.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/enums.h"

/* clang-format off */
/*
//=================================================================
// Table A.1: AV2 Multi-Sequence Configurations
//=================================================================
 *
 * Table A.1: AV2 Multi-Sequence Configurations
 *
 * ConfigurationID | Configuration Label | Toolset |  BitDepth (8/10/12/14/16) | Chroma Format (4:0:0/4:2:0/4:2:2/4:4:4)
 * ----------------|---------------------|---------|---------------------------|----------------------------------------
 *       0         | C_Main_420_10       | Main    | 8, 10                     |  4:0:0, 4:2:0
 *       1         | C_Main_422_10       | Main    | 8, 10                     |  4:0:0, 4:2:0, 4:2:2
 *       2         | C_Main_444_10       | Main    | 8, 10                     |  4:0:0, 4:2:0, 4:4:4
 *       3-63      | Reserved            | -       | -                         |

 *
 * Notes:
 * - ConfigurationID: Identifies the multi-sequence configuration(6-bit value(
 * - Chroma Format: Supplrted bit depths for this configuration
 * - Chroma Format: Support chroma subsampling formats
 * - Resered: ConfigurationID valies 3-64 are reserved for future use
 */
/* clang-format on */

#if CONFIG_CWG_F429_INTEROP
typedef enum {
  C_MAIN_420_10 = 0,  // Main toolset, 8/10-10-bit, 4:0:0/4:2:0
  C_MAIN_422_10 = 1,  // Main toolset, 8/10-10-bit, 4:0:0/4:2:0/4:2:2
  C_MAIN_444_10 = 2,  // Main toolset, 8/10-10-bit, 4:0:0/4:2:0/4:4:4
} AV2_CONFIGURATION_LABEL;

/* clang-format off */
/*=================================================================
// Table A.2: Allowed Values for Sub-Bitstream Syntax Elements
//=================================================================
 *
 * Table A.2: Allowed Values for Sub-Bitstream Syntax Elements Accoring to Multi-Sequence Configuration
 *
 *  Configuration Label    |   seq_profile_idc    |    chroma_format_idc    |    bit_depth_idc
 * ------------------------|----------------------|-------------------------|---------------
 *   C_Main_420_10         | 0..5                 | 0..1                    | 0..1
 *   C_Main_422_10         | 0..5                 | 0..1, 3                 | 0..1
 *   C_Main_444_10         | 0..5                 | 0..2                    | 0..1
 *
 * Notes:
 * - seq_profile_idc: Allowed profile values (0=MAIN_420_10_IP0, 1=MAIN_420_10_IP1, 2=MAIN_420_10_IP2, 3=Main_420_10,
 *                                            4=MAIN_422_10, 5=MAIN_444_10)
 * - bit_depth_idc: 0=8-bit, 1=10-bit
 * - C_Main_420_10: Supports profiles 0-3, chroma 4:0:0 and 4:2:0, bit depths 8 and 10
 * - C_Main_422_10: Supports profiles 0-3, chroma 4:0:0, 4:2:0 and 4:2:2, bit depth 8 and 10
 * - C_Main_444_10: Supports profiles 0-3, chroma 4:0:0, 4:2:0, and 4:4:4, bit depth 8 and 10
 */

// Interoperability Point Table (Table 2 from CWG-F429)
// Number of interoperability points (0-15)

// INTEROP_0: Max 4 extended, 1 embedded, no combinations
// INTEROP_1: Max 4 extended, 2 embedded, no combinations
// INTEROP_2: Max 4 extended, 3 embedded, combinations allowed
// INTEROP_3-14: Reserved
// INTEROP_15: Max values, combinations allowed
/* clang-format on */

typedef enum {
  INTEROP_0,
  INTEROP_1,
  INTEROP_2,
  INTEROP_3,
  MAX_INTEROP_POINTS,
} INTEROP_POINTS;

static const int InteropMaxEmbeddedLayers[NUM_INTEROP_POINTS] = {
  4,               // INTEROP_0
  4,               // INTEROP_1
  4,               // INTEROP_2
  -1,              // I// INTEROP_3 (reserved)
  -1,              // I// INTEROP_4 (reserved)
  -1,              // I// INTEROP_5 (reserved)
  -1,              // I// INTEROP_6 (reserved)
  -1,              // I// INTEROP_7 (reserved)
  -1,              // I// INTEROP_8 (reserved)
  -1,              // I// INTEROP_9 (reserved)
  -1,              // I// INTEROP_10 (reserved)
  -1,              // I// INTEROP_11 (reserved)
  -1,              // I// INTEROP_12 (reserved)
  -1,              // I// INTEROP_13 (reserved)
  -1,              // I// INTEROP_14 (reserved)
  MAX_NUM_XLAYERS  // INTEROP 15 (max)
};

static const int InteropCombinationsAllowed[NUM_INTEROP_POINTS] = {
  0,   // INTEROP_0: No
  0,   // INTEROP_0: No
  1,   // INTEROP_2: No
  -1,  // INTEROP_3: Yes
  -1,  // INTEROP_4: (reserved)
  -1,  // INTEROP_5: (reserved)
  -1,  // INTEROP_6: (reserved)
  -1,  // INTEROP_7: (reserved)
  -1,  // INTEROP_8: (reserved)
  -1,  // INTEROP_9: (reserved)
  -1,  // INTEROP_10: (reserved)
  -1,  // INTEROP_11: (reserved)
  -1,  // INTEROP_12: (reserved)
  -1,  // INTEROP_13: (reserved)
  -1,  // INTEROP_14: (reserved)
  1,   // INTEROP_15: Yes
};

static const int SeqProfileToInteropPoint[32] = {
  0,   // seq_profile_idc 0 -> INTEROP_0
  1,   // seq_profile_idc 1 -> INTEROP_1
  2,   // seq_profile_idc 2 -> INTEROP_2
  15,  // seq_profile_idc 3 -> INTEROP_15
};

static const int SeqProfileMaxMlayerCnt[32] = {
  1,                // seq_profile_idc 0
  2,                // seq_profile_idc 1
  3,                // seq_profile_idc 2
  MAX_NUM_MLAYERS,  // seq_profile_idc 3
  MAX_NUM_MLAYERS,  // seq_profile_idc 4
  MAX_NUM_MLAYERS,  // seq_profile_idc 5
  MAX_NUM_MLAYERS,  // seq_profile_idc 6
};

/* clang-format off */
/* Table A4 Allowed values for sub-bitstream syntax elements to conform to a specific AV2 profile
 *  Profile Label          |    seq_profile_idc    |    chroma_format_idc    |    bit_depth_idc    |    max_mlayer_cnt
 * -------------------------------------------------------------------------------------------------------------------
 *  Main_420_10_IP0                   0                  CHROMA_FORMAT_400          0 or 1                        1
 *                                                       CHROMA_FORMAT_420
 * -------------------------------------------------------------------------------------------------------------------
 *  Main_420_10_IP1                   1                  CHROMA_FORMAT_400          0 or 1                        2
 *                                                       CHROMA_FORMAT_420
 * --------------------------------------------------------------------------------------------------------------------
 *  Main_420_10_IP2                   2                  CHROMA_FORMAT_400          0 or 1                        3
 *                                                       CHROMA_FORMAT_420
 * --------------------------------------------------------------------------------------------------------------------
 *  Main_420_10                       3                  CHROMA_FORMAT_400          0 or 1                        -
 *                                                       CHROMA_FORMAT_420
 * ---------------------------------------------------------------------------------------------------------------------
 *  Main_422_10                       4                  CHROMA_FORMAT_400          0 or 1                        -
 *                                                       CHROMA_FORMAT_420
 *                                                       CHROMA_FORMAT_422
 * ---------------------------------------------------------------------------------------------------------------------
 *  Main_444_10                       5                  CHROMA_FORMAT_400          0 or 1                        -
 *                                                       CHROMA_FORMAT_420
 *                                                       CHROMA_FORMAT_444
 * ---------------------------------------------------------------------------------------------------------------------
 *  Reserved                         6-31
 * ---------------------------------------------------------------------------------------------------------------------
 */
/* clang-format on */

//=================================================================
// Profile Conformance Functions
//=================================================================
// Helper functions
// Get interoperability point from seq_profile_idc
// Return -1 for reserved seq_profile_idc values
static INLINE int av2_get_interop_point_from_profile(int seq_profile_idc) {
  if (seq_profile_idc < 0 || seq_profile_idc >= 32) return -1;
  return SeqProfileToInteropPoint[seq_profile_idc];
}

// Get max extended layers for a given interop point
// Returns -1 for reserved interoperability points
static INLINE int av2_get_interop_max_extended_layers(int interop_point) {
  if (interop_point < 0 || interop_point >= NUM_INTEROP_POINTS) return -1;
  return InteropMaxEmbeddedLayers[interop_point];
}

// Get max embedded layers for a given interoperability point
// Returns -1 for reserved interoperability points
static INLINE int av2_get_interop_max_embedded_layers(int interop_point) {
  if (interop_point < 0 || interop_point >= NUM_INTEROP_POINTS) return -1;
  return InteropMaxEmbeddedLayers[interop_point];
}

static INLINE int av2_get_interop_combinations_allowed(int interop_point) {
  if (interop_point < 0 || interop_point >= 32) return -1;
  return InteropCombinationsAllowed[interop_point];
}

static INLINE int av2_get_max_mlayer_cnt_from_profile(int seq_profile_idc) {
  if (seq_profile_idc < 0 || seq_profile_idc >= 32) return -1;
  return SeqProfileMaxMlayerCnt[seq_profile_idc];
}

int av2_validate_layer_capacity(int seq_profile_idc, int num_extended_layers,
                                int num_embedded_layers) {
  const int interop_point = av2_get_interop_point_from_profile(seq_profile_idc);
  if (interop_point < 0) return 0;

  // Check if layer combinations are allowed for this interop point
  const int combinations_allowed =
      av2_get_interop_combinations_allowed(interop_point);
  if (combinations_allowed < 0) return 0;
  if (combinations_allowed == 0) {
    if (num_extended_layers > 1 || num_embedded_layers > 1) return 0;
  }

  const int max_extended = av2_get_interop_max_extended_layers(interop_point);
  const int max_embedded = av2_get_interop_max_embedded_layers(interop_point);
  if (max_extended < 0 || max_embedded < 0)
    return 0;  // These are the reserved interop points
  if (num_extended_layers > max_extended) return 0;
  if (num_embedded_layers > max_embedded) return 0;

  return 1;
}

static int check_bit_depth_8_10(int bit_depth, int profile_idc,
                                struct avm_internal_error_info *error_info,
                                int is_decoder) {
  if (bit_depth != AVM_BITS_8 && bit_depth != AVM_BITS_10) {
    avm_internal_error(
        error_info,
        is_decoder ? AVM_CODEC_UNSUP_BITSTREAM : AVM_CODEC_INVALID_PARAM,
        "Profile %d only supports 8-bit and 10-bit.",
        (BITSTREAM_PROFILE)profile_idc);
    return 0;
  }
  return 1;
}

enum {
  TOOLSET_MAIN = 0,  // Main tool set
  // TOOLSET_MULTIVIEW = 1, // Example
  // TOOLSET_SCALABILITY = 2,  // Example
  TOOLSET_TYPES
};

static const char *const toolset_names[] = { "MAIN", "UNKNOWN" };
const char *get_toolset_name(int toolset) {
  if (toolset >= 0 && toolset < TOOLSET_TYPES) {
    return toolset_names[toolset];
  }
  return toolset_names[TOOLSET_TYPES];
}

int get_toolset_from_config_idc(int config_idc) {
  if (config_idc >= 0 && config_idc <= 2) {
    return TOOLSET_MAIN;
  }
  // future
  return TOOLSET_MAIN;
}

static int check_chroma_format(int monochrome, int is_420, int is_422,
                               int is_444, int allow_420, int allow_422,
                               int allow_444, int profile_idc,
                               struct avm_internal_error_info *error_info,
                               int is_decoder) {
  // Monochrome (4:0:0) is always allowed
  if (monochrome) {
    return 1;
  }

  // Check if the current chroma format is allowed
  if ((is_420 && allow_420) || (is_422 && allow_422) || (is_444 && allow_444)) {
    return 1;
  }

  // Build allowed formats string for error message
  const char *format_420 = allow_420 ? ", 4:2:0" : "";
  const char *format_422 = allow_422 ? ", 4:2:2" : "";
  const char *format_444 = allow_444 ? ", 4:4:4" : "";

  avm_internal_error(
      error_info,
      is_decoder ? AVM_CODEC_UNSUP_BITSTREAM : AVM_CODEC_INVALID_PARAM,
      "Profile %d only supports 4:0:0%s%s%s chroma.",
      (BITSTREAM_PROFILE)profile_idc, format_420, format_422, format_444);
  return 0;
}

static int check_mlayer_count(int profile_idc, int seq_max_mcount,
                              struct avm_internal_error_info *error_info,
                              int is_decoder) {
  // Only check for IP profiles (0, 1, 2)
  if (profile_idc != MAIN_420_10_IP0 && profile_idc != MAIN_420_10_IP1 &&
      profile_idc != MAIN_420_10_IP2) {
    return 1;
  }

  const int max_allowed_mcount =
      av2_get_max_mlayer_cnt_from_profile(profile_idc);
  if (max_allowed_mcount < 0) {
    avm_internal_error(
        error_info,
        is_decoder ? AVM_CODEC_UNSUP_BITSTREAM : AVM_CODEC_INVALID_PARAM,
        "Profile conformance error: max_allowed_count is below 0");
    return 0;
  }

  if (seq_max_mcount > max_allowed_mcount) {
    avm_internal_error(
        error_info,
        is_decoder ? AVM_CODEC_UNSUP_BITSTREAM : AVM_CODEC_INVALID_PARAM,
        "Profile conformance error: seq_max_mcount is exceeded.");
    return 0;
  }

  return 1;
}

// Checks the profile conformance -- MOST IMPORTANT FUNCTION
int av2_check_profile_interop_conformance(
    int profile, int bit_depth, int subsampling_x, int subsampling_y,
    int monochrome, int seq_max_mcount,
    struct avm_internal_error_info *error_info, int is_decoder) {
  const int is_420 = (subsampling_x == 1 && subsampling_y == 1);
  const int is_422 = (subsampling_x == 1 && subsampling_y == 0);
  const int is_444 = (subsampling_x == 0 && subsampling_y == 0);

  // All profiles support 8-bit and 10-bit only
  if (!check_bit_depth_8_10(bit_depth, profile, error_info, is_decoder)) {
    return 0;
  }

  switch (profile) {
    case MAIN_420_10_IP0:  // seq_profile_idc == 0
    case MAIN_420_10_IP1:  // seq_profile_idc == 1
    case MAIN_420_10_IP2:  // seq_profile_idc == 2
    case MAIN_420_10:      // seq_profile_idc == 3
      // All 420 profiles: allow only 4:2:0 and monochrome
      if (!check_chroma_format(monochrome, is_420, is_422, is_444, 1, 0,
                               0,  // allow_420 = 1, allow_422=0, allow_444=0
                               profile, error_info, is_decoder)) {
        return 0;
      }
      break;
    case MAIN_422_10:  // seq_profile_idc == 4
      // 422 profile: allow 4:2:0 and 4:2:2 and monochrome
      if (!check_chroma_format(monochrome, is_420, is_422, is_444, 1, 1,
                               0,  // allow_420 = 1, allow_422=1, allow_444=0
                               profile, error_info, is_decoder)) {
        return 0;
      }
      break;
    case MAIN_444_10:  // seq_profile_idc == 5
      // 444 profile: allow 4:2:0, 4:2:2, 4:4:4 and monochrome
      if (!check_chroma_format(monochrome, is_420, is_422, is_444, 1, 1,
                               1,  // allow_420 = 1, allow_422=1, allow_444=1
                               profile, error_info, is_decoder)) {
        return 0;
      }
      break;
    default:
      // Profile 6+ - reserved/unsupported
      return 0;
  }
  // Check if Max mlayer count is valid for IP profiles (seq_profile_idc <=2)
  if (!check_mlayer_count(profile, seq_max_mcount, error_info, is_decoder)) {
    return 0;
  }
  return 1;
}

/* clang-format off */
/*=================================================================
// Profile Scaling and Bitrate Functions
//=================================================================

 * Table A.5: Definition of ProfileScalingFactor
 * seq_profile_idc          | bit_depth_idc      |      chroma_format_idc       | ProfileScalingFactor
 * ----------------------------------------------------------------------------------------------------
 * (0, 1, 2, 3, 4, 5)            (0, 1)              CHROMA_FORMAT_400                       0
 *                                                   CHROMA_FORMAT_420
 * ----------------------------------------------------------------------------------------------------
 *      4                        (0, 1)              CHROMA_FORMAT_422                       1
 * ----------------------------------------------------------------------------------------------------
 *      5                        (0, 1)              CHROMA_FORMAT_444                      2
 * ----------------------------------------------------------------------------------------------------
 */
/* clang-format on */

int get_profile_scaling_factor(int seq_profile_idc, int chroma_format_idc) {
  // Table A.5: Definition of ProfileScalingFactor
  // Note that the bit_depth_idx must be 0 or 1 for all valid combinations

  // All profiles (0-5) with 400 or 420 chroma format
  if (chroma_format_idc == CHROMA_FORMAT_400 ||
      chroma_format_idc == CHROMA_FORMAT_420) {
    return 0;
  }

  // Profile 4 with 422 chroma format
  if (seq_profile_idc == 4 && chroma_format_idc == CHROMA_FORMAT_422) {
    return 1;
  }

  // Profile 5 with 444 chroma format
  if (seq_profile_idc == 5 && chroma_format_idc == CHROMA_FORMAT_444) {
    return 2;
  }

  // Default for invalid combinations
  return 0;
}

// Get BitrateProfileFactor from ProfileScalingFactor per AV2 spec
int get_bitrate_profile_factor(int profile_scaling_factor) {
  // Per spec:
  // If ProfileScalingFactor == 0, BitrateProfileFactor = 1.0
  // If ProfileScalingFactor == 1, BitrateProfileFactor = 2.0
  // If ProfileScalingFactor == 2, BitrateProfileFactor = 3.0
  if (profile_scaling_factor == 0) return 1;
  if (profile_scaling_factor == 1) return 2;
  if (profile_scaling_factor == 2) return 3;
  return 1;  // Default
}

//================================================
// Set Profile for (LCR, OPS)
//================================================
int av2_set_profile(int bit_depth, int chroma_format_idc, int num_mlayers,
                    int lcr_present, int ops_present) {
  // Validate bit depth
  if (!(bit_depth == AVM_BITS_8 || bit_depth == AVM_BITS_10)) {
    return -1;
  }

  // Validate chroma format
  if (!(chroma_format_idc == CHROMA_FORMAT_400 ||
        chroma_format_idc == CHROMA_FORMAT_420 ||
        chroma_format_idc == CHROMA_FORMAT_422 ||
        chroma_format_idc == CHROMA_FORMAT_444)) {
    return -1;
  }

  // Profile selection based on chroma format and mlayer count
  // Per Table A.1 Profile Definition and Table A.2 Allowed varies
  if (chroma_format_idc == CHROMA_FORMAT_400 ||
      chroma_format_idc == CHROMA_FORMAT_420) {
    if (lcr_present || ops_present) {
      if (num_mlayers == 1) {
        return MAIN_420_10_IP0;  // Profile 0: Main_420_10_IPO (max_mlayer_cnt =
                                 // 1)
      } else if (num_mlayers == 2) {
        return MAIN_420_10_IP1;  // Profile 1: Main_420_10_IP1 (max_mlayer_cnt =
                                 // 2)
      } else if (num_mlayers == 3) {
        return MAIN_420_10_IP2;  // Profile 2: Main_420_10_IP2 (max_mlayer_cnt =
                                 // 3)
      } else {
        return MAIN_420_10;  // Profile 3: Main_420_10 (unconstraint profile)
      }
    } else {
      // No LCR/OPS: infer profile from mlayer count
      if (num_mlayers == 1) {
        return MAIN_420_10_IP0;
      } else if (num_mlayers == 2) {
        return MAIN_420_10_IP1;
      } else if (num_mlayers == 3) {
        return MAIN_420_10_IP2;
      } else {
        return MAIN_420_10;
      }
    }
  } else if (chroma_format_idc == CHROMA_FORMAT_422) {
    // 4:2:2 chroma format
    // Maps to C_Main_422_10. Per Table A.2" seq_profile_idc can be 0..3 for
    // C_Main_422_10
    return MAIN_422_10;
  } else if (chroma_format_idc == CHROMA_FORMAT_444) {
    // 4:4:4 chroma format
    // Maps to C_Main_444_10 configuration
    return MAIN_444_10;
  }
  return -1;
}
#endif  // CONFIG_CWG_F429_INTEROP
