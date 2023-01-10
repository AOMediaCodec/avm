/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AOM_AV1_COMMON_PEF_H_
#define AOM_AV1_COMMON_PEF_H_

#include "config/aom_config.h"

#include "aom_ports/mem.h"
#include "av1/common/blockd.h"
#include "av1/common/seg_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PEF_SHIFT 11
#define PEF_QTHR 150
#define PEF_DELTA 2
#define PEF_THR_SHIFT 3
#define PEF_DELTA_SCALE 8
#define PEF_BD_FACTOR 24
#define PEF_MCU_SZ 8

// Structure for PEF parameters
typedef struct {
  int filter_level[2];
  int filter_level_u;
  int filter_level_v;
  int delta_q_luma[2];
  int delta_side_luma[2];
  int delta_q_u;
  int delta_side_u;
  int delta_q_v;
  int delta_side_v;
  int pef_delta;
} PefParams;

// Structure for intermediate thresholds
typedef struct {
  uint16_t q_thr[MAX_MB_PLANE][2];
  uint16_t side_thr[MAX_MB_PLANE][2];
} PefInfo;

// Structure for PEF function input
typedef struct {
  // 0 for OPFL prediciton, 1 for TIP prediciton, 2 for TIP frame
  int pef_mode;
  int plane;
  int bw;
  int bh;
  int ss_x;
  int ss_y;
  int bit_depth;
  uint16_t *dst;
  int dst_stride;
  int_mv *mv_refined;
} PefFuncInput;

typedef void (*filt_func)(uint16_t *s, int stride, int bd, uint16_t q_thresh,
                          uint16_t side_thresh, int q_mult, int w_mult, int n,
                          int filt_len);

// Initialize PEF parameters
void init_pef_parameter(struct AV1Common *const cm, int plane_start,
                        int plane_end);

// enhance TIP frame
void enhance_tip_frame(struct AV1Common *const cm, MACROBLOCKD *xd);

// enhance prediction blocks
void enhance_prediction(const struct AV1Common *cm, MACROBLOCKD *xd, int plane,
                        uint16_t *dst, int dst_stride, int bw, int bh
#if CONFIG_OPTFLOW_REFINEMENT
                        ,
                        int_mv *const mv_refined, int use_opfl
#endif  // CONFIG_OPTFLOW_REFINEMENT
);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_PEF_H_
