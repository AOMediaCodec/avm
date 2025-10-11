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

#ifndef AOM_AV1_COMMON_AV1_LOOPFILTER_H_
#define AOM_AV1_COMMON_AV1_LOOPFILTER_H_

#include "config/aom_config.h"

#include "aom_ports/mem.h"
#include "av1/common/blockd.h"
#include "av1/common/seg_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DF_OFFSETS 64
#define ZERO_DF_OFFSET 32

#define DF_TWO_PARAM 0
#define DF_DUAL 1

#define DF_PAR_BITS 3
#define DF_DELTA_SCALE 8
#define DF_SEARCH_STEP_SIZE 2

#define DF_FILT26 1
#define DF_CHROMA_WIDE 1

#define DF_REDUCED_SB_EDGE 1

#define MAX_LOOP_FILTER 63
#define MAX_SHARPNESS 7

#define SIMD_WIDTH 16

#define SUB_PU_THR_SHIFT 3
#define SUB_PU_QTHR 150
#define SUB_PU_BD_FACTOR 24

enum lf_path {
  LF_PATH_420,
  LF_PATH_444,
  LF_PATH_SLOW,
};

/*!\cond */
enum { VERT_EDGE = 0, HORZ_EDGE = 1, NUM_EDGE_DIRS } UENUM1BYTE(EDGE_DIR);
typedef struct {
  uint64_t bits[4];
} FilterMask;

struct loopfilter {
  int filter_level[2];
  int filter_level_u;
  int filter_level_v;

#if DF_DUAL
  int delta_q_luma[2];
  int delta_side_luma[2];
#else
  int delta_q_luma;
  int delta_side_luma;
#endif  // DF_DUAL
  int delta_q_u;
  int delta_side_u;
  int delta_q_v;
  int delta_side_v;

  uint8_t mode_ref_delta_enabled;
  uint8_t mode_ref_delta_update;

  // The order is based on the ranked references.
  int8_t ref_deltas[SINGLE_REF_FRAMES];

  // 0 = ZERO_MV, MV
  int8_t mode_deltas[MAX_MODE_LF_DELTAS];

  int combine_vert_horz_lf;

  int tip_filter_level;
  int tip_delta;
};

// Need to align this structure so when it is declared and
// passed it can be loaded into vector registers.
typedef struct {
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, mblim[SIMD_WIDTH]);
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, lim[SIMD_WIDTH]);
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, hev_thr[SIMD_WIDTH]);
} loop_filter_thresh;

typedef struct {
#if CONFIG_DF_DQP
  int q_thr_q_offset[MAX_MB_PLANE][2][SINGLE_REF_FRAMES][MAX_MODE_LF_DELTAS];
  int side_thr_q_offset[MAX_MB_PLANE][2][SINGLE_REF_FRAMES][MAX_MODE_LF_DELTAS];
#else
  uint16_t q_thr[MAX_MB_PLANE][MAX_SEGMENTS][2][SINGLE_REF_FRAMES]
                [MAX_MODE_LF_DELTAS];
  uint16_t side_thr[MAX_MB_PLANE][MAX_SEGMENTS][2][SINGLE_REF_FRAMES]
                   [MAX_MODE_LF_DELTAS];
#endif  // CONFIG_DF_DQP

  uint16_t tip_q_thr[MAX_MB_PLANE][2];
  uint16_t tip_side_thr[MAX_MB_PLANE][2];
} loop_filter_info_n;

typedef struct LoopFilterWorkerData {
  YV12_BUFFER_CONFIG *frame_buffer;
  struct AV1Common *cm;
  struct macroblockd_plane planes[MAX_MB_PLANE];
  // TODO(Ranjit): When the filter functions are modified to use xd->lossless
  // add lossless as a member here.
  MACROBLOCKD *xd;
} LFWorkerData;
/*!\endcond */

/* assorted loopfilter functions which get used elsewhere */
struct AV1Common;
struct macroblockd;
struct AV1LfSyncData;

void av1_loop_filter_init(struct AV1Common *cm);

void av1_loop_filter_frame_init(struct AV1Common *cm, int plane_start,
                                int plane_end);

/*!\brief Apply AV1 loop filter
 *
 * \ingroup in_loop_filter
 * \callgraph
 */
void av1_loop_filter_frame(YV12_BUFFER_CONFIG *frame, struct AV1Common *cm,
                           struct macroblockd *xd, int plane_start,
                           int plane_end, int partial_frame);
void loop_filter_tip_plane(struct AV1Common *cm, const int plane, uint16_t *dst,
                           const int dst_stride, const int plane_w,
                           const int plane_h);
void setup_tip_dst_planes(struct AV1Common *const cm, const int plane,
                          const int tpl_row, const int tpl_col);
void loop_filter_tip_frame(struct AV1Common *cm, int plane_start,
                           int plane_end);
void init_tip_lf_parameter(struct AV1Common *cm, int plane_start,
                           int plane_end);
void av1_filter_block_plane_vert(struct AV1Common *const cm,
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col);

void av1_filter_block_plane_horz(struct AV1Common *const cm,
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col);
int df_quant_from_qindex(int q_index, int bit_depth);

int df_side_from_qindex(int q_index, int bit_depth);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_AV1_LOOPFILTER_H_
