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

#ifndef AVM_AV2_COMMON_CCSO_H_
#define AVM_AV2_COMMON_CCSO_H_

#define CCSO_INPUT_INTERVAL 3
#define CCSO_PROC_BLK_LOG2 5

#include <float.h>
#include "config/avm_config.h"
#include "avm/avm_integer.h"
#include "avm_ports/mem.h"
#include "av2/common/av2_common_int.h"

static const uint16_t quant_sz[4][4] = { { 16, 8, 32, 0 },
                                         { 56, 40, 64, 128 },
                                         { 48, 24, 96, 192 },
                                         { 80, 112, 160, 256 } };

static const int edge_clf_to_edge_interval[2] = { 3, 2 };

#ifdef __cplusplus
extern "C" {
#endif

void av2_copy_ccso_filters(CcsoInfo *to, CcsoInfo *from, int plane,
                           bool frame_level, bool block_level, int sb_count);

void extend_ccso_border(const YV12_BUFFER_CONFIG *frame, uint16_t *buf,
                        const int d);

void cal_filter_support(int *rec_luma_idx, const uint16_t *rec_y,
                        const int quant_step_size, const int inv_quant_step,
                        const int *rec_idx, const int edge_clf);

// ext_filter_support must be less than 7.
void derive_ccso_sample_pos(int *rec_idx, const int ccso_stride,
                            const uint8_t ext_filter_support);

void ccso_frame(YV12_BUFFER_CONFIG *frame, AV2_COMMON *cm, MACROBLOCKD *xd,
                uint16_t *ext_rec_y);

// Apply CCSO for each process block row
void av2_apply_ccso_filter_for_row(AV2_COMMON *cm, MACROBLOCKD *xd,
                                   const uint16_t *src_y, uint16_t *dst_yuv,
                                   int *src_loc, int *src_cls, int blk_row,
                                   int thr, int blk_size, int blk_size_proc,
                                   int blk_log2_x, int blk_log2_y,
                                   int unit_log2_x, int unit_log2_y, int plane);

void ccso_filter_block_hbd_wo_buf_4x4_c(
    AV2_COMMON *cm, const uint16_t *src_y, uint16_t *dst_yuv,
    int tile_col_start, int tile_row_start, const int x, const int y,
    const int pic_width, const int pic_height, int *src_cls,
    const int8_t *offset_buf, const int src_y_stride, const int dst_stride,
    const int y_uv_hscale, const int y_uv_vscale, const int thr,
    const int neg_thr, const int *src_loc, const int max_val,
    const int blk_size_x, const int blk_size_y, const bool isSingleBand,
    const uint8_t shift_bits, const int edge_clf, const uint8_t ccso_bo_only,
    int plane);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_COMMON_CCSO_H_
