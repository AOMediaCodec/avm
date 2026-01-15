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

/*!\file
 * \brief Provides the profile related functions
 * These include:
 * - Profile conformance checking
 * - Chroma format conversions
 * - Profile scaling factor calculations
 * - Interoperability point validation
 * - Profile selection for LCR, OPS, and MSDO
 * Note: For detailed Annex A tabled (Table A.1 and A.2) see av2/common/AnnexA.c
 */

#include "avm/avm_codec.h"
#include "av2/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif

struct avm_internal_error_info;
struct SequenceHeader;

#define NUM_INTEROP_POINTS 16

//==========================================
// Profile Conformance Function
//===========================================
#if CONFIG_CWG_F429_INTEROP
// Validates the bitstream parameters conform to the specified profile
// Returns 1 on success and 0 on failure
int av2_check_profile_interop_conformance(
                                          int profile, int bit_depth, int subsampling_x, int subsampling_y,
                                          int monochrome, int seq_max_mcount,struct avm_internal_error_info *error_info, int is_decoder);

//==========================================
// Profile Scaling and Bitrate Functions
//===========================================
// Gets profile scaling factor CWG-G004
int get_profile_scaling_factor(int seq_profile_idc, int chroma_format_idc);

int get_bitrate_profile_factor(int profile_scaling_factor);

//==========================================
// Validate function
//===========================================
int av2_validate_layer_capacity(int seq_profile_idc,
                                int num_extended_layers,
                                int num_embedded_layers);
// SEQ_PROFILE
/*typedef enum {
 MAIN_420_10_IP0,
 MAIN_420_10_IP1,
 MAIN_420_10_IP2,
 MAIN_420_10,
 MAIN_422_10,
 MAIN_444_10,
 RESERVED,
 } BITSTREAM_PROFILE;*/
#endif  // CONFIG_CWG_F429_INTEROP

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_COMMON_TIMING_H_
