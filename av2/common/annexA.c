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
//===================================================================================
// Table A.1: AV2 Multi-Sequence Configurations
//===================================================================================
 * ConfigurationID | Configuration Label | Toolset |  BitDepth   | Chroma Format
 * ----------------|---------------------|---------|-------------|-------------------
 *       0         | C_Main_420_10       | Main    | 8, 10       |  4:0:0, 4:2:0
 *       1         | C_Main_422_10       | Main    | 8, 10       |  4:0:0, 4:2:0, 4:2:2
 *       2         | C_Main_444_10       | Main    | 8, 10       |  4:0:0, 4:2:0, 4:4:4
 *       3-63      | Reserved            | -       | -           |

 *
 * Notes:
 * - ConfigurationID: Identifies the multi-sequence configuration (6-bit value)
 * - Chroma Format: Supplrted bit depths for this configuration
 * - Chroma Format: Support chroma subsampling formats
 * - Resered: ConfigurationID valies 3-64 are reserved for future use
 */
/* clang-format on */

#if CONFIG_AV2_PROFILES
typedef enum {
  C_MAIN_420_10 = 0,  // Main toolset, 8/10-bit, 4:0:0/4:2:0
  C_MAIN_422_10 = 1,  // Main toolset, 8/10-bit, 4:0:0/4:2:0/4:2:2
  C_MAIN_444_10 = 2,  // Main toolset, 8/10-bit, 4:0:0/4:2:0/4:4:4
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
 *   C_Main_420_10         | 0..5                 | 0 or 1                  | 0 or 1
 *   C_Main_422_10         | 0..5                 | 0, 1, 3                 | 0 or 1
 *   C_Main_444_10         | 0..5                 | 0, 1, 2                 | 0 or 1
 *
 * Notes:
 * - seq_profile_idc: Allowed profile values (0=MAIN_420_10_IP0, 1=MAIN_420_10_IP1, 2=MAIN_420_10_IP2, 3=Main_420_10,
 *                                            4=MAIN_422_10, 5=MAIN_444_10)
 * - bit_depth_idc: 0=8-bit, 1=10-bit
 * - C_Main_420_10: Supports profiles 0-5, chroma 4:0:0 and 4:2:0, bit depths 8 and 10
 * - C_Main_422_10: Supports profiles 0-5, chroma 4:0:0, 4:2:0 and 4:2:2, bit depth 8 and 10
 * - C_Main_444_10: Supports profiles 0-5, chroma 4:0:0, 4:2:0, and 4:4:4, bit depth 8 and 10
 */

// Interoperability Point Table
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
  NUM_INTEROP_POINTS = 16,
} INTEROP_POINTS;

