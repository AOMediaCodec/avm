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

#if CONFIG_DF_PAR_BITS
#define DF_PAR_BITS 3
#else
#define DF_PAR_BITS 5
#endif  // CONFIG_DF_PAR_BITS
#define DF_DELTA_SCALE 8
#define DF_SEARCH_STEP_SIZE 2
#if !CONFIG_DF_PAR_BITS
#define DF_PAR_OFFSET (1 << (DF_PAR_BITS - 1))
#define DF_PAR_MIN_VAL (-(1 << (DF_PAR_BITS - 1)))
#define DF_PAR_MAX_VAL ((1 << (DF_PAR_BITS - 1)) - 1)
#endif  // !CONFIG_DF_PAR_BITS

#define DF_FILT26 1
#define DF_CHROMA_WIDE 1

#define DF_REDUCED_SB_EDGE 1

#define MAX_LOOP_FILTER 63
#define MAX_SHARPNESS 7

#define SIMD_WIDTH 16

#if CONFIG_LF_SUB_PU
#define SUB_PU_THR_SHIFT 3
#define SUB_PU_QTHR 150
#define SUB_PU_BD_FACTOR 24
#endif  // CONFIG_LF_SUB_PU

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

#if CONFIG_LF_SUB_PU
  int tip_filter_level;
#if !CONFIG_IMPROVE_TIP_LF
  int tip_delta_idx;
#endif  // !CONFIG_IMPROVE_TIP_LF
  int tip_delta;
#endif  // CONFIG_LF_SUB_PU
};

// Need to align this structure so when it is declared and
// passed it can be loaded into vector registers.
typedef struct {
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, mblim[SIMD_WIDTH]);
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, lim[SIMD_WIDTH]);
  DECLARE_ALIGNED(SIMD_WIDTH, uint8_t, hev_thr[SIMD_WIDTH]);
} loop_filter_thresh;

typedef struct {
  uint16_t q_thr[MAX_MB_PLANE][MAX_SEGMENTS][2][SINGLE_REF_FRAMES]
                [MAX_MODE_LF_DELTAS];
  uint16_t side_thr[MAX_MB_PLANE][MAX_SEGMENTS][2][SINGLE_REF_FRAMES]
                   [MAX_MODE_LF_DELTAS];
#if CONFIG_LF_SUB_PU
  uint16_t tip_q_thr[MAX_MB_PLANE][2];
  uint16_t tip_side_thr[MAX_MB_PLANE][2];
#endif  // CONFIG_LF_SUB_PU
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

#if CONFIG_LF_SUB_PU
void loop_filter_tip_plane(struct AV1Common *cm, const int plane, uint16_t *dst,
                           const int dst_stride,
#if CONFIG_IMPROVE_TIP_LF
                           const int plane_w, const int plane_h
#else
                           const int bw, const int bh
#endif  // CONFIG_IMPROVE_TIP_LF
);
void setup_tip_dst_planes(struct AV1Common *const cm, const int plane,
                          const int tpl_row, const int tpl_col);
void loop_filter_tip_frame(struct AV1Common *cm, int plane_start,
                           int plane_end);
void init_tip_lf_parameter(struct AV1Common *cm, int plane_start,
                           int plane_end);
#endif  // CONFIG_LF_SUB_PU
#if CONFIG_BRU
void av1_filter_block_plane_vert(struct AV1Common *const cm,
#else
void av1_filter_block_plane_vert(const struct AV1Common *const cm,
#endif  // CONFIG_BRU
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col);

#if CONFIG_BRU
void av1_filter_block_plane_horz(struct AV1Common *const cm,
#else
void av1_filter_block_plane_horz(const struct AV1Common *const cm,
#endif  // CONFIG_BRU
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col);
int df_quant_from_qindex(int q_index, int bit_depth);

int df_side_from_qindex(int q_index, int bit_depth);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_AV1_LOOPFILTER_H_
