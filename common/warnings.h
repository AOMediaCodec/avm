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
#ifndef AVM_COMMON_WARNINGS_H_
#define AVM_COMMON_WARNINGS_H_

#ifdef __cplusplus
extern "C" {
#endif

struct avm_codec_enc_cfg;
struct AvxEncoderConfig;

/*
 * Checks config for improperly used settings. Warns user upon encountering
 * settings that will lead to poor output quality. Prompts user to continue
 * when warnings are issued.
 */
void check_encoder_config(int disable_prompt,
                          const struct AvxEncoderConfig *global_config,
                          const struct avm_codec_enc_cfg *stream_config);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_COMMON_WARNINGS_H_
