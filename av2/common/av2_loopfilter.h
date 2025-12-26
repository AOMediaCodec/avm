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

#ifndef AVM_AV2_COMMON_AV2_LOOPFILTER_H_
#define AVM_AV2_COMMON_AV2_LOOPFILTER_H_

#include "config/avm_config.h"

#include "avm_ports/mem.h"
#include "av2/common/blockd.h"
#include "av2/common/seg_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DF_OFFSETS 64
#define ZERO_DF_OFFSET 32

#define DF_PAR_BITS 3
#define DF_DELTA_SCALE 8
#define DF_SEARCH_STEP_SIZE 2

#define SUB_PU_THR_SHIFT 3
#define SUB_PU_QTHR 150
#define SUB_PU_BD_FACTOR 24

/*!\cond */
enum { VERT_EDGE = 0, HORZ_EDGE = 1, NUM_EDGE_DIRS } UENUM1BYTE(EDGE_DIR);

struct loopfilter {
  int apply_deblocking_filter[2];
  int apply_deblocking_filter_u;
  int apply_deblocking_filter_v;

  int delta_q_luma[2];
  int delta_side_luma[2];

  int delta_q_u;
  int delta_side_u;
  int delta_q_v;
  int delta_side_v;

  int combine_vert_horz_lf;

  int apply_deblocking_filter_tip;
  int tip_delta;
};

typedef struct {
  int q_thr_q_offset[MAX_MB_PLANE][2];
  int side_thr_q_offset[MAX_MB_PLANE][2];
  uint16_t tip_q_thr[MAX_MB_PLANE][2];
  uint16_t tip_side_thr[MAX_MB_PLANE][2];
} loop_filter_info_n;

typedef struct LoopFilterWorkerData {
  YV12_BUFFER_CONFIG *frame_buffer;
  struct AV2Common *cm;
  struct macroblockd_plane planes[MAX_MB_PLANE];
  // TODO(Ranjit): When the filter functions are modified to use xd->lossless
  // add lossless as a member here.
  MACROBLOCKD *xd;
} LFWorkerData;
/*!\endcond */

/* assorted loopfilter functions which get used elsewhere */
struct AV2Common;
struct macroblockd;

void av2_loop_filter_init(struct AV2Common *cm);

void av2_loop_filter_frame_init(struct AV2Common *cm, int plane_start,
                                int plane_end);

/*!\brief Apply AV2 loop filter
 *
 * \ingroup in_loop_filter
 * \callgraph
 */
void av2_loop_filter_frame(YV12_BUFFER_CONFIG *frame, struct AV2Common *cm,
                           struct macroblockd *xd, int plane_start,
                           int plane_end, int partial_frame);
void loop_filter_tip_plane(struct AV2Common *cm, const int plane, uint16_t *dst,
                           const int dst_stride, const int plane_w,
                           const int plane_h);
void setup_tip_dst_planes(struct AV2Common *const cm, MACROBLOCKD *xd,
                          const int plane, const int tpl_row,
                          const int tpl_col);
void loop_filter_tip_frame(struct AV2Common *cm, MACROBLOCKD *xd,
                           int plane_start, int plane_end);
void init_tip_lf_parameter(struct AV2Common *cm, int plane_start,
                           int plane_end);
void av2_filter_block_plane_vert(struct AV2Common *const cm,
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col);

void av2_filter_block_plane_horz(struct AV2Common *const cm,
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col);
int df_quant_from_qindex(int q_index, int bit_depth);

int df_side_from_qindex(int q_index, int bit_depth);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_AV2_LOOPFILTER_H_
