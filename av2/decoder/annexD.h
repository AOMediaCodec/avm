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

#ifndef AVM_AV2_DECODER_ANNEXD_H_
#define AVM_AV2_DECODER_ANNEXD_H_

#include "av2/common/av2_common_int.h"
#include "avm_scale/yv12config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Annex D MULTISTREAM_ATLAS composition process (informative).
//
// Composites the decoded extended-layer frames of one temporal unit into a
// single canvas frame, using the geometry in `atlas` (an active
// MULTISTREAM_ATLAS or MULTISTREAM_ALPHA_ATLAS record).
//
// Inputs:
//   atlas        - active atlas record; atlas->ats_basic_info provides the
//                  canvas size, per-segment position/size, and background.
//   seg_src      - array of length `num_segments`; seg_src[i] is the resolved
//                  decoded source frame for segment i (chosen by the caller:
//                  the current-TU frame for that stream, else its previous
//                  frame). A NULL entry leaves that segment as the background.
//   num_segments - number of segments (ats_num_atlas_segments_minus_1 + 1).
//   canvas       - pre-allocated output buffer sized to the atlas canvas; its
//                  bit depth is the composite depth (typically the max over the
//                  texture sources).
//
// All non-NULL sources must share `canvas`'s subsampling and monochrome/color
// layout (chroma-format conversion is not implemented). Source bit depths may
// differ from the canvas and are scaled per sample to the canvas depth.
// Resampling (when a source's size differs from its segment) uses the
// non-normative resizer. Background color uses BT.709 limited-range conversion.
//
// Returns 0 on success, -1 on invalid arguments or a format mismatch.
int av2_annex_d_compose_tu(const AtlasSegmentInfo *atlas,
                           YV12_BUFFER_CONFIG *const *seg_src, int num_segments,
                           YV12_BUFFER_CONFIG *canvas);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_DECODER_ANNEXD_H_
