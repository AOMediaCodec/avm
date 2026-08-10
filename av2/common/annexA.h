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

#ifndef AVM_AV2_COMMON_ANNEXA_H_
#define AVM_AV2_COMMON_ANNEXA_H_

#include <stdbool.h>
#include <stdint.h>

/*!\file
 * \brief Provides the profile related functions
 * These include:
 * - Profile conformance checking
 * - Chroma format conversions
 * - Profile scaling factor calculations
 * - Interoperability point validation
 * - Profile selection for LCR, OPS, and MSDO
 * Note: For detailed Annex A tables, see av2/common/AnnexA.c
 */

#include "avm/avm_codec.h"
#include "av2/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif

struct avm_internal_error_info;
struct SequenceHeader;

//==========================================
// Profile Conformance Function
//===========================================
// Validates the bitstream parameters conform to the specified profile
// Returns 1 on success and 0 on failure
int av2_check_profile_interop_conformance(
    struct SequenceHeader *seq_params,
    struct avm_internal_error_info *error_info, int is_decoder);

typedef struct AV2ProfileLevelFactors {
  uint32_t picture_size_profile_factor;
  uint32_t bitrate_factor_numerator;
  uint32_t bitrate_factor_denominator;
} AV2ProfileLevelFactors;

// Returns the exact PicSizeProfileFactor and BitrateProfileFactor values for
// profiles 0 through 5, independent of CONFIG_12BIT_PROFILE. Callers that
// select a bitstream profile enforce its build-time support separately.
// Returns false for values without a table row, including Configurable.
bool av2_get_profile_level_factors(int seq_profile_idc,
                                   AV2ProfileLevelFactors *factors);

// Returns whether an OBU contributes to CompressedSize in Annex A.
bool av2_obu_counts_toward_compressed_size(OBU_TYPE obu_type);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_COMMON_ANNEXA_H_
