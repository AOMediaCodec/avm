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

#ifndef AVM_AV2_COMMON_SIDEINFO_H_
#define AVM_AV2_COMMON_SIDEINFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NUMBER_CONTEXTS 160  // relaxed upper bound
#define MAX_DIMS_CONTEXT0 100    // relaxed upper bound
#define MAX_DIMS_CONTEXT1 10     // relaxed upper bound
#define MAX_DIMS_CONTEXT2 10     // relaxed upper bound
#define MAX_DIMS_CONTEXT3 5      // relaxed upper bound

extern int beginningFrameFlag[MAX_NUMBER_CONTEXTS][MAX_DIMS_CONTEXT3]
                             [MAX_DIMS_CONTEXT2][MAX_DIMS_CONTEXT1]
                             [MAX_DIMS_CONTEXT0];
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_SIDEINFO_H_
