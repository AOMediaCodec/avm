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

#ifndef AVM_AV2_ENCODER_SUBGOP_H_

#include "av2/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void av2_init_subgop_config_set(SubGOPSetCfg *config_set);
int av2_process_subgop_config_set(const char *param, SubGOPSetCfg *config_set);
void av2_print_subgop_config_set(SubGOPSetCfg *config_set);
int av2_process_subgop_config_set_fromfile(const char *paramfile,
                                           SubGOPSetCfg *config_set);

// Finds the ptr to the subgop config with the queried number of
// frames and whether it is the last or first subgop in a gop.
// If the right number of frames is found but the right
// subgop_in_gop_code is not found then the generic config with the
// matching length is returned. If the right number of frames
// is not found, Null is returned.
const SubGOPCfg *av2_find_subgop_config(SubGOPSetCfg *config_set,
                                        int num_frames, int is_last_gop,
                                        int is_first_subgop);

// Finds the ptr to the subgop config with the queried number of
// frames and subgop_in_gop_code. Only if th exact subgop_in_gop_code
// is found for the given length, the pointer to that config is returned.
// Else Null is returned.
const SubGOPCfg *av2_find_subgop_config_exact(
    SubGOPSetCfg *config_set, int num_frames,
    SUBGOP_IN_GOP_CODE subgop_in_gop_code);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_SUBGOP_H_
