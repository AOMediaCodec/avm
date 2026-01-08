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
#ifndef AVM_AV2_DECODER_INSPECTION_H_
#define AVM_AV2_DECODER_INSPECTION_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#include "av2/common/blockd.h"
#include "av2/common/seg_common.h"
#if CONFIG_ACCOUNTING
#include "av2/decoder/accounting.h"
#endif

#ifndef AVM_AVM_AVMDX_H_
typedef void (*avm_inspect_cb)(void *decoder, void *data);
#endif

typedef struct insp_mv insp_mv;

struct insp_mv {
  int16_t row;
  int16_t col;
};

typedef struct insp_pixel_data insp_pixel_data;

struct insp_pixel_data {
  int16_t samples[MAX_SB_SIZE][MAX_SB_SIZE];
};

typedef struct insp_mi_data insp_mi_data;

struct insp_mi_data {
  insp_mv mv[2];
  int16_t ref_frame[2];
  int16_t ref_frame_order_hint[2];
  int16_t ref_frame_is_inter[2];
  int16_t ref_frame_is_tip[2];
  int16_t mv_precision;
  int16_t mode;
  int16_t uv_mode;
  int16_t sb_type;
  int16_t sb_type_chroma;
  int16_t skip;
  int16_t segment_id;
  int16_t dual_filter_type;
  int16_t filter[2];
  int16_t tx_type;
  int16_t tx_size;
  int16_t cdef_level;
  int16_t cdef_strength;
  int16_t cfl_alpha_idx;
  int16_t cfl_alpha_sign;
  int16_t current_qindex;
  int16_t compound_type;
  int16_t motion_mode;
  int16_t intrabc;
  int16_t palette;
  int16_t uv_palette;
  int16_t angle_delta;
  int16_t uv_angle_delta;
};

typedef struct insp_sb_data insp_sb_data;

struct insp_sb_data {
  PARTITION_TREE *partition_tree_luma;
  PARTITION_TREE *partition_tree_chroma;
  bool has_separate_chroma_partition_tree;
  int16_t prediction_samples[MAX_SB_SIZE][MAX_SB_SIZE];
  int16_t recon_samples[MAX_SB_SIZE][MAX_SB_SIZE];
  tran_low_t dqcoeff[MAX_MB_PLANE][MAX_SB_SQUARE];
  tran_low_t qcoeff[MAX_MB_PLANE][MAX_SB_SQUARE];
  tran_low_t dequant_values[MAX_MB_PLANE][MAX_SB_SQUARE];
};

typedef struct insp_frame_data insp_frame_data;

struct insp_frame_data {
#if CONFIG_ACCOUNTING
  Accounting *accounting;
#endif
  insp_mi_data *mi_grid;
  insp_sb_data *sb_grid;
  int max_sb_rows;
  int max_sb_cols;
  int16_t frame_number;
  int immediate_output_picture;
  int frame_type;
  int base_qindex;
  int tip_frame_mode;
  int mi_rows;
  int mi_cols;
  int tile_mi_rows;
  int tile_mi_cols;

  int32_t y_dequant[MAX_SEGMENTS][2];
  int32_t u_dequant[MAX_SEGMENTS][2];
  int32_t v_dequant[MAX_SEGMENTS][2];

  // TODO(negge): add per frame CDEF data
  int delta_q_present_flag;
  int delta_q_res;
#if !CONFIG_F024_KEYOBU
  int show_existing_frame;
#endif
  int superblock_size;
  // Points to the same underlying allocations as the decoder
  YV12_BUFFER_CONFIG recon_frame_buffer;
  YV12_BUFFER_CONFIG predicted_frame_buffer;
  YV12_BUFFER_CONFIG prefiltered_frame_buffer;
  int bit_depth;
  int render_width;
  int render_height;
  int width;
  int height;
};

void ifd_init(insp_frame_data *fd, int frame_width, int frame_height);
void ifd_clear(insp_frame_data *fd);
int ifd_inspect_superblock(insp_frame_data *fd, void *decoder);
int ifd_inspect(insp_frame_data *fd, void *decoder, int skip_not_transform);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus
#endif  // AVM_AV2_DECODER_INSPECTION_H_