static const int seq_profile_max_mlayer_cnt[MAX_PROFILES] = {
  1, 2, 3, MAX_NUM_MLAYERS, MAX_NUM_MLAYERS, MAX_NUM_MLAYERS,
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

static INLINE int av2_get_max_mlayer_cnt_from_profile(int seq_profile_idc) {
  if (seq_profile_idc < 0 || seq_profile_idc >= MAX_PROFILES) return -1;
  return seq_profile_max_mlayer_cnt[seq_profile_idc];
}

static int check_bit_depth_8_10(int bit_depth) {
  if (bit_depth != AVM_BITS_8 && bit_depth != AVM_BITS_10) {
    return 0;
  }
  return 1;
}

static avm_codec_err_t check_chroma_format(int monochrome, int is_420,
                                           int is_422, int is_444,
                                           int allow_420, int allow_422,
                                           int allow_444) {
  // Monochrome (4:0:0) is always allowed
  if (monochrome) {
    return AVM_CODEC_OK;
  }

  // Check if the current chroma format is allowed
  if ((is_420 && allow_420) || (is_422 && allow_422) || (is_444 && allow_444)) {
    return AVM_CODEC_OK;
  }
  return AVM_CODEC_UNSUP_BITSTREAM;
}

static avm_codec_err_t check_mlayer_count(int profile_idc, int seq_max_mcount) {
  const int max_allowed_mcount =
      av2_get_max_mlayer_cnt_from_profile(profile_idc);
  if (max_allowed_mcount < 0 && seq_max_mcount > max_allowed_mcount) {
    return AVM_CODEC_UNSUP_BITSTREAM;
  }
  return AVM_CODEC_OK;
}

// Checks the profile conformance -- Top-level function
int av2_check_profile_interop_conformance(
    struct SequenceHeader *seq_params,
    struct avm_internal_error_info *error_info, int is_decoder) {
  uint32_t chroma_format_idc = CHROMA_FORMAT_420;
  avm_codec_err_t err = av2_get_chroma_format_idc(
      seq_params->subsampling_x, seq_params->subsampling_y,
      seq_params->monochrome, &chroma_format_idc);
  (void)err;

  const int is_420 = (chroma_format_idc == CHROMA_FORMAT_420);
  const int is_422 = (chroma_format_idc == CHROMA_FORMAT_422);
  const int is_444 = (chroma_format_idc == CHROMA_FORMAT_444);

  const int profile = seq_params->seq_profile_idc;
  const int bit_depth = seq_params->bit_depth;
  const int monochrome = seq_params->monochrome;
  const int seq_max_mcount = seq_params->seq_max_mlayer_cnt;

  // All profiles support 8-bit and 10-bit only
  int is_valid_bit_depth = check_bit_depth_8_10(bit_depth);
  if (!is_valid_bit_depth) {
    return 0;
  }

  switch (profile) {
    case MAIN_420_10_IP0:
    case MAIN_420_10_IP1:
    case MAIN_420_10_IP2:
    case MAIN_420_10:
      // All 420 profiles: allow only 4:2:0 and monochrome
      err = check_chroma_format(monochrome, is_420, is_422, is_444,
                                1 /* allow_420 */, 0 /* allow_422 */,
                                0 /* allow_444 */);
      if (err != AVM_CODEC_OK) {
        avm_internal_error(
            error_info,
            is_decoder ? AVM_CODEC_UNSUP_BITSTREAM : AVM_CODEC_INVALID_PARAM,
            "Profile %d only supports 4:0:0%s%s%s chroma.",
            (BITSTREAM_PROFILE)profile, is_420, is_422, is_444);
      }
      break;
    case MAIN_422_10:
      // 422 profile: allow 4:2:0 and 4:2:2 and monochrome
      err = check_chroma_format(monochrome, is_420, is_422, is_444,
                                1 /* allow_420 */, 1 /* allow_422 */,
                                0 /* allow_444 */);
      if (err != AVM_CODEC_OK) {
        avm_internal_error(
            error_info,
            is_decoder ? AVM_CODEC_UNSUP_BITSTREAM : AVM_CODEC_INVALID_PARAM,
            "Profile %d only supports 4:0:0%s%s%s chroma.",
            (BITSTREAM_PROFILE)profile, is_420, is_422, is_444);
      }
      break;
    case MAIN_444_10:
      // 444 profile: allow 4:2:0, 4:2:2, 4:4:4 and monochrome
      err = check_chroma_format(monochrome, is_420, is_422, is_444,
                                1 /* allow_420 */, 1 /* allow_422 */,
                                1 /* allow_444 */);
      if (err != AVM_CODEC_OK) {
        avm_internal_error(
            error_info,
            is_decoder ? AVM_CODEC_UNSUP_BITSTREAM : AVM_CODEC_INVALID_PARAM,
            "Profile %d only supports 4:0:0%s%s%s chroma.",
            (BITSTREAM_PROFILE)profile, is_420, is_422, is_444);
      }
      break;
    default:
      // Profile 6+ - reserved/unsupported
      return 0;
  }
  // Check if Max mlayer count is valid for IP profiles (seq_profile_idc <=2)
  err = check_mlayer_count(profile, seq_max_mcount);
  if (err != AVM_CODEC_OK) {
    avm_internal_error(
        error_info,
        is_decoder ? AVM_CODEC_UNSUP_BITSTREAM : AVM_CODEC_INVALID_PARAM,
        "Unsupported mlayer count present in the bitstream");
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
 *      5                        (0, 1)              CHROMA_FORMAT_444                       2
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
  if (seq_profile_idc == MAIN_422_10 &&
      chroma_format_idc == CHROMA_FORMAT_422) {
    return 1;
  }

  // Profile 5 with 444 chroma format
  if (seq_profile_idc == MAIN_444_10 &&
      chroma_format_idc == CHROMA_FORMAT_444) {
    return 2;
  }

  // Default for invalid combinations
  return 0;
}
#endif  // CONFIG_AV2_PROFILES
