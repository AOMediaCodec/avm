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

#include <emmintrin.h>  // SSE2

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/x86/fwd_txfm_sse2.h"

#define FDCT4x4_2D_HELPER fdct4x4_helper
#define FDCT4x4_2D avm_fdct4x4_sse2
#define FDCT4x4_2D_LP avm_fdct4x4_lp_sse2
#define FDCT8x8_2D avm_highbd_fdct8x8_sse2
#include "avm_dsp/x86/fwd_txfm_impl_sse2.h"
#undef FDCT4x4_2D_HELPER
#undef FDCT4x4_2D
#undef FDCT4x4_2D_LP
#undef FDCT8x8_2D
