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

#include <assert.h>
#include <stddef.h>

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/enums.h"
#include "av1/common/filter.h"
#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/binary_codes_reader.h"
#include "aom_dsp/bitreader.h"
#include "aom_dsp/bitreader_buffer.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/aom_timer.h"
#include "aom_ports/mem.h"
#include "aom_ports/mem_ops.h"
#include "aom_scale/aom_scale.h"
#include "aom_util/aom_thread.h"

#if CONFIG_BITSTREAM_DEBUG || CONFIG_MISMATCH_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG || CONFIG_MISMATCH_DEBUG

#include "av1/common/alloccommon.h"
#include "av1/common/cdef.h"
#if CONFIG_CCSO
#include "av1/common/ccso.h"
#endif
#include "av1/common/cfl.h"
#if CONFIG_INSPECTION
#include "av1/decoder/inspection.h"
#endif
#include "av1/common/common.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/frame_buffers.h"
#include "av1/common/idct.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/resize.h"
#include "av1/common/seg_common.h"
#include "av1/common/thread_common.h"
#include "av1/common/tile_common.h"
#include "av1/common/tip.h"
#include "av1/common/warped_motion.h"
#include "av1/common/obmc.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/decodemv.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodetxb.h"
#include "av1/decoder/detokenize.h"

#define AOM_MIN_THREADS_PER_TILE 1
#define AOM_MAX_THREADS_PER_TILE 2

// This is needed by ext_tile related unit tests.
#define EXT_TILE_DEBUG 1
#define MC_TEMP_BUF_PELS                       \
  (((MAX_SB_SIZE)*2 + (AOM_INTERP_EXTEND)*2) * \
   ((MAX_SB_SIZE)*2 + (AOM_INTERP_EXTEND)*2))

#if CONFIG_THROUGHPUT_ANALYSIS
int64_t tot_ctx_syms = { 0 };
int64_t tot_bypass_syms = { 0 };
int64_t max_ctx_syms = { 0 };
int64_t max_bypass_syms = { 0 };
int64_t max_bits = { 0 };
int64_t tot_bits = { 0 };
int64_t tot_frames = { 0 };
#endif  // CONFIG_THROUGHPUT_ANALYSIS

// Checks that the remaining bits start with a 1 and ends with 0s.
// It consumes an additional byte, if already byte aligned before the check.
int av1_check_trailing_bits(AV1Decoder *pbi, struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  // bit_offset is set to 0 (mod 8) when the reader is already byte aligned
  int bits_before_alignment = 8 - rb->bit_offset % 8;
  int trailing = aom_rb_read_literal(rb, bits_before_alignment);
  if (trailing != (1 << (bits_before_alignment - 1))) {
    cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
    return -1;
  }
  return 0;
}

// Use only_chroma = 1 to only set the chroma planes
static AOM_INLINE void set_planes_to_neutral_grey(
    const SequenceHeader *const seq_params, const YV12_BUFFER_CONFIG *const buf,
    int only_chroma) {
  const int val = 1 << (seq_params->bit_depth - 1);
  for (int plane = only_chroma; plane < MAX_MB_PLANE; plane++) {
    const int is_uv = plane > 0;
    uint16_t *const base = buf->buffers[plane];
    // Set the first row to neutral grey. Then copy the first row to all
    // subsequent rows.
    if (buf->crop_heights[is_uv] > 0) {
      aom_memset16(base, val, buf->crop_widths[is_uv]);
      for (int row_idx = 1; row_idx < buf->crop_heights[is_uv]; row_idx++) {
        memcpy(&base[row_idx * buf->strides[is_uv]], base,
               sizeof(*base) * buf->crop_widths[is_uv]);
      }
    }
  }
}

static AOM_INLINE void loop_restoration_read_sb_coeffs(
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    AV1_COMMON *const cm, MACROBLOCKD *xd, aom_reader *const r, int plane,
#else
    const AV1_COMMON *const cm, MACROBLOCKD *xd, aom_reader *const r, int plane,
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    int runit_idx);

static int read_is_valid(const uint8_t *start, size_t len, const uint8_t *end) {
  return len != 0 && len <= (size_t)(end - start);
}

static TX_MODE read_tx_mode(struct aom_read_bit_buffer *rb,
                            int coded_lossless) {
  if (coded_lossless) return ONLY_4X4;
  return aom_rb_read_bit(rb) ? TX_MODE_SELECT : TX_MODE_LARGEST;
}

static REFERENCE_MODE read_frame_reference_mode(
    const AV1_COMMON *cm, struct aom_read_bit_buffer *rb) {
  if (frame_is_intra_only(cm)) {
    return SINGLE_REFERENCE;
  } else {
    return aom_rb_read_bit(rb) ? REFERENCE_MODE_SELECT : SINGLE_REFERENCE;
  }
}

static AOM_INLINE void inverse_transform_block(DecoderCodingBlock *dcb,
                                               const AV1_COMMON *cm, int plane,
                                               const TX_TYPE tx_type,
                                               const TX_SIZE tx_size,
                                               uint16_t *dst, int stride,
                                               int reduced_tx_set) {
  tran_low_t *dqcoeff = dcb->dqcoeff_block[plane] + dcb->cb_offset[plane];
  eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
  uint16_t scan_line = eob_data->max_scan_line;
  uint16_t eob = eob_data->eob;
  // Update eob and scan_line according to those of the other chroma plane
  if (plane && is_cctx_allowed(cm, &dcb->xd)) {
    eob_info *eob_data_c1 =
        dcb->eob_data[AOM_PLANE_U] + dcb->txb_offset[AOM_PLANE_U];
    eob_info *eob_data_c2 =
        dcb->eob_data[AOM_PLANE_V] + dcb->txb_offset[AOM_PLANE_V];
    scan_line = AOMMAX(eob_data_c1->max_scan_line, eob_data_c2->max_scan_line);
    eob = AOMMAX(eob_data_c1->eob, eob_data_c2->eob);
  }
  av1_inverse_transform_block(&dcb->xd, dqcoeff, plane, tx_type, tx_size, dst,
                              stride, eob, reduced_tx_set);
  const int width = tx_size_wide[tx_size] <= 32 ? tx_size_wide[tx_size] : 32;
  const int height = tx_size_high[tx_size] <= 32 ? tx_size_high[tx_size] : 32;
  const int sbSize = (width >= 8 && height >= 8) ? 8 : 4;
  int32_t nz0 = (sbSize - 1) * tx_size_wide[tx_size] + sbSize;
  int32_t nz1 = (scan_line + 1);
  memset(dqcoeff, 0, AOMMAX(nz0, nz1) * sizeof(dqcoeff[0]));
}

static AOM_INLINE void read_coeffs_tx_intra_block(
    const AV1_COMMON *const cm, DecoderCodingBlock *dcb, aom_reader *const r,
    const int plane, const int row, const int col, const TX_SIZE tx_size) {
  MB_MODE_INFO *mbmi = dcb->xd.mi[0];
  if (!mbmi->skip_txfm[dcb->xd.tree_type == CHROMA_PART]) {
#if TXCOEFF_TIMER
    struct aom_usec_timer timer;
    aom_usec_timer_start(&timer);
#endif
    av1_read_coeffs_txb_facade(cm, dcb, r, plane, row, col, tx_size);
#if TXCOEFF_TIMER
    aom_usec_timer_mark(&timer);
    const int64_t elapsed_time = aom_usec_timer_elapsed(&timer);
    cm->txcoeff_timer += elapsed_time;
    ++cm->txb_count;
#endif
  }
#if CONFIG_PC_WIENER
  else {
    // all tx blocks are skipped.
    av1_update_txk_skip_array(cm, dcb->xd.mi_row, dcb->xd.mi_col,
                              dcb->xd.tree_type, &mbmi->chroma_ref_info, plane,
                              row, col, tx_size);
  }
#endif  // CONFIG_PC_WIENER
}

static AOM_INLINE void decode_block_void(const AV1_COMMON *const cm,
                                         DecoderCodingBlock *dcb,
                                         aom_reader *const r, const int plane,
                                         const int row, const int col,
                                         const TX_SIZE tx_size) {
  (void)cm;
  (void)dcb;
  (void)r;
  (void)plane;
  (void)row;
  (void)col;
  (void)tx_size;
}

static AOM_INLINE void predict_inter_block_void(AV1_COMMON *const cm,
                                                DecoderCodingBlock *dcb,
                                                BLOCK_SIZE bsize) {
  (void)cm;
  (void)dcb;
  (void)bsize;
}

static AOM_INLINE void cfl_store_inter_block_void(AV1_COMMON *const cm,
                                                  MACROBLOCKD *const xd) {
  (void)cm;
  (void)xd;
}

static AOM_INLINE void predict_and_reconstruct_intra_block(
    const AV1_COMMON *const cm, DecoderCodingBlock *dcb, aom_reader *const r,
    const int plane, const int row, const int col, const TX_SIZE tx_size) {
  (void)r;
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  PLANE_TYPE plane_type = get_plane_type(plane);

  av1_predict_intra_block_facade(cm, xd, plane, col, row, tx_size);
#if CONFIG_INSPECTION
  {
    const int txwpx = tx_size_wide[tx_size];
    const int txhpx = tx_size_high[tx_size];

    struct macroblockd_plane *const pd = &xd->plane[plane];
    const int dst_stride = pd->dst.stride;
    uint16_t *dst = &pd->dst.buf[(row * dst_stride + col) << MI_SIZE_LOG2];
    for (int i = 0; i < txhpx; i++) {
      for (int j = 0; j < txwpx; j++) {
        uint16_t pixel = dst[i * dst_stride + j];
        int stride = cm->predicted_pixels.strides[plane > 0];
        int pixel_c, pixel_r;

        if (plane) {
          mi_to_pixel_loc(&pixel_c, &pixel_r,
                          mbmi->chroma_ref_info.mi_col_chroma_base,
                          mbmi->chroma_ref_info.mi_row_chroma_base, col, row,
                          pd->subsampling_x, pd->subsampling_y);
        } else {
          mi_to_pixel_loc(&pixel_c, &pixel_r, xd->mi_col, xd->mi_row, col, row,
                          pd->subsampling_x, pd->subsampling_y);
        }

        pixel_c += j;
        pixel_r += i;
        cm->predicted_pixels.buffers[plane][pixel_r * stride + pixel_c] = pixel;
      }
    }
  }
#endif  // CONFIG_INSPECTION

#if CONFIG_MISMATCH_DEBUG
  const int mi_row = -xd->mb_to_top_edge >> (3 + MI_SIZE_LOG2);
  const int mi_col = -xd->mb_to_left_edge >> (3 + MI_SIZE_LOG2);
  int pixel_c, pixel_r;
  BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
  int blk_w = block_size_wide[bsize];
  int blk_h = block_size_high[bsize];
  if (plane == 0 || xd->is_chroma_ref) {
    struct macroblockd_plane *const pd = &xd->plane[plane];
    if (plane) {
      mi_to_pixel_loc(&pixel_c, &pixel_r,
                      mbmi->chroma_ref_info.mi_col_chroma_base,
                      mbmi->chroma_ref_info.mi_row_chroma_base, col, row,
                      pd->subsampling_x, pd->subsampling_y);
    } else {
      mi_to_pixel_loc(&pixel_c, &pixel_r, mi_col, mi_row, col, row,
                      pd->subsampling_x, pd->subsampling_y);
    }
    mismatch_check_block_pre(pd->dst.buf, pd->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             cm->current_frame.display_order_hint,
#else
                             cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             plane, pixel_c, pixel_r, blk_w, blk_h);
  }
#endif  // CONFIG_MISMATCH_DEBUG

  if (!mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
    eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
    // In CCTX, when C2 eob = 0 but C1 eob > 0, plane V reconstruction is
    // still needed
    int recon_with_cctx = 0;
    if (is_cctx_allowed(cm, xd) && plane == AOM_PLANE_V &&
        av1_get_cctx_type(xd, row, col) > CCTX_NONE) {
      eob_info *eob_data_c1 =
          dcb->eob_data[AOM_PLANE_U] + dcb->txb_offset[AOM_PLANE_U];
      recon_with_cctx = eob_data_c1->eob > 0;
    }
    if (eob_data->eob || recon_with_cctx) {
      const bool reduced_tx_set_used = cm->features.reduced_tx_set_used;
      // tx_type was read out in av1_read_coeffs_txb.
      const TX_TYPE tx_type = av1_get_tx_type(xd, plane_type, row, col, tx_size,
                                              reduced_tx_set_used);
      struct macroblockd_plane *const pd = &xd->plane[plane];
      uint16_t *dst =
          &pd->dst.buf[(row * pd->dst.stride + col) << MI_SIZE_LOG2];
      inverse_transform_block(dcb, cm, plane, tx_type, tx_size, dst,
                              pd->dst.stride, reduced_tx_set_used);
    }
  }

#if CONFIG_MISMATCH_DEBUG
  {
    struct macroblockd_plane *const pd = &xd->plane[plane];
    uint16_t *dst = &pd->dst.buf[(row * pd->dst.stride + col) << MI_SIZE_LOG2];
    if (plane) {
      mi_to_pixel_loc(&pixel_c, &pixel_r,
                      mbmi->chroma_ref_info.mi_col_chroma_base,
                      mbmi->chroma_ref_info.mi_row_chroma_base, col, row,
                      pd->subsampling_x, pd->subsampling_y);
    } else {
      mi_to_pixel_loc(&pixel_c, &pixel_r, mi_col, mi_row, col, row,
                      pd->subsampling_x, pd->subsampling_y);
    }
    mismatch_check_block_tx(dst, pd->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                            cm->current_frame.display_order_hint,
#else
                            cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                            plane, pixel_c, pixel_r, blk_w, blk_h);
  }
#endif  // CONFIG_MISMATCH_DEBUG

  if (plane == AOM_PLANE_Y && store_cfl_required(cm, xd) &&
      xd->tree_type == SHARED_PART) {
#if CONFIG_ADAPTIVE_DS_FILTER
    cfl_store_tx(xd, row, col, tx_size, cm->seq_params.enable_cfl_ds_filter);
#else
    cfl_store_tx(xd, row, col, tx_size);
#endif  // CONFIG_ADAPTIVE_DS_FILTER
  }
}

// Facade function for inverse cross chroma component transform
static AOM_INLINE void inverse_cross_chroma_transform_block(
    const AV1_COMMON *const cm, DecoderCodingBlock *dcb, aom_reader *const r,
    const int plane, const int blk_row, const int blk_col,
    const TX_SIZE tx_size) {
  (void)cm;
  (void)r;
  (void)plane;
  tran_low_t *dqcoeff_c1 =
      dcb->dqcoeff_block[AOM_PLANE_U] + dcb->cb_offset[AOM_PLANE_U];
  tran_low_t *dqcoeff_c2 =
      dcb->dqcoeff_block[AOM_PLANE_V] + dcb->cb_offset[AOM_PLANE_V];
  MACROBLOCKD *const xd = &dcb->xd;
  const CctxType cctx_type = av1_get_cctx_type(xd, blk_row, blk_col);
  av1_inv_cross_chroma_tx_block(dqcoeff_c1, dqcoeff_c2, tx_size, cctx_type);
}

static AOM_INLINE void inverse_transform_inter_block(
    const AV1_COMMON *const cm, DecoderCodingBlock *dcb, aom_reader *const r,
    const int plane, const int blk_row, const int blk_col,
    const TX_SIZE tx_size) {
  (void)r;
  MACROBLOCKD *const xd = &dcb->xd;
  PLANE_TYPE plane_type = get_plane_type(plane);
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const bool reduced_tx_set_used = cm->features.reduced_tx_set_used;
  // tx_type was read out in av1_read_coeffs_txb.
  const TX_TYPE tx_type = av1_get_tx_type(xd, plane_type, blk_row, blk_col,
                                          tx_size, reduced_tx_set_used);

  uint16_t *dst =
      &pd->dst.buf[(blk_row * pd->dst.stride + blk_col) << MI_SIZE_LOG2];
  inverse_transform_block(dcb, cm, plane, tx_type, tx_size, dst, pd->dst.stride,
                          reduced_tx_set_used);
#if CONFIG_MISMATCH_DEBUG
  int pixel_c, pixel_r;
  BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
  int blk_w = block_size_wide[bsize];
  int blk_h = block_size_high[bsize];
  const int mi_row = -xd->mb_to_top_edge >> (3 + MI_SIZE_LOG2);
  const int mi_col = -xd->mb_to_left_edge >> (3 + MI_SIZE_LOG2);
  if (plane) {
    MB_MODE_INFO *const mbmi = xd->mi[0];
    mi_to_pixel_loc(&pixel_c, &pixel_r,
                    mbmi->chroma_ref_info.mi_col_chroma_base,
                    mbmi->chroma_ref_info.mi_row_chroma_base, blk_col, blk_row,
                    pd->subsampling_x, pd->subsampling_y);
  } else {
    mi_to_pixel_loc(&pixel_c, &pixel_r, mi_col, mi_row, blk_col, blk_row,
                    pd->subsampling_x, pd->subsampling_y);
  }
  mismatch_check_block_tx(dst, pd->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                          cm->current_frame.display_order_hint,
#else
                          cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                          plane, pixel_c, pixel_r, blk_w, blk_h);
#endif  // CONFIG_MISMATCH_DEBUG
}

static AOM_INLINE void set_cb_buffer_offsets(DecoderCodingBlock *dcb,
                                             TX_SIZE tx_size, int plane) {
  dcb->cb_offset[plane] += tx_size_wide[tx_size] * tx_size_high[tx_size];
  dcb->txb_offset[plane] =
      dcb->cb_offset[plane] / (TX_SIZE_W_MIN * TX_SIZE_H_MIN);
}

static AOM_INLINE void decode_reconstruct_tx(AV1_COMMON *cm,
                                             ThreadData *const td,
                                             aom_reader *r,
                                             MB_MODE_INFO *const mbmi,
                                             int plane, BLOCK_SIZE plane_bsize,
                                             int blk_row, int blk_col,
#if !CONFIG_NEW_TX_PARTITION
                                             int block,
#endif  // !CONFIG_NEW_TX_PARTITION
                                             TX_SIZE tx_size, int *eob_total) {
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  if (plane == AOM_PLANE_U && is_cctx_allowed(cm, xd)) return;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
#if CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(bsize_base, pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
#else
  if (xd->tree_type == SHARED_PART)
    assert(mbmi->sb_type[PLANE_TYPE_Y] == mbmi->sb_type[PLANE_TYPE_UV]);
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(mbmi->sb_type[plane > 0], pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  // Scale to match transform block unit.
  const int max_blocks_high = max_block_high(xd, plane_bsize, plane);
  const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;

  if (tx_size == plane_tx_size || plane) {
    if (plane == AOM_PLANE_V && is_cctx_allowed(cm, xd)) {
      td->read_coeffs_tx_inter_block_visit(cm, dcb, r, AOM_PLANE_U, blk_row,
                                           blk_col, tx_size);
      td->read_coeffs_tx_inter_block_visit(cm, dcb, r, AOM_PLANE_V, blk_row,
                                           blk_col, tx_size);
      td->inverse_cctx_block_visit(cm, dcb, r, -1, blk_row, blk_col, tx_size);
      td->inverse_tx_inter_block_visit(cm, dcb, r, AOM_PLANE_U, blk_row,
                                       blk_col, tx_size);
      td->inverse_tx_inter_block_visit(cm, dcb, r, AOM_PLANE_V, blk_row,
                                       blk_col, tx_size);
      eob_info *eob_data_c1 =
          dcb->eob_data[AOM_PLANE_U] + dcb->txb_offset[AOM_PLANE_U];
      eob_info *eob_data_c2 =
          dcb->eob_data[AOM_PLANE_V] + dcb->txb_offset[AOM_PLANE_V];
      *eob_total += eob_data_c1->eob + eob_data_c2->eob;
      set_cb_buffer_offsets(dcb, tx_size, AOM_PLANE_U);
      set_cb_buffer_offsets(dcb, tx_size, AOM_PLANE_V);
    } else {
      assert(plane == AOM_PLANE_Y || !is_cctx_allowed(cm, xd));
      td->read_coeffs_tx_inter_block_visit(cm, dcb, r, plane, blk_row, blk_col,
                                           tx_size);

      td->inverse_tx_inter_block_visit(cm, dcb, r, plane, blk_row, blk_col,
                                       tx_size);
      eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
      *eob_total += eob_data->eob;
      set_cb_buffer_offsets(dcb, tx_size, plane);
    }
  } else {
#if CONFIG_NEW_TX_PARTITION
    TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
    const int index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);
    get_tx_partition_sizes(mbmi->tx_partition_type[index], tx_size, sub_txs);
    int cur_partition = 0;
    int bsw = 0, bsh = 0;
    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh) {
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        const TX_SIZE sub_tx = sub_txs[cur_partition];
        bsw = tx_size_wide_unit[sub_tx];
        bsh = tx_size_high_unit[sub_tx];
        const int offsetr = blk_row + row;
        const int offsetc = blk_col + col;
        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;

        td->read_coeffs_tx_inter_block_visit(cm, dcb, r, plane, offsetr,
                                             offsetc, sub_tx);
        td->inverse_tx_inter_block_visit(cm, dcb, r, plane, offsetr, offsetc,
                                         sub_tx);
        eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
        *eob_total += eob_data->eob;
        set_cb_buffer_offsets(dcb, sub_tx, plane);
        cur_partition++;
      }
    }
#else
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    assert(IMPLIES(tx_size <= TX_4X4, sub_txs == tx_size));
    assert(IMPLIES(tx_size > TX_4X4, sub_txs < tx_size));
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];
    const int sub_step = bsw * bsh;

    assert(bsw > 0 && bsh > 0);

    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh) {
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        const int offsetr = blk_row + row;
        const int offsetc = blk_col + col;

        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;

        decode_reconstruct_tx(cm, td, r, mbmi, plane, plane_bsize, offsetr,
                              offsetc, block, sub_txs, eob_total);
        block += sub_step;
      }
    }
#endif  // CONFIG_NEW_TX_PARTITION
  }
}

static AOM_INLINE void set_offsets(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                                   BLOCK_SIZE bsize, int mi_row, int mi_col,
                                   int bw, int bh, int x_inside_boundary,
                                   int y_inside_boundary,
                                   PARTITION_TREE *parent, int index) {
  const int num_planes = av1_num_planes(cm);
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const TileInfo *const tile = &xd->tile;

  set_mi_offsets(mi_params, xd, mi_row, mi_col
#if CONFIG_C071_SUBBLK_WARPMV
                 ,
                 x_inside_boundary, y_inside_boundary
#endif  // CONFIG_C071_SUBBLK_WARPMV
  );
  xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART] = bsize;
#if CONFIG_RD_DEBUG
  xd->mi[0]->mi_row = mi_row;
  xd->mi[0]->mi_col = mi_col;
#endif

  if (xd->tree_type == SHARED_PART) {
    assert(x_inside_boundary && y_inside_boundary);
    for (int x = 1; x < x_inside_boundary; ++x) xd->mi[x] = xd->mi[0];
    int idx = mi_params->mi_stride;
    for (int y = 1; y < y_inside_boundary; ++y) {
      memcpy(&xd->mi[idx], &xd->mi[0], x_inside_boundary * sizeof(xd->mi[0]));
      idx += mi_params->mi_stride;
    }
  }

  CHROMA_REF_INFO *chroma_ref_info = &xd->mi[0]->chroma_ref_info;
  set_chroma_ref_info(xd->tree_type, mi_row, mi_col, index, bsize,
                      chroma_ref_info, parent ? &parent->chroma_ref_info : NULL,
                      parent ? parent->bsize : BLOCK_INVALID,
                      parent ? parent->partition : PARTITION_NONE,
                      xd->plane[1].subsampling_x, xd->plane[1].subsampling_y);
  set_plane_n4(xd, bw, bh, num_planes, chroma_ref_info);
  set_entropy_context(xd, mi_row, mi_col, num_planes, chroma_ref_info);

  // Distance of Mb to the various image edges. These are specified to 8th pel
  // as they are always compared to values that are in 1/8th pel units
  set_mi_row_col(xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows,
                 mi_params->mi_cols, chroma_ref_info);

  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes, chroma_ref_info);
}

#if !CONFIG_REFINEMV
typedef struct PadBlock {
  int x0;
  int x1;
  int y0;
  int y1;
} PadBlock;
#endif  //! CONFIG_REFINEMV

static AOM_INLINE void highbd_build_mc_border(const uint16_t *src,
                                              int src_stride, uint16_t *dst,
                                              int dst_stride, int x, int y,
                                              int b_w, int b_h, int w, int h) {
  // Get a pointer to the start of the real data for this row.
  const uint16_t *ref_row = src - x - y * src_stride;

  if (y >= h)
    ref_row += (h - 1) * src_stride;
  else if (y > 0)
    ref_row += y * src_stride;

  do {
    int right = 0, copy;
    int left = x < 0 ? -x : 0;

    if (left > b_w) left = b_w;

    if (x + b_w > w) right = x + b_w - w;

    if (right > b_w) right = b_w;

    copy = b_w - left - right;

    if (left) aom_memset16(dst, ref_row[0], left);

    if (copy) memcpy(dst + left, ref_row + x + left, copy * sizeof(uint16_t));

    if (right) aom_memset16(dst + left + copy, ref_row[w - 1], right);

    dst += dst_stride;
    ++y;

    if (y > 0 && y < h) ref_row += src_stride;
  } while (--b_h);
}

#if !CONFIG_REFINEMV
int update_extend_mc_border_params(const struct scale_factors *const sf,
                                   struct buf_2d *const pre_buf, MV32 scaled_mv,
                                   PadBlock *block, int subpel_x_mv,
                                   int subpel_y_mv, int do_warp, int is_intrabc,
                                   int *x_pad, int *y_pad) {
  // Get reference width and height.
  int frame_width = pre_buf->width;
  int frame_height = pre_buf->height;

  // Do border extension if there is motion or
  // width/height is not a multiple of 8 pixels.
  // Extension is needed in optical flow refinement to obtain MV offsets
  (void)scaled_mv;
  if (!is_intrabc && !do_warp) {
    if (subpel_x_mv || (sf->x_step_q4 != SUBPEL_SHIFTS)) {
      block->x0 -= AOM_INTERP_EXTEND - 1;
      block->x1 += AOM_INTERP_EXTEND;
      *x_pad = 1;
    }

    if (subpel_y_mv || (sf->y_step_q4 != SUBPEL_SHIFTS)) {
      block->y0 -= AOM_INTERP_EXTEND - 1;
      block->y1 += AOM_INTERP_EXTEND;
      *y_pad = 1;
    }

    // Skip border extension if block is inside the frame.
    if (block->x0 < 0 || block->x1 > frame_width - 1 || block->y0 < 0 ||
        block->y1 > frame_height - 1) {
      return 1;
    }
  }
  return 0;
}
#endif  //! CONFIG_REFINEMV

static INLINE void extend_mc_border(const struct scale_factors *const sf,
                                    struct buf_2d *const pre_buf,
                                    MV32 scaled_mv, PadBlock block,
                                    int subpel_x_mv, int subpel_y_mv,
                                    int do_warp, int is_intrabc,
                                    uint16_t *mc_buf, uint16_t **pre,
                                    int *src_stride) {
  int x_pad = 0, y_pad = 0;
  if (update_extend_mc_border_params(sf, pre_buf, scaled_mv, &block,
                                     subpel_x_mv, subpel_y_mv, do_warp,
                                     is_intrabc, &x_pad, &y_pad
#if CONFIG_REFINEMV
                                     ,
                                     NULL
#endif  // CONFIG_REFINEMV

                                     )) {
    // Get reference block pointer.
    const uint16_t *const buf_ptr =
        pre_buf->buf0 + block.y0 * pre_buf->stride + block.x0;
    int buf_stride = pre_buf->stride;
    const int b_w = block.x1 - block.x0;
    const int b_h = block.y1 - block.y0;

    // Extend the border.
    highbd_build_mc_border(buf_ptr, buf_stride, mc_buf, b_w, block.x0, block.y0,
                           b_w, b_h, pre_buf->width, pre_buf->height);

    *src_stride = b_w;
    *pre = mc_buf + y_pad * (AOM_INTERP_EXTEND - 1) * b_w +
           x_pad * (AOM_INTERP_EXTEND - 1);
  }
}
#if !CONFIG_REFINEMV
static void dec_calc_subpel_params(
    const MV *const src_mv, InterPredParams *const inter_pred_params,
    const MACROBLOCKD *const xd, int mi_x, int mi_y, uint16_t **pre,
    SubpelParams *subpel_params, int *src_stride, PadBlock *block,
#if CONFIG_OPTFLOW_REFINEMENT
    int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
    MV32 *scaled_mv, int *subpel_x_mv, int *subpel_y_mv) {
  const struct scale_factors *sf = inter_pred_params->scale_factors;
  struct buf_2d *pre_buf = &inter_pred_params->ref_frame_buf;
#if CONFIG_OPTFLOW_REFINEMENT
  // Use original block size to clamp MV and to extend block boundary
  const int bw = use_optflow_refinement ? inter_pred_params->orig_block_width
                                        : inter_pred_params->block_width;
  const int bh = use_optflow_refinement ? inter_pred_params->orig_block_height
                                        : inter_pred_params->block_height;
#else
  const int bw = inter_pred_params->block_width;
  const int bh = inter_pred_params->block_height;
#endif  // CONFIG_OPTFLOW_REFINEMENT
  const int is_scaled = av1_is_scaled(sf);
  if (is_scaled) {
    int ssx = inter_pred_params->subsampling_x;
    int ssy = inter_pred_params->subsampling_y;
    int orig_pos_y = inter_pred_params->pix_row << SUBPEL_BITS;
    int orig_pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
#if CONFIG_OPTFLOW_REFINEMENT
    if (use_optflow_refinement) {
      orig_pos_y += ROUND_POWER_OF_TWO_SIGNED(src_mv->row * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssy);
      orig_pos_x += ROUND_POWER_OF_TWO_SIGNED(src_mv->col * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssx);
    } else {
      orig_pos_y += src_mv->row * (1 << (1 - ssy));
      orig_pos_x += src_mv->col * (1 << (1 - ssx));
    }
#else
    orig_pos_y += src_mv->row * (1 << (1 - ssy));
    orig_pos_x += src_mv->col * (1 << (1 - ssx));
#endif  // CONFIG_OPTFLOW_REFINEMENT
    int pos_y = sf->scale_value_y(orig_pos_y, sf);
    int pos_x = sf->scale_value_x(orig_pos_x, sf);
    pos_x += SCALE_EXTRA_OFF;
    pos_y += SCALE_EXTRA_OFF;

    const int top = -AOM_LEFT_TOP_MARGIN_SCALED(ssy);
    const int left = -AOM_LEFT_TOP_MARGIN_SCALED(ssx);
    const int bottom = (pre_buf->height + AOM_INTERP_EXTEND)
                       << SCALE_SUBPEL_BITS;
    const int right = (pre_buf->width + AOM_INTERP_EXTEND) << SCALE_SUBPEL_BITS;
    pos_y = clamp(pos_y, top, bottom);
    pos_x = clamp(pos_x, left, right);

    subpel_params->subpel_x = pos_x & SCALE_SUBPEL_MASK;
    subpel_params->subpel_y = pos_y & SCALE_SUBPEL_MASK;
    subpel_params->xs = sf->x_step_q4;
    subpel_params->ys = sf->y_step_q4;

    // Get reference block top left coordinate.
    block->x0 = pos_x >> SCALE_SUBPEL_BITS;
    block->y0 = pos_y >> SCALE_SUBPEL_BITS;

    // Get reference block bottom right coordinate.
    block->x1 =
        ((pos_x + (inter_pred_params->block_width - 1) * subpel_params->xs) >>
         SCALE_SUBPEL_BITS) +
        1;
    block->y1 =
        ((pos_y + (inter_pred_params->block_height - 1) * subpel_params->ys) >>
         SCALE_SUBPEL_BITS) +
        1;

    MV temp_mv;
    temp_mv = clamp_mv_to_umv_border_sb(xd, src_mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
                                        use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                        inter_pred_params->subsampling_x,
                                        inter_pred_params->subsampling_y);
    *scaled_mv = av1_scale_mv(&temp_mv, mi_x, mi_y, sf);
    scaled_mv->row += SCALE_EXTRA_OFF;
    scaled_mv->col += SCALE_EXTRA_OFF;

    *subpel_x_mv = scaled_mv->col & SCALE_SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SCALE_SUBPEL_MASK;
  } else {
    // Get block position in current frame.
    int pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
    int pos_y = inter_pred_params->pix_row << SUBPEL_BITS;

    const MV mv_q4 = clamp_mv_to_umv_border_sb(
        xd, src_mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
        use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
        inter_pred_params->subsampling_x, inter_pred_params->subsampling_y);
    subpel_params->xs = subpel_params->ys = SCALE_SUBPEL_SHIFTS;
    subpel_params->subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    subpel_params->subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;

    // Get reference block top left coordinate.
    pos_x += mv_q4.col;
    pos_y += mv_q4.row;
    block->x0 = pos_x >> SUBPEL_BITS;
    block->y0 = pos_y >> SUBPEL_BITS;

    // Get reference block bottom right coordinate.
    block->x1 =
        (pos_x >> SUBPEL_BITS) + (inter_pred_params->block_width - 1) + 1;
    block->y1 =
        (pos_y >> SUBPEL_BITS) + (inter_pred_params->block_height - 1) + 1;

    scaled_mv->row = mv_q4.row;
    scaled_mv->col = mv_q4.col;
    *subpel_x_mv = scaled_mv->col & SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SUBPEL_MASK;
  }
  *pre = pre_buf->buf0 + block->y0 * pre_buf->stride + block->x0;
  *src_stride = pre_buf->stride;

#if CONFIG_D071_IMP_MSK_BLD
  if (inter_pred_params->border_data.enable_bacp) {
    subpel_params->x0 = block->x0;
    subpel_params->x1 = block->x1;
    subpel_params->y0 = block->y0;
    subpel_params->y1 = block->y1;
  }
#endif  // CONFIG_D071_IMP_MSK_BLD
}
#endif  //! CONFIG_REFINEMV
static void dec_calc_subpel_params_and_extend(
    const MV *const src_mv, InterPredParams *const inter_pred_params,
    MACROBLOCKD *const xd, int mi_x, int mi_y, int ref,
#if CONFIG_OPTFLOW_REFINEMENT
    int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
    uint16_t **mc_buf, uint16_t **pre, SubpelParams *subpel_params,
    int *src_stride) {

#if CONFIG_REFINEMV
  if (inter_pred_params->use_ref_padding) {
    common_calc_subpel_params_and_extend(
        src_mv, inter_pred_params, xd, mi_x, mi_y, ref,
#if CONFIG_OPTFLOW_REFINEMENT
        use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
        mc_buf, pre, subpel_params, src_stride);
    return;
  }
#endif

  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;
  dec_calc_subpel_params(src_mv, inter_pred_params, xd, mi_x, mi_y, pre,
                         subpel_params, src_stride, &block,
#if CONFIG_OPTFLOW_REFINEMENT
                         use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                         &scaled_mv, &subpel_x_mv, &subpel_y_mv);
  extend_mc_border(inter_pred_params->scale_factors,
                   &inter_pred_params->ref_frame_buf, scaled_mv, block,
                   subpel_x_mv, subpel_y_mv,
                   inter_pred_params->mode == WARP_PRED,
                   inter_pred_params->is_intrabc, mc_buf[ref], pre, src_stride);
}

#if !CONFIG_REFINEMV
static AOM_INLINE void tip_dec_calc_subpel_params(
    const MV *const src_mv, InterPredParams *const inter_pred_params, int mi_x,
    int mi_y, uint16_t **pre, SubpelParams *subpel_params, int *src_stride,
    PadBlock *block,
#if CONFIG_OPTFLOW_REFINEMENT
    int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
    MV32 *scaled_mv, int *subpel_x_mv, int *subpel_y_mv) {
  const struct scale_factors *sf = inter_pred_params->scale_factors;
  struct buf_2d *pre_buf = &inter_pred_params->ref_frame_buf;

#if CONFIG_REFINEMV
  const int bw = inter_pred_params->original_pu_width;
  const int bh = inter_pred_params->original_pu_height;
#else
#if CONFIG_OPTFLOW_REFINEMENT
  // Use original block size to clamp MV and to extend block boundary
  const int bw = use_optflow_refinement ? inter_pred_params->orig_block_width
                                        : inter_pred_params->block_width;
  const int bh = use_optflow_refinement ? inter_pred_params->orig_block_height
                                        : inter_pred_params->block_height;
#else
  const int bw = inter_pred_params->block_width;
  const int bh = inter_pred_params->block_height;
#endif  // CONFIG_OPTFLOW_REFINEMENT
#endif  // CONFIG_REFINEMV

  const int is_scaled = av1_is_scaled(sf);
  if (is_scaled) {
    const int ssx = inter_pred_params->subsampling_x;
    const int ssy = inter_pred_params->subsampling_y;
    int orig_pos_y = inter_pred_params->pix_row << SUBPEL_BITS;
    int orig_pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
#if CONFIG_OPTFLOW_REFINEMENT
    if (use_optflow_refinement) {
      orig_pos_y += ROUND_POWER_OF_TWO_SIGNED(src_mv->row * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssy);
      orig_pos_x += ROUND_POWER_OF_TWO_SIGNED(src_mv->col * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssx);
    } else {
      orig_pos_y += src_mv->row * (1 << (1 - ssy));
      orig_pos_x += src_mv->col * (1 << (1 - ssx));
    }
#else
    orig_pos_y += src_mv->row * (1 << (1 - ssy));
    orig_pos_x += src_mv->col * (1 << (1 - ssx));
#endif  // CONFIG_OPTFLOW_REFINEMENT
    int pos_y = sf->scale_value_y(orig_pos_y, sf);
    int pos_x = sf->scale_value_x(orig_pos_x, sf);
    pos_x += SCALE_EXTRA_OFF;
    pos_y += SCALE_EXTRA_OFF;

    const int top = -AOM_LEFT_TOP_MARGIN_SCALED(ssy);
    const int left = -AOM_LEFT_TOP_MARGIN_SCALED(ssx);
    const int bottom = (pre_buf->height + AOM_INTERP_EXTEND)
                       << SCALE_SUBPEL_BITS;
    const int right = (pre_buf->width + AOM_INTERP_EXTEND) << SCALE_SUBPEL_BITS;
    pos_y = clamp(pos_y, top, bottom);
    pos_x = clamp(pos_x, left, right);

    subpel_params->subpel_x = pos_x & SCALE_SUBPEL_MASK;
    subpel_params->subpel_y = pos_y & SCALE_SUBPEL_MASK;
    subpel_params->xs = sf->x_step_q4;
    subpel_params->ys = sf->y_step_q4;

    // Get reference block top left coordinate.
    block->x0 = pos_x >> SCALE_SUBPEL_BITS;
    block->y0 = pos_y >> SCALE_SUBPEL_BITS;

    // Get reference block bottom right coordinate.
#if CONFIG_D071_IMP_MSK_BLD
    block->x1 =
        ((pos_x + (inter_pred_params->block_width - 1) * subpel_params->xs) >>
         SCALE_SUBPEL_BITS) +
        1;
    block->y1 =
        ((pos_y + (inter_pred_params->block_height - 1) * subpel_params->ys) >>
         SCALE_SUBPEL_BITS) +
        1;
#else
    block->x1 =
        ((pos_x + (bw - 1) * subpel_params->xs) >> SCALE_SUBPEL_BITS) + 1;
    block->y1 =
        ((pos_y + (bh - 1) * subpel_params->ys) >> SCALE_SUBPEL_BITS) + 1;
#endif  // CONFIG_D071_IMP_MSK_BLD

    MV temp_mv;
    temp_mv = tip_clamp_mv_to_umv_border_sb(inter_pred_params, src_mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
                                            use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                            inter_pred_params->subsampling_x,
                                            inter_pred_params->subsampling_y);
    *scaled_mv = av1_scale_mv(&temp_mv, mi_x, mi_y, sf);
    scaled_mv->row += SCALE_EXTRA_OFF;
    scaled_mv->col += SCALE_EXTRA_OFF;

    *subpel_x_mv = scaled_mv->col & SCALE_SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SCALE_SUBPEL_MASK;
  } else {
    // Get block position in current frame.
    int pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
    int pos_y = inter_pred_params->pix_row << SUBPEL_BITS;

    const MV mv_q4 = tip_clamp_mv_to_umv_border_sb(
        inter_pred_params, src_mv, bw, bh,
#if CONFIG_OPTFLOW_REFINEMENT
        use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
        inter_pred_params->subsampling_x, inter_pred_params->subsampling_y);
    subpel_params->xs = subpel_params->ys = SCALE_SUBPEL_SHIFTS;
    subpel_params->subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    subpel_params->subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;

    // Get reference block top left coordinate.
    pos_x += mv_q4.col;
    pos_y += mv_q4.row;
    pos_x = (pos_x >> SUBPEL_BITS);
    pos_y = (pos_y >> SUBPEL_BITS);
    block->x0 = pos_x;
    block->y0 = pos_y;

    // Get reference block bottom right coordinate.
#if CONFIG_D071_IMP_MSK_BLD
    block->x1 = pos_x + inter_pred_params->block_width;
    block->y1 = pos_y + inter_pred_params->block_height;
#else
    block->x1 = pos_x + bw;
    block->y1 = pos_y + bh;
#endif  // CONFIG_D071_IMP_MSK_BLD

    scaled_mv->row = mv_q4.row;
    scaled_mv->col = mv_q4.col;
    *subpel_x_mv = scaled_mv->col & SUBPEL_MASK;
    *subpel_y_mv = scaled_mv->row & SUBPEL_MASK;
  }
  *pre = pre_buf->buf0 + block->y0 * pre_buf->stride + block->x0;
  *src_stride = pre_buf->stride;

#if CONFIG_D071_IMP_MSK_BLD
  if (inter_pred_params->border_data.enable_bacp) {
    subpel_params->x0 = block->x0;
    subpel_params->x1 = block->x1;
    subpel_params->y0 = block->y0;
    subpel_params->y1 = block->y1;
  }
#endif  // CONFIG_D071_IMP_MSK_BLD
}
#endif
static void tip_dec_calc_subpel_params_and_extend(
    const MV *const src_mv, InterPredParams *const inter_pred_params,
    MACROBLOCKD *const xd, int mi_x, int mi_y, int ref,
#if CONFIG_OPTFLOW_REFINEMENT
    int use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
    uint16_t **mc_buf, uint16_t **pre, SubpelParams *subpel_params,
    int *src_stride) {

#if CONFIG_REFINEMV
  if (inter_pred_params->use_ref_padding) {
    // printf(" used pading in the decoder \n");
    tip_common_calc_subpel_params_and_extend(
        src_mv, inter_pred_params, xd, mi_x, mi_y, ref,
#if CONFIG_OPTFLOW_REFINEMENT
        use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
        mc_buf, pre, subpel_params, src_stride);
    return;
  }
#else

  (void)xd;
#endif  // CONFIG_REFINEMV
  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;
  tip_dec_calc_subpel_params(src_mv, inter_pred_params, mi_x, mi_y, pre,
                             subpel_params, src_stride, &block,
#if CONFIG_OPTFLOW_REFINEMENT
                             use_optflow_refinement,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                             &scaled_mv, &subpel_x_mv, &subpel_y_mv);
  extend_mc_border(inter_pred_params->scale_factors,
                   &inter_pred_params->ref_frame_buf, scaled_mv, block,
                   subpel_x_mv, subpel_y_mv,
                   inter_pred_params->mode == WARP_PRED,
                   inter_pred_params->is_intrabc, mc_buf[ref], pre, src_stride);
}

static void av1_dec_setup_tip_frame(AV1_COMMON *cm, MACROBLOCKD *xd,
                                    uint16_t **mc_buf,
                                    CONV_BUF_TYPE *tmp_conv_dst) {
  av1_setup_tip_motion_field(cm, 0);

  av1_setup_tip_frame(cm, xd, mc_buf, tmp_conv_dst,
                      tip_dec_calc_subpel_params_and_extend);
#if CONFIG_TIP_IMPLICIT_QUANT
  if (cm->seq_params.enable_tip_explicit_qp == 0) {
    const int avg_u_ac_delta_q =
        (cm->tip_ref.ref_frame_buffer[0]->u_ac_delta_q +
         cm->tip_ref.ref_frame_buffer[1]->u_ac_delta_q + 1) >>
        1;
    const int avg_v_ac_delta_q =
        (cm->tip_ref.ref_frame_buffer[0]->v_ac_delta_q +
         cm->tip_ref.ref_frame_buffer[1]->v_ac_delta_q + 1) >>
        1;
    const int base_qindex =
        (cm->tip_ref.ref_frame_buffer[0]->base_qindex +
         cm->tip_ref.ref_frame_buffer[1]->base_qindex + 1) >>
        1;
    cm->cur_frame->base_qindex = cm->quant_params.base_qindex = base_qindex;
    cm->cur_frame->u_ac_delta_q = cm->quant_params.u_ac_delta_q =
        avg_u_ac_delta_q;
    cm->cur_frame->v_ac_delta_q = cm->quant_params.v_ac_delta_q =
        avg_v_ac_delta_q;
    if (cm->seq_params.enable_pef && cm->features.allow_pef) {
      init_pef_parameter(cm, 0, av1_num_planes(cm));
    }
  }
#endif  // CONFIG_TIP_IMPLICIT_QUANT
#if CONFIG_TIP_DIRECT_FRAME_MV
  if (cm->seq_params.enable_pef && cm->features.allow_pef) {
    enhance_tip_frame(cm, xd);
    aom_extend_frame_borders(&cm->tip_ref.tip_frame->buf, av1_num_planes(cm));
  }
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
}

static void av1_dec_tip_on_the_fly(AV1_COMMON *cm, MACROBLOCKD *xd,
                                   uint16_t **mc_buf, CONV_BUF_TYPE *conv_dst) {
  const MV *mv = &xd->mi[0]->mv[0].as_mv;
  const int mvs_rows =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  const int mvs_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  const int extra_pixel = 1 << TMVP_MI_SZ_LOG2;

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int x_inside_boundary = xd->width;
  const int y_inside_boundary = xd->height;

  // define the block start and end pixel locations
  FULLPEL_MV start_mv = get_fullmv_from_mv(mv);
  const int bw = (x_inside_boundary << MI_SIZE_LOG2);
  const int bh = (y_inside_boundary << MI_SIZE_LOG2);
  int start_pixel_row = (mi_row << MI_SIZE_LOG2) + start_mv.row;
  int start_pixel_col = (mi_col << MI_SIZE_LOG2) + start_mv.col;
  int end_pixel_row = start_pixel_row + bh;
  int end_pixel_col = start_pixel_col + bw;

  // extend for handling interpolation
  if (mv->row != 0) {
    start_pixel_row -= extra_pixel;
    end_pixel_row += extra_pixel;
  }

  if (mv->col != 0) {
    start_pixel_col -= extra_pixel;
    end_pixel_col += extra_pixel;
  }

  // clamp block start and end locations to make sure the block is in the
  // frame
  start_pixel_row = AOMMAX(0, start_pixel_row);
  start_pixel_col = AOMMAX(0, start_pixel_col);
  end_pixel_row = AOMMAX(0, end_pixel_row);
  end_pixel_col = AOMMAX(0, end_pixel_col);
  start_pixel_row = AOMMIN(cm->mi_params.mi_rows * MI_SIZE, start_pixel_row);
  start_pixel_col = AOMMIN(cm->mi_params.mi_cols * MI_SIZE, start_pixel_col);
  end_pixel_row = AOMMIN(cm->mi_params.mi_rows * MI_SIZE, end_pixel_row);
  end_pixel_col = AOMMIN(cm->mi_params.mi_cols * MI_SIZE, end_pixel_col);

  // convert the pixel block location to MV field grid location
  int tpl_start_row = start_pixel_row >> TMVP_MI_SZ_LOG2;
  int tpl_end_row = (end_pixel_row + TMVP_MI_SIZE - 1) >> TMVP_MI_SZ_LOG2;
  int tpl_start_col = start_pixel_col >> TMVP_MI_SZ_LOG2;
  int tpl_end_col = (end_pixel_col + TMVP_MI_SIZE - 1) >> TMVP_MI_SZ_LOG2;

  // handling the boundary case when start and end locations are the same
  if (tpl_start_row == tpl_end_row) {
    if (tpl_start_row > 0) {
      tpl_start_row -= 1;
    } else {
      tpl_end_row += 1;
    }
  }

  if (tpl_start_col == tpl_end_col) {
    if (tpl_start_col > 0) {
      tpl_start_col -= 1;
    } else {
      tpl_end_col += 1;
    }
  }

  // handle SIMD alignment for the chroma case
  tpl_start_row = (tpl_start_row >> 1) << 1;
  tpl_start_col = (tpl_start_col >> 1) << 1;
  tpl_end_row = ((tpl_end_row + 1) >> 1) << 1;
  tpl_end_col = ((tpl_end_col + 1) >> 1) << 1;
  tpl_end_row = AOMMIN(mvs_rows, tpl_end_row);
  tpl_end_col = AOMMIN(mvs_cols, tpl_end_col);

  av1_setup_tip_on_the_fly(cm, xd, tpl_start_row, tpl_start_col, tpl_end_row,
                           tpl_end_col, mvs_cols, mc_buf, conv_dst,
                           tip_dec_calc_subpel_params_and_extend);
}

static AOM_INLINE void decode_mbmi_block(AV1Decoder *const pbi,
                                         DecoderCodingBlock *dcb, int mi_row,
                                         int mi_col, aom_reader *r,
                                         PARTITION_TYPE partition,
                                         BLOCK_SIZE bsize,
                                         PARTITION_TREE *parent, int index) {
  AV1_COMMON *const cm = &pbi->common;
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int x_mis = AOMMIN(bw, cm->mi_params.mi_cols - mi_col);
  const int y_mis = AOMMIN(bh, cm->mi_params.mi_rows - mi_row);
  MACROBLOCKD *const xd = &dcb->xd;

#if CONFIG_ACCOUNTING
  aom_accounting_set_context(&pbi->accounting, mi_col, mi_row, xd->tree_type);
#endif
  set_offsets(cm, xd, bsize, mi_row, mi_col, bw, bh, x_mis, y_mis, parent,
              index);
  xd->mi[0]->partition = partition;
  av1_read_mode_info(pbi, dcb, r, x_mis, y_mis);

#if CONFIG_EXT_RECUR_PARTITIONS
  if (xd->tree_type != LUMA_PART) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    const struct macroblockd_plane *const pd_u = &xd->plane[1];
    const BLOCK_SIZE chroma_bsize_base =
        get_bsize_base(xd, xd->mi[0], AOM_PLANE_U);
    assert(chroma_bsize_base < BLOCK_SIZES_ALL);
    if (get_plane_block_size(chroma_bsize_base, pd_u->subsampling_x,
                             pd_u->subsampling_y) == BLOCK_INVALID) {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Block size %dx%d invalid with this subsampling mode",
                         block_size_wide[chroma_bsize_base],
                         block_size_high[chroma_bsize_base]);
    }
#if CONFIG_EXT_RECUR_PARTITIONS
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  if (cm->features.tip_frame_mode == TIP_FRAME_AS_REF &&
      is_tip_ref_frame(xd->mi[0]->ref_frame[0])) {
    av1_dec_tip_on_the_fly(cm, xd, pbi->td.mc_buf, pbi->td.tmp_conv_dst);
  }
}

static void dec_build_inter_predictors(const AV1_COMMON *cm,
                                       DecoderCodingBlock *dcb, int plane,
                                       MB_MODE_INFO *mi, int build_for_obmc,
                                       int bw, int bh, int mi_x, int mi_y
#if CONFIG_REFINEMV
                                       ,
                                       int build_for_refine_mv_only
#endif  // CONFIG_REFINEMV
) {
  av1_build_inter_predictors(cm, &dcb->xd, plane, mi,
#if CONFIG_BAWP
                             NULL,
#endif
#if CONFIG_REFINEMV
                             build_for_refine_mv_only,
#endif  // CONFIG_REFINEMV
                             build_for_obmc, bw, bh, mi_x, mi_y, dcb->mc_buf,
                             dec_calc_subpel_params_and_extend);
}

static AOM_INLINE void dec_build_inter_predictor(const AV1_COMMON *cm,
                                                 DecoderCodingBlock *dcb,
                                                 int mi_row, int mi_col,
                                                 BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &dcb->xd;
  const int num_planes = av1_num_planes(cm);

#if CONFIG_REFINEMV
  MB_MODE_INFO *mbmi = xd->mi[0];
  int need_subblock_mvs = xd->is_chroma_ref && mbmi->refinemv_flag &&
                          !is_intrabc_block(mbmi, xd->tree_type);
  assert(IMPLIES(need_subblock_mvs, !is_interintra_pred(mbmi)));
#if CONFIG_AFFINE_REFINEMENT
  if (need_subblock_mvs && default_refinemv_modes(cm, mbmi))
#else
  if (need_subblock_mvs && default_refinemv_modes(mbmi))
#endif  // CONFIG_AFFINE_REFINEMENT
    need_subblock_mvs &= (mbmi->comp_group_idx == 0 &&
                          mbmi->interinter_comp.type == COMPOUND_AVERAGE);
  if (need_subblock_mvs) {
    fill_subblock_refine_mv(xd->refinemv_subinfo, xd->plane[0].width,
                            xd->plane[0].height, mbmi->mv[0].as_mv,
                            mbmi->mv[1].as_mv);
  }
#endif  // CONFIG_REFINEMV

  for (int plane = 0; plane < num_planes; ++plane) {
    if (plane && !xd->is_chroma_ref) break;
    const int mi_x = mi_col * MI_SIZE;
    const int mi_y = mi_row * MI_SIZE;
    dec_build_inter_predictors(cm, dcb, plane, xd->mi[0], 0,
                               xd->plane[plane].width, xd->plane[plane].height,
                               mi_x, mi_y
#if CONFIG_REFINEMV
                               ,
                               0
#endif  // CONFIG_REFINEMV
    );

    assert(IMPLIES(!is_interintra_allowed(xd->mi[0]),
                   !is_interintra_mode(xd->mi[0])));

    if (is_interintra_pred(xd->mi[0])) {
      BUFFER_SET ctx = { { xd->plane[0].dst.buf, xd->plane[1].dst.buf,
                           xd->plane[2].dst.buf },
                         { xd->plane[0].dst.stride, xd->plane[1].dst.stride,
                           xd->plane[2].dst.stride } };
      av1_build_interintra_predictor(cm, xd, xd->plane[plane].dst.buf,
                                     xd->plane[plane].dst.stride, &ctx, plane,
                                     bsize);
    }
  }
}

static INLINE void dec_build_prediction_by_above_pred(
    MACROBLOCKD *const xd, int rel_mi_row, int rel_mi_col, uint8_t op_mi_size,
    int dir, MB_MODE_INFO *above_mbmi, void *fun_ctxt, const int num_planes) {
  struct build_prediction_ctxt *ctxt = (struct build_prediction_ctxt *)fun_ctxt;
  const int above_mi_col = xd->mi_col + rel_mi_col;
  int mi_x, mi_y;
  MB_MODE_INFO backup_mbmi = *above_mbmi;

  (void)rel_mi_row;
  (void)dir;

  av1_setup_build_prediction_by_above_pred(xd, rel_mi_col, op_mi_size,
                                           &backup_mbmi, ctxt, num_planes);
  mi_x = above_mi_col << MI_SIZE_LOG2;
  mi_y = xd->mi_row << MI_SIZE_LOG2;
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];

  for (int j = 0; j < num_planes; ++j) {
    const struct macroblockd_plane *pd = &xd->plane[j];
    int bw = (op_mi_size * MI_SIZE) >> pd->subsampling_x;
    int bh = clamp(block_size_high[bsize] >> (pd->subsampling_y + 1), 4,
                   block_size_high[BLOCK_64X64] >> (pd->subsampling_y + 1));

    if (av1_skip_u4x4_pred_in_obmc(bsize, pd, 0)) continue;
    dec_build_inter_predictors(ctxt->cm, (DecoderCodingBlock *)ctxt->dcb, j,
                               &backup_mbmi, 1, bw, bh, mi_x, mi_y
#if CONFIG_REFINEMV
                               ,
                               0
#endif  // CONFIG_REFINEMV
    );
  }
}

static AOM_INLINE void dec_build_prediction_by_above_preds(
    const AV1_COMMON *cm, DecoderCodingBlock *dcb,
    uint16_t *tmp_buf[MAX_MB_PLANE], int tmp_width[MAX_MB_PLANE],
    int tmp_height[MAX_MB_PLANE], int tmp_stride[MAX_MB_PLANE]) {
  MACROBLOCKD *const xd = &dcb->xd;
  if (!xd->up_available) return;

  // Adjust mb_to_bottom_edge to have the correct value for the OBMC
  // prediction block. This is half the height of the original block,
  // except for 128-wide blocks, where we only use a height of 32.
  const int this_height = xd->height * MI_SIZE;
  const int pred_height = AOMMIN(this_height / 2, 32);
  xd->mb_to_bottom_edge += GET_MV_SUBPEL(this_height - pred_height);
  struct build_prediction_ctxt ctxt = {
    cm, tmp_buf, tmp_width, tmp_height, tmp_stride, xd->mb_to_right_edge, dcb
  };
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
  foreach_overlappable_nb_above(
      cm, xd, max_neighbor_obmc[mi_size_wide_log2[bsize]],
      dec_build_prediction_by_above_pred, &ctxt, false);

  xd->mb_to_left_edge = -GET_MV_SUBPEL(xd->mi_col * MI_SIZE);
  xd->mb_to_right_edge = ctxt.mb_to_far_edge;
  xd->mb_to_bottom_edge -= GET_MV_SUBPEL(this_height - pred_height);
}

static INLINE void dec_build_prediction_by_left_pred(
    MACROBLOCKD *const xd, int rel_mi_row, int rel_mi_col, uint8_t op_mi_size,
    int dir, MB_MODE_INFO *left_mbmi, void *fun_ctxt, const int num_planes) {
  struct build_prediction_ctxt *ctxt = (struct build_prediction_ctxt *)fun_ctxt;
  const int left_mi_row = xd->mi_row + rel_mi_row;
  int mi_x, mi_y;
  MB_MODE_INFO backup_mbmi = *left_mbmi;

  (void)rel_mi_col;
  (void)dir;

  av1_setup_build_prediction_by_left_pred(xd, rel_mi_row, op_mi_size,
                                          &backup_mbmi, ctxt, num_planes);
  mi_x = xd->mi_col << MI_SIZE_LOG2;
  mi_y = left_mi_row << MI_SIZE_LOG2;
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART];

  for (int j = 0; j < num_planes; ++j) {
    const struct macroblockd_plane *pd = &xd->plane[j];
    int bw = clamp(block_size_wide[bsize] >> (pd->subsampling_x + 1), 4,
                   block_size_wide[BLOCK_64X64] >> (pd->subsampling_x + 1));
    int bh = (op_mi_size << MI_SIZE_LOG2) >> pd->subsampling_y;

    if (av1_skip_u4x4_pred_in_obmc(bsize, pd, 1)) continue;
    dec_build_inter_predictors(ctxt->cm, (DecoderCodingBlock *)ctxt->dcb, j,
                               &backup_mbmi, 1, bw, bh, mi_x, mi_y
#if CONFIG_REFINEMV
                               ,
                               0
#endif  // CONFIG_REFINEMV
    );
  }
}

static AOM_INLINE void dec_build_prediction_by_left_preds(
    const AV1_COMMON *cm, DecoderCodingBlock *dcb,
    uint16_t *tmp_buf[MAX_MB_PLANE], int tmp_width[MAX_MB_PLANE],
    int tmp_height[MAX_MB_PLANE], int tmp_stride[MAX_MB_PLANE]) {
  MACROBLOCKD *const xd = &dcb->xd;
  if (!xd->left_available) return;

  // Adjust mb_to_right_edge to have the correct value for the OBMC
  // prediction block. This is half the width of the original block,
  // except for 128-wide blocks, where we only use a width of 32.
  const int this_width = xd->width * MI_SIZE;
  const int pred_width = AOMMIN(this_width / 2, 32);
  xd->mb_to_right_edge += GET_MV_SUBPEL(this_width - pred_width);

  struct build_prediction_ctxt ctxt = {
    cm, tmp_buf, tmp_width, tmp_height, tmp_stride, xd->mb_to_bottom_edge, dcb
  };
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART];
  foreach_overlappable_nb_left(cm, xd,
                               max_neighbor_obmc[mi_size_high_log2[bsize]],
                               dec_build_prediction_by_left_pred, &ctxt);

  xd->mb_to_top_edge = -GET_MV_SUBPEL(xd->mi_row * MI_SIZE);
  xd->mb_to_right_edge -= GET_MV_SUBPEL(this_width - pred_width);
  xd->mb_to_bottom_edge = ctxt.mb_to_far_edge;
}

static AOM_INLINE void dec_build_obmc_inter_predictors_sb(
    const AV1_COMMON *cm, DecoderCodingBlock *dcb) {
  const int num_planes = av1_num_planes(cm);
  uint16_t *dst_buf1[MAX_MB_PLANE], *dst_buf2[MAX_MB_PLANE];
  int dst_stride1[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_stride2[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_width1[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_width2[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_height1[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_height2[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };

  MACROBLOCKD *const xd = &dcb->xd;
  av1_setup_obmc_dst_bufs(xd, dst_buf1, dst_buf2);

  dec_build_prediction_by_above_preds(cm, dcb, dst_buf1, dst_width1,
                                      dst_height1, dst_stride1);
  dec_build_prediction_by_left_preds(cm, dcb, dst_buf2, dst_width2, dst_height2,
                                     dst_stride2);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes, &xd->mi[0]->chroma_ref_info);
  av1_build_obmc_inter_prediction(cm, xd, dst_buf1, dst_stride1, dst_buf2,
                                  dst_stride2);
}

static AOM_INLINE void cfl_store_inter_block(AV1_COMMON *const cm,
                                             MACROBLOCKD *const xd) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  if (store_cfl_required(cm, xd) && xd->tree_type == SHARED_PART) {
#if CONFIG_ADAPTIVE_DS_FILTER
    cfl_store_block(xd, mbmi->sb_type[PLANE_TYPE_Y], mbmi->tx_size,
                    cm->seq_params.enable_cfl_ds_filter);
#else
    cfl_store_block(xd, mbmi->sb_type[PLANE_TYPE_Y], mbmi->tx_size);
#endif  // CONFIG_ADAPTIVE_DS_FILTER
  }
}

static AOM_INLINE void predict_inter_block(AV1_COMMON *const cm,
                                           DecoderCodingBlock *dcb,
                                           BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int num_planes = av1_num_planes(cm);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  for (int ref = 0; ref < 1 + has_second_ref(mbmi); ++ref) {
    const MV_REFERENCE_FRAME frame = mbmi->ref_frame[ref];
    if (frame == INTRA_FRAME) {
      assert(is_intrabc_block(mbmi, xd->tree_type));
      assert(ref == 0);
    } else {
      const RefCntBuffer *ref_buf = get_ref_frame_buf(cm, frame);
      const struct scale_factors *ref_scale_factors =
          get_ref_scale_factors_const(cm, frame);

      xd->block_ref_scale_factors[ref] = ref_scale_factors;
      av1_setup_pre_planes(xd, ref, &ref_buf->buf, mi_row, mi_col,
                           ref_scale_factors, num_planes,
                           &mbmi->chroma_ref_info);
    }
  }

  dec_build_inter_predictor(cm, dcb, mi_row, mi_col, bsize);
  if (mbmi->motion_mode == OBMC_CAUSAL) {
    dec_build_obmc_inter_predictors_sb(cm, dcb);
  }

#if CONFIG_MISMATCH_DEBUG
  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end = get_partition_plane_end(xd->tree_type, num_planes);
  for (int plane = plane_start; plane < plane_end; ++plane) {
    const struct macroblockd_plane *pd = &xd->plane[plane];
    int pixel_c, pixel_r;
    if (plane && !xd->is_chroma_ref) continue;
    if (plane) {
      mi_to_pixel_loc(&pixel_c, &pixel_r,
                      mbmi->chroma_ref_info.mi_col_chroma_base,
                      mbmi->chroma_ref_info.mi_row_chroma_base, 0, 0,
                      pd->subsampling_x, pd->subsampling_y);
    } else {
      mi_to_pixel_loc(&pixel_c, &pixel_r, mi_col, mi_row, 0, 0,
                      pd->subsampling_x, pd->subsampling_y);
    }
    mismatch_check_block_pre(pd->dst.buf, pd->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             cm->current_frame.display_order_hint,
#else
                             cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             plane, pixel_c, pixel_r, pd->width, pd->height);
  }
#endif  // CONFIG_MISMATCH_DEBUG

#if CONFIG_INSPECTION
  for (int plane = 0; plane < num_planes; plane++) {
    struct macroblockd_plane *const pd = &xd->plane[plane];
    const int dst_stride = pd->dst.stride;
    const int plane_block_size =
        get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
    const int plane_width = mi_size_wide[plane_block_size];
    const int plane_height = mi_size_high[plane_block_size];
    for (int i = 0; i < plane_height * MI_SIZE; i++) {
      for (int j = 0; j < plane_width * MI_SIZE; j++) {
        uint16_t pixel = pd->dst.buf[i * dst_stride + j];
        int stride = cm->predicted_pixels.strides[plane > 0];
        int pixel_c, pixel_r;
        if (plane) {
          mi_to_pixel_loc(&pixel_c, &pixel_r,
                          mbmi->chroma_ref_info.mi_col_chroma_base,
                          mbmi->chroma_ref_info.mi_row_chroma_base, 0, 0,
                          pd->subsampling_x, pd->subsampling_y);
        } else {
          mi_to_pixel_loc(&pixel_c, &pixel_r, xd->mi_col, xd->mi_row, 0, 0,
                          pd->subsampling_x, pd->subsampling_y);
        }
        pixel_c += j;
        pixel_r += i;
        cm->predicted_pixels.buffers[plane][pixel_r * stride + pixel_c] = pixel;
      }
    }
  }
#endif  // CONFIG_INSPECTION
}

static AOM_INLINE void set_color_index_map_offset(MACROBLOCKD *const xd,
                                                  int plane, aom_reader *r) {
  (void)r;
  Av1ColorMapParam params;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  av1_get_block_dimensions(mbmi->sb_type[plane > 0], plane, xd,
                           &params.plane_width, &params.plane_height, NULL,
                           NULL);
  xd->color_index_map_offset[plane] += params.plane_width * params.plane_height;
}

static AOM_INLINE void decode_token_recon_block(AV1Decoder *const pbi,
                                                ThreadData *const td,
                                                aom_reader *r,
                                                PARTITION_TYPE partition,
                                                BLOCK_SIZE bsize) {
  AV1_COMMON *const cm = &pbi->common;
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  xd->mi[0]->partition = partition;
  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end =
      get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
  if (!is_inter_block(mbmi, xd->tree_type)) {
#if CONFIG_PC_WIENER
    // When row_mt is used, this function can be called with
    // td->read_coeffs_tx_intra_block_visit == decode_block_void.
    // In that case do not reset since it will erase previously set
    // values.
    if (td->read_coeffs_tx_intra_block_visit != decode_block_void)
      av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, bsize, 0,
                              xd->tree_type, &mbmi->chroma_ref_info,
                              plane_start, plane_end);
#endif  // CONFIG_PC_WIENER
    int row, col;

    xd->cfl.use_dc_pred_cache = 0;
    xd->cfl.dc_pred_is_cached[0] = 0;
    xd->cfl.dc_pred_is_cached[1] = 0;
    assert(bsize == get_plane_block_size(bsize, xd->plane[0].subsampling_x,
                                         xd->plane[0].subsampling_y));
    const int max_blocks_wide = max_block_wide(xd, bsize, 0);
    const int max_blocks_high = max_block_high(xd, bsize, 0);
    const BLOCK_SIZE max_unit_bsize = BLOCK_64X64;
    int mu_blocks_wide = mi_size_wide[max_unit_bsize];
    int mu_blocks_high = mi_size_high[max_unit_bsize];
    mu_blocks_wide = AOMMIN(max_blocks_wide, mu_blocks_wide);
    mu_blocks_high = AOMMIN(max_blocks_high, mu_blocks_high);

    for (row = 0; row < max_blocks_high; row += mu_blocks_high) {
      for (col = 0; col < max_blocks_wide; col += mu_blocks_wide) {
        for (int plane = plane_start; plane < plane_end; ++plane) {
          if (plane && !xd->is_chroma_ref) break;
          const struct macroblockd_plane *const pd = &xd->plane[plane];
          const int ss_x = pd->subsampling_x;
          const int ss_y = pd->subsampling_y;
          const BLOCK_SIZE plane_bsize =
              get_mb_plane_block_size(xd, mbmi, plane, ss_x, ss_y);
          const TX_SIZE tx_size = av1_get_tx_size(plane, xd);
          if (plane == AOM_PLANE_U && is_cctx_allowed(cm, xd)) continue;
          const int stepr = tx_size_high_unit[tx_size];
          const int stepc = tx_size_wide_unit[tx_size];
          const int plane_unit_height =
              get_plane_tx_unit_height(xd, plane_bsize, plane, row, ss_y);
          const int plane_unit_width =
              get_plane_tx_unit_width(xd, plane_bsize, plane, col, ss_x);
          for (int blk_row = row >> ss_y; blk_row < plane_unit_height;
               blk_row += stepr) {
            for (int blk_col = col >> ss_x; blk_col < plane_unit_width;
                 blk_col += stepc) {
              if (plane == AOM_PLANE_V && is_cctx_allowed(cm, xd)) {
                td->read_coeffs_tx_intra_block_visit(cm, dcb, r, AOM_PLANE_U,
                                                     blk_row, blk_col, tx_size);
                td->read_coeffs_tx_intra_block_visit(cm, dcb, r, AOM_PLANE_V,
                                                     blk_row, blk_col, tx_size);
                td->inverse_cctx_block_visit(cm, dcb, r, -1, blk_row, blk_col,
                                             tx_size);
                td->predict_and_recon_intra_block_visit(
                    cm, dcb, r, AOM_PLANE_U, blk_row, blk_col, tx_size);
                td->predict_and_recon_intra_block_visit(
                    cm, dcb, r, AOM_PLANE_V, blk_row, blk_col, tx_size);
                set_cb_buffer_offsets(dcb, tx_size, AOM_PLANE_U);
                set_cb_buffer_offsets(dcb, tx_size, AOM_PLANE_V);
              } else {
                assert(plane == AOM_PLANE_Y || !is_cctx_allowed(cm, xd));
                td->read_coeffs_tx_intra_block_visit(cm, dcb, r, plane, blk_row,
                                                     blk_col, tx_size);
                td->predict_and_recon_intra_block_visit(
                    cm, dcb, r, plane, blk_row, blk_col, tx_size);
                set_cb_buffer_offsets(dcb, tx_size, plane);
              }
            }
          }
        }
      }
    }
  } else {
#if CONFIG_PC_WIENER
    // When row_mt is used, this function can be called with
    // td->read_coeffs_tx_inter_block_visit == decode_block_void.
    // In that case do not reset since it will erase previously set
    // values.
    if (td->read_coeffs_tx_inter_block_visit != decode_block_void)
      av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, bsize, 0,
                              xd->tree_type, &mbmi->chroma_ref_info,
                              plane_start, plane_end);
#endif  // CONFIG_PC_WIENER
    td->predict_inter_block_visit(cm, dcb, bsize);
    // Reconstruction
    if (!mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
      int eobtotal = 0;

      const int max_blocks_wide = max_block_wide(xd, bsize, 0);
      const int max_blocks_high = max_block_high(xd, bsize, 0);
      int row, col;

      const BLOCK_SIZE max_unit_bsize = BLOCK_64X64;
      assert(max_unit_bsize ==
             get_plane_block_size(BLOCK_64X64, xd->plane[0].subsampling_x,
                                  xd->plane[0].subsampling_y));
      int mu_blocks_wide = mi_size_wide[max_unit_bsize];
      int mu_blocks_high = mi_size_high[max_unit_bsize];

      mu_blocks_wide = AOMMIN(max_blocks_wide, mu_blocks_wide);
      mu_blocks_high = AOMMIN(max_blocks_high, mu_blocks_high);

      for (row = 0; row < max_blocks_high; row += mu_blocks_high) {
        for (col = 0; col < max_blocks_wide; col += mu_blocks_wide) {
          for (int plane = plane_start; plane < plane_end; ++plane) {
            if (plane && !xd->is_chroma_ref) break;
            const struct macroblockd_plane *const pd = &xd->plane[plane];
            const int ss_x = pd->subsampling_x;
            const int ss_y = pd->subsampling_y;
            const BLOCK_SIZE plane_bsize =
                get_mb_plane_block_size(xd, mbmi, plane, ss_x, ss_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
            assert(plane_bsize == get_plane_block_size(bsize, ss_x, ss_y));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
            const TX_SIZE max_tx_size =
                get_vartx_max_txsize(xd, plane_bsize, plane);
            const int bh_var_tx = tx_size_high_unit[max_tx_size];
            const int bw_var_tx = tx_size_wide_unit[max_tx_size];
#if !CONFIG_NEW_TX_PARTITION
            int block = 0;
            int step =
                tx_size_wide_unit[max_tx_size] * tx_size_high_unit[max_tx_size];
#endif  // !CONFIG_NEW_TX_PARTITION
            const int plane_unit_height =
                get_plane_tx_unit_height(xd, plane_bsize, plane, row, ss_y);
            const int plane_unit_width =
                get_plane_tx_unit_width(xd, plane_bsize, plane, col, ss_x);

            for (int blk_row = row >> ss_y; blk_row < plane_unit_height;
                 blk_row += bh_var_tx) {
              for (int blk_col = col >> ss_x; blk_col < plane_unit_width;
                   blk_col += bw_var_tx) {
                decode_reconstruct_tx(cm, td, r, mbmi, plane, plane_bsize,
                                      blk_row, blk_col,
#if !CONFIG_NEW_TX_PARTITION
                                      block,
#endif  // !CONFIG_NEW_TX_PARTITION
                                      max_tx_size, &eobtotal);
#if !CONFIG_NEW_TX_PARTITION
                block += step;
#endif  // !CONFIG_NEW_TX_PARTITION
              }
            }
          }
        }
      }
    } else if (is_cctx_enabled(cm, xd) && xd->is_chroma_ref &&
               xd->tree_type != LUMA_PART) {
#if CONFIG_PC_WIENER
      av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, bsize, 1,
                              xd->tree_type, &mbmi->chroma_ref_info,
                              plane_start, plane_end);
#endif  // CONFIG_PC_WIENER
        // fill cctx_type_map with CCTX_NONE for skip blocks so their
        // neighbors can derive cctx contexts
      const struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_U];
      const int ss_x = pd->subsampling_x;
      const int ss_y = pd->subsampling_y;
      const BLOCK_SIZE uv_plane_bsize =
          get_mb_plane_block_size(xd, mbmi, AOM_PLANE_U, ss_x, ss_y);
      const TX_SIZE max_tx_size =
          get_vartx_max_txsize(xd, uv_plane_bsize, AOM_PLANE_U);
      const int max_blocks_wide = max_block_wide(xd, bsize, 0);
      const int max_blocks_high = max_block_high(xd, bsize, 0);
      const BLOCK_SIZE max_unit_bsize = BLOCK_64X64;
      int mu_blocks_wide = mi_size_wide[max_unit_bsize];
      int mu_blocks_high = mi_size_high[max_unit_bsize];
      for (int row = 0; row < max_blocks_high; row += mu_blocks_high) {
        for (int col = 0; col < max_blocks_wide; col += mu_blocks_wide) {
          int row_offset, col_offset;
#if CONFIG_EXT_RECUR_PARTITIONS
          get_chroma_mi_offsets(xd, &row_offset, &col_offset);
#else
          get_chroma_mi_offsets(xd, max_tx_size, &row_offset, &col_offset);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
          update_cctx_array(xd, 0, 0, row_offset, col_offset, max_tx_size,
                            CCTX_NONE);
        }
      }
    }
#if CONFIG_PC_WIENER
    else {
      av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, bsize, 1,
                              xd->tree_type, &mbmi->chroma_ref_info,
                              plane_start, plane_end);
    }
#endif  // CONFIG_PC_WIENER
    td->cfl_store_inter_block_visit(cm, xd);
  }

  av1_visit_palette(pbi, xd, r, set_color_index_map_offset);
  av1_mark_block_as_coded(xd, bsize, cm->sb_size);
}

static AOM_INLINE void set_inter_tx_size(MB_MODE_INFO *mbmi, int stride_log2,
                                         int tx_w_log2, int tx_h_log2,
                                         int min_txs, int split_size, int txs,
                                         int blk_row, int blk_col) {
  for (int idy = 0; idy < tx_size_high_unit[split_size];
       idy += tx_size_high_unit[min_txs]) {
    for (int idx = 0; idx < tx_size_wide_unit[split_size];
         idx += tx_size_wide_unit[min_txs]) {
      const int index = (((blk_row + idy) >> tx_h_log2) << stride_log2) +
                        ((blk_col + idx) >> tx_w_log2);
      mbmi->inter_tx_size[index] = txs;
    }
  }
}

#if CONFIG_NEW_TX_PARTITION
static TX_SIZE read_tx_partition(MACROBLOCKD *xd, MB_MODE_INFO *mbmi,
                                 TX_SIZE max_tx_size, int blk_row, int blk_col,
                                 aom_reader *r) {
  int plane_type = (xd->tree_type == CHROMA_PART);
  const BLOCK_SIZE bsize = mbmi->sb_type[plane_type];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int max_blocks_high = max_block_high(xd, bsize, 0);
  const int max_blocks_wide = max_block_wide(xd, bsize, 0);
  if (is_inter && (blk_row >= max_blocks_high || blk_col >= max_blocks_wide))
    return TX_INVALID;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
#if !CONFIG_TX_PARTITION_CTX
  const int is_rect = is_rect_tx(max_tx_size);
#endif  // !CONFIG_TX_PARTITION_CTX
  const int allow_horz = allow_tx_horz_split(max_tx_size);
  const int allow_vert = allow_tx_vert_split(max_tx_size);
  TX_PARTITION_TYPE partition = 0;
#if CONFIG_TX_PARTITION_CTX
  const int bsize_group = size_to_tx_part_group_lookup[bsize];
  int do_partition = 0;
  if (allow_horz || allow_vert) {
    aom_cdf_prob *do_partition_cdf =
        ec_ctx->txfm_do_partition_cdf[is_inter][bsize_group];
    do_partition =
        aom_read_symbol(r, do_partition_cdf, 2, ACCT_INFO("do_partition"));
  }

  if (do_partition) {
    if (allow_horz && allow_vert) {
      // Read 4way tree type
      assert(bsize_group > 0);
      aom_cdf_prob *partition_type_cdf =
          ec_ctx->txfm_4way_partition_type_cdf[is_inter][bsize_group - 1];
      const TX_PARTITION_TYPE partition_type = aom_read_symbol(
          r, partition_type_cdf, 3, ACCT_INFO("partition_type"));
      partition = partition_type + 1;
    } else {
      /*
       If only one split type (horizontal or vertical) is allowed for this
       block, then derive parition type based on the allowed split type
       (horizontal or vertical).
       */
      partition = allow_horz ? TX_PARTITION_HORZ : TX_PARTITION_VERT;
    }
  } else {
    partition = TX_PARTITION_NONE;
  }
#else
  /*
  If both horizontal and vertical splits are allowed for this block,
  first signal using a 4 way tree to indicate TX_PARTITION_NONE,
  TX_PARTITION_SPLIT, TX_PARTITION_HORZ or TX_PARTITION_VERT. If the
  actual tx partition type is HORZ4 or VERT4, we read an additional
  bit to indicate to split further.
  */
  if (allow_horz && allow_vert) {
    // Read 4way tree type
    const int split4_ctx =
        is_inter ? txfm_partition_split4_inter_context(
                       xd->above_txfm_context + blk_col,
                       xd->left_txfm_context + blk_row, bsize, max_tx_size)
                 : get_tx_size_context(xd);
    aom_cdf_prob *split4_cdf =
        is_inter ? ec_ctx->inter_4way_txfm_partition_cdf[is_rect][split4_ctx]
                 : ec_ctx->intra_4way_txfm_partition_cdf[is_rect][split4_ctx];
    const TX_PARTITION_TYPE split4_partition =
        aom_read_symbol(r, split4_cdf, 4, ACCT_INFO("split4_partition"));
    partition = split4_partition;

    /*
    If only one split type (horizontal or vertical) is allowed for this block,
    first signal a bit indicating whether there is any split at all. If
    the partition has a split, and this block is able to be split further,
    we send a second bit to indicate if the type should be HORZ4 or VERT4.
    */
  } else if (allow_horz || allow_vert) {
    // Read bit to indicate if there is any split at all
    aom_cdf_prob *split2_cdf = is_inter ? ec_ctx->inter_2way_txfm_partition_cdf
                                        : ec_ctx->intra_2way_txfm_partition_cdf;
    const int has_first_split =
        aom_read_symbol(r, split2_cdf, 2, ACCT_INFO("has_first_split"));
    partition = has_first_split
                    ? (allow_horz ? TX_PARTITION_HORZ : TX_PARTITION_VERT)
                    : TX_PARTITION_NONE;
  } else {
    assert(!allow_horz && !allow_vert);
    partition = TX_PARTITION_NONE;
  }
#endif  // CONFIG_TX_PARTITION_CTX
  TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
  get_tx_partition_sizes(partition, max_tx_size, sub_txs);
  // TODO(sarahparker) This assumes all of the tx sizes in the partition
  // scheme are the same size. This will need to be adjusted to deal with the
  // case where they can be different.
  mbmi->tx_size = sub_txs[0];
  const int index =
      is_inter ? av1_get_txb_size_index(bsize, blk_row, blk_col) : 0;
  mbmi->tx_partition_type[index] = partition;
  if (is_inter) {
    const TX_SIZE txs = sub_tx_size_map[max_txsize_rect_lookup[bsize]];
    const int tx_w_log2 = tx_size_wide_log2[txs] - MI_SIZE_LOG2;
    const int tx_h_log2 = tx_size_high_log2[txs] - MI_SIZE_LOG2;
    const int bw_log2 = mi_size_wide_log2[bsize];
    const int stride_log2 = bw_log2 - tx_w_log2;
    set_inter_tx_size(mbmi, stride_log2, tx_w_log2, tx_h_log2, txs, max_tx_size,
                      mbmi->tx_size, blk_row, blk_col);
#if !CONFIG_TX_PARTITION_CTX
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, mbmi->tx_size,
                          max_tx_size);
#endif  // !CONFIG_TX_PARTITION_CTX
  }
  return sub_txs[0];
}
#else
static AOM_INLINE void read_tx_size_vartx(MACROBLOCKD *xd, MB_MODE_INFO *mbmi,
                                          TX_SIZE tx_size, int depth,
#if CONFIG_LPF_MASK
                                          AV1_COMMON *cm, int mi_row,
                                          int mi_col, int store_bitmask,
#endif
                                          int blk_row, int blk_col,
                                          aom_reader *r) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  int is_split = 0;
  int plane_type = (xd->tree_type == CHROMA_PART);
  const BLOCK_SIZE bsize = mbmi->sb_type[plane_type];
  const int max_blocks_high = max_block_high(xd, bsize, 0);
  const int max_blocks_wide = max_block_wide(xd, bsize, 0);
  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
  assert(tx_size > TX_4X4);
  TX_SIZE txs = max_txsize_rect_lookup[bsize];
  for (int level = 0; level < MAX_VARTX_DEPTH - 1; ++level)
    txs = sub_tx_size_map[txs];
  const int tx_w_log2 = tx_size_wide_log2[txs] - MI_SIZE_LOG2;
  const int tx_h_log2 = tx_size_high_log2[txs] - MI_SIZE_LOG2;
  const int bw_log2 = mi_size_wide_log2[bsize];
  const int stride_log2 = bw_log2 - tx_w_log2;

  if (depth == MAX_VARTX_DEPTH) {
    set_inter_tx_size(mbmi, stride_log2, tx_w_log2, tx_h_log2, txs, tx_size,
                      tx_size, blk_row, blk_col);
    mbmi->tx_size = tx_size;
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);
    return;
  }
  const int ctx = txfm_partition_context(xd->above_txfm_context + blk_col,
                                         xd->left_txfm_context + blk_row,
                                         mbmi->sb_type[plane_type], tx_size);
  is_split = aom_read_symbol(r, ec_ctx->txfm_partition_cdf[ctx], 2,
                             ACCT_INFO("is_split"));

  if (is_split) {
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];

    if (sub_txs == TX_4X4) {
      set_inter_tx_size(mbmi, stride_log2, tx_w_log2, tx_h_log2, txs, tx_size,
                        sub_txs, blk_row, blk_col);
      mbmi->tx_size = sub_txs;
      txfm_partition_update(xd->above_txfm_context + blk_col,
                            xd->left_txfm_context + blk_row, sub_txs, tx_size);
#if CONFIG_LPF_MASK
      if (store_bitmask) {
        av1_store_bitmask_vartx(cm, mi_row + blk_row, mi_col + blk_col,
                                txsize_to_bsize[tx_size], TX_4X4, mbmi);
      }
#endif
      return;
    }
#if CONFIG_LPF_MASK
    if (depth + 1 == MAX_VARTX_DEPTH && store_bitmask) {
      av1_store_bitmask_vartx(cm, mi_row + blk_row, mi_col + blk_col,
                              txsize_to_bsize[tx_size], sub_txs, mbmi);
      store_bitmask = 0;
    }
#endif

    assert(bsw > 0 && bsh > 0);
    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh) {
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        int offsetr = blk_row + row;
        int offsetc = blk_col + col;
        read_tx_size_vartx(xd, mbmi, sub_txs, depth + 1,
#if CONFIG_LPF_MASK
                           cm, mi_row, mi_col, store_bitmask,
#endif
                           offsetr, offsetc, r);
      }
    }
  } else {
    set_inter_tx_size(mbmi, stride_log2, tx_w_log2, tx_h_log2, txs, tx_size,
                      tx_size, blk_row, blk_col);
    mbmi->tx_size = tx_size;
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);
#if CONFIG_LPF_MASK
    if (store_bitmask) {
      av1_store_bitmask_vartx(cm, mi_row + blk_row, mi_col + blk_col,
                              txsize_to_bsize[tx_size], tx_size, mbmi);
    }
#endif
  }
}

static TX_SIZE read_selected_tx_size(const MACROBLOCKD *const xd,
                                     aom_reader *r) {
  // TODO(debargha): Clean up the logic here. This function should only
  // be called for intra.
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART];
  const int32_t tx_size_cat = bsize_to_tx_size_cat(bsize);
  const int max_depths = bsize_to_max_depth(bsize);
  const int ctx = get_tx_size_context(xd);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  const int depth = aom_read_symbol(r, ec_ctx->tx_size_cdf[tx_size_cat][ctx],
                                    max_depths + 1, ACCT_INFO("depth"));
  assert(depth >= 0 && depth <= max_depths);
  const TX_SIZE tx_size = depth_to_tx_size(depth, bsize);
  return tx_size;
}
#endif  // CONFIG_NEW_TX_PARTITION

static TX_SIZE read_tx_size(MACROBLOCKD *xd, TX_MODE tx_mode, int is_inter,
                            int allow_select_inter, aom_reader *r) {
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART];
  if (xd->lossless[xd->mi[0]->segment_id]) return TX_4X4;

  if (block_signals_txsize(bsize)) {
    if ((!is_inter || allow_select_inter) && tx_mode == TX_MODE_SELECT) {
#if CONFIG_NEW_TX_PARTITION
      MB_MODE_INFO *mbmi = xd->mi[0];
      const TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
      return read_tx_partition(xd, mbmi, max_tx_size, 0, 0, r);
#else
      const TX_SIZE coded_tx_size = read_selected_tx_size(xd, r);
      return coded_tx_size;
#endif  // CONFIG_NEW_TX_PARTITION
    } else {
      return tx_size_from_tx_mode(bsize, tx_mode);
    }
  } else {
    assert(IMPLIES(tx_mode == ONLY_4X4, bsize == BLOCK_4X4));
    return max_txsize_rect_lookup[bsize];
  }
}

static AOM_INLINE void parse_decode_block(AV1Decoder *const pbi,
                                          ThreadData *const td, int mi_row,
                                          int mi_col, aom_reader *r,
                                          PARTITION_TYPE partition,
                                          BLOCK_SIZE bsize,
                                          PARTITION_TREE *parent, int index) {
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  decode_mbmi_block(pbi, dcb, mi_row, mi_col, r, partition, bsize, parent,
                    index);

  av1_visit_palette(pbi, xd, r, av1_decode_palette_tokens);

  AV1_COMMON *cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);
  MB_MODE_INFO *mbmi = xd->mi[0];
  int inter_block_tx = is_inter_block(mbmi, xd->tree_type) ||
                       is_intrabc_block(mbmi, xd->tree_type);
  if (xd->tree_type != CHROMA_PART) {
    if (cm->features.tx_mode == TX_MODE_SELECT && block_signals_txsize(bsize) &&
        !mbmi->skip_txfm[xd->tree_type == CHROMA_PART] && inter_block_tx &&
        !xd->lossless[mbmi->segment_id]) {
      const TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
      const int bh = tx_size_high_unit[max_tx_size];
      const int bw = tx_size_wide_unit[max_tx_size];
      const int width = mi_size_wide[bsize];
      const int height = mi_size_high[bsize];

      for (int idy = 0; idy < height; idy += bh)
        for (int idx = 0; idx < width; idx += bw)
#if CONFIG_NEW_TX_PARTITION
          read_tx_partition(xd, mbmi, max_tx_size, idy, idx, r);
#else
          read_tx_size_vartx(xd, mbmi, max_tx_size, 0,
#if CONFIG_LPF_MASK
                             cm, mi_row, mi_col, 1,
#endif
                             idy, idx, r);
#endif  // CONFIG_NEW_TX_PARTITION
    } else {
      mbmi->tx_size =
          read_tx_size(xd, cm->features.tx_mode, inter_block_tx,
                       !mbmi->skip_txfm[xd->tree_type == CHROMA_PART], r);
      if (inter_block_tx)
        memset(mbmi->inter_tx_size, mbmi->tx_size, sizeof(mbmi->inter_tx_size));
#if !CONFIG_TX_PARTITION_CTX
      set_txfm_ctxs(mbmi->tx_size, xd->width, xd->height,
                    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
                        is_inter_block(mbmi, xd->tree_type),
                    xd);
#endif  // !CONFIG_TX_PARTITION_CTX
#if CONFIG_LPF_MASK
      const int w = mi_size_wide[bsize];
      const int h = mi_size_high[bsize];
      if (w <= mi_size_wide[BLOCK_64X64] && h <= mi_size_high[BLOCK_64X64]) {
        av1_store_bitmask_univariant_tx(cm, mi_row, mi_col, bsize, mbmi);
      } else {
        for (int row = 0; row < h; row += mi_size_high[BLOCK_64X64]) {
          for (int col = 0; col < w; col += mi_size_wide[BLOCK_64X64]) {
            av1_store_bitmask_univariant_tx(cm, mi_row + row, mi_col + col,
                                            BLOCK_64X64, mbmi);
          }
        }
      }
#endif
    }
  }
#if CONFIG_LPF_MASK
  const int w = mi_size_wide[bsize];
  const int h = mi_size_high[bsize];
  if (w <= mi_size_wide[BLOCK_64X64] && h <= mi_size_high[BLOCK_64X64]) {
    av1_store_bitmask_other_info(cm, mi_row, mi_col, bsize, mbmi, 1, 1);
  } else {
    for (int row = 0; row < h; row += mi_size_high[BLOCK_64X64]) {
      for (int col = 0; col < w; col += mi_size_wide[BLOCK_64X64]) {
        av1_store_bitmask_other_info(cm, mi_row + row, mi_col + col,
                                     BLOCK_64X64, mbmi, row == 0, col == 0);
      }
    }
  }
#endif

  if (cm->delta_q_info.delta_q_present_flag) {
    for (int i = 0; i < MAX_SEGMENTS; i++) {
      const int current_qindex = av1_get_qindex(
          &cm->seg, i, xd->current_base_qindex, cm->seq_params.bit_depth);

      const CommonQuantParams *const quant_params = &cm->quant_params;
      for (int j = 0; j < num_planes; ++j) {
        const int dc_delta_q = j == 0 ? quant_params->y_dc_delta_q
                                      : (j == 1 ? quant_params->u_dc_delta_q
                                                : quant_params->v_dc_delta_q);
        const int ac_delta_q = j == 0 ? 0
                                      : (j == 1 ? quant_params->u_ac_delta_q
                                                : quant_params->v_ac_delta_q);
        xd->plane[j].seg_dequant_QTX[i][0] =
            av1_dc_quant_QTX(current_qindex, dc_delta_q,
                             j == 0 ? cm->seq_params.base_y_dc_delta_q
                                    : cm->seq_params.base_uv_dc_delta_q,
                             cm->seq_params.bit_depth);
        xd->plane[j].seg_dequant_QTX[i][1] = av1_ac_quant_QTX(
            current_qindex, ac_delta_q, cm->seq_params.bit_depth);
      }
    }
  }
  assert(bsize == mbmi->sb_type[av1_get_sdp_idx(xd->tree_type)]);
  if (mbmi->skip_txfm[xd->tree_type == CHROMA_PART])
    av1_reset_entropy_context(xd, bsize, num_planes);
  decode_token_recon_block(pbi, td, r, partition, bsize);

#if CONFIG_REFINED_MVS_IN_TMVP
  if (!frame_is_intra_only(cm) &&
      cm->seq_params.order_hint_info.enable_ref_frame_mvs) {
    MB_MODE_INFO *const mi = xd->mi[0];
    if (opfl_allowed_for_cur_block(cm, mi)) {
      const int bw = mi_size_wide[bsize];
      const int bh = mi_size_high[bsize];
      const int x_inside_boundary = AOMMIN(bw, cm->mi_params.mi_cols - mi_col);
      const int y_inside_boundary = AOMMIN(bh, cm->mi_params.mi_rows - mi_row);
      av1_copy_frame_refined_mvs(cm, xd, mi, xd->mi_row, xd->mi_col,
                                 x_inside_boundary, y_inside_boundary);
    }
  }
#endif  // CONFIG_REFINED_MVS_IN_TMVP

  if (xd->tree_type != SHARED_PART) {
    const int bh = mi_size_high[bsize];
    const int bw = mi_size_wide[bsize];
    const CommonModeInfoParams *const mi_params = &cm->mi_params;
    const int x_inside_boundary = AOMMIN(bw, mi_params->mi_cols - mi_col);
    const int y_inside_boundary = AOMMIN(bh, mi_params->mi_rows - mi_row);
    int idx = mi_params->mi_stride;
    assert(x_inside_boundary && y_inside_boundary);
    if (xd->tree_type != CHROMA_PART) {
      for (int y = 0; y < y_inside_boundary; ++y) {
        for (int x = 0; x < x_inside_boundary; ++x) {
          if (x == 0 && y == 0) continue;
          set_blk_offsets(mi_params, xd, mi_row, mi_col, y, x);
          *(xd->mi[y * idx + x]) = *(xd->mi[0]);
        }
      }
    } else {
      assert(x_inside_boundary && y_inside_boundary);
      for (int y = 0; y < y_inside_boundary; ++y) {
        for (int x = 0; x < x_inside_boundary; ++x) {
          if (x == 0 && y == 0) continue;
          set_blk_offsets(mi_params, xd, mi_row, mi_col, y, x);
          xd->mi[y * idx + x]->sb_type[PLANE_TYPE_UV] =
              xd->mi[0]->sb_type[PLANE_TYPE_UV];
          xd->mi[y * idx + x]->uv_mode = xd->mi[0]->uv_mode;
          xd->mi[y * idx + x]->angle_delta[PLANE_TYPE_UV] =
              xd->mi[0]->angle_delta[PLANE_TYPE_UV];
          if (av1_allow_palette(cm->features.allow_screen_content_tools,
                                bsize)) {
            xd->mi[y * idx + x]->palette_mode_info.palette_size[PLANE_TYPE_UV] =
                xd->mi[0]->palette_mode_info.palette_size[PLANE_TYPE_UV];
            for (int i = PALETTE_MAX_SIZE; i < 3 * PALETTE_MAX_SIZE; i++)
              xd->mi[y * idx + x]->palette_mode_info.palette_colors[i] =
                  xd->mi[0]->palette_mode_info.palette_colors[i];
          }
        }
      }
    }
  }
}

static AOM_INLINE void set_offsets_for_pred_and_recon(
    AV1Decoder *const pbi, ThreadData *const td, int mi_row, int mi_col,
    BLOCK_SIZE bsize, PARTITION_TREE *parent, int index) {
  AV1_COMMON *const cm = &pbi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int num_planes = av1_num_planes(cm);

  const int offset = mi_row * mi_params->mi_stride + mi_col;
  const TileInfo *const tile = &xd->tile;

  xd->mi = mi_params->mi_grid_base + offset;
  xd->tx_type_map =
      &mi_params->tx_type_map[mi_row * mi_params->mi_stride + mi_col];
  xd->tx_type_map_stride = mi_params->mi_stride;
  xd->cctx_type_map =
      &mi_params->cctx_type_map[mi_row * mi_params->mi_stride + mi_col];
  xd->cctx_type_map_stride = mi_params->mi_stride;

  CHROMA_REF_INFO *chroma_ref_info = &xd->mi[0]->chroma_ref_info;
  set_chroma_ref_info(xd->tree_type, mi_row, mi_col, index, bsize,
                      chroma_ref_info, parent ? &parent->chroma_ref_info : NULL,
                      parent ? parent->bsize : BLOCK_INVALID,
                      parent ? parent->partition : PARTITION_NONE,
                      xd->plane[1].subsampling_x, xd->plane[1].subsampling_y);
  set_plane_n4(xd, bw, bh, num_planes, chroma_ref_info);

  // Distance of Mb to the various image edges. These are specified to 8th pel
  // as they are always compared to values that are in 1/8th pel units
  set_mi_row_col(xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows,
                 mi_params->mi_cols, chroma_ref_info);

  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes, chroma_ref_info);
}

static AOM_INLINE void decode_block(AV1Decoder *const pbi, ThreadData *const td,
                                    int mi_row, int mi_col, aom_reader *r,
                                    PARTITION_TYPE partition, BLOCK_SIZE bsize,
                                    PARTITION_TREE *parent, int index) {
  (void)partition;
  set_offsets_for_pred_and_recon(pbi, td, mi_row, mi_col, bsize, parent, index);
  decode_token_recon_block(pbi, td, r, partition, bsize);
}

#if CONFIG_EXT_RECUR_PARTITIONS
/*!\brief Maps (ext_part, 4way, 4way_type, rect_type) to partition_type. */
static PARTITION_TYPE
    rect_part_table[2][2][NUM_UNEVEN_4WAY_PARTS][NUM_RECT_PARTS] = {
      {
          // !do_ext_partition
          {
              // !do_4way
              { // UNEVEN_4A
                PARTITION_HORZ, PARTITION_VERT },
              { // UNEVEN_4B
                PARTITION_HORZ, PARTITION_VERT },
          },
          {
              // do_4way
              { // UNEVEN_4A
                PARTITION_HORZ, PARTITION_VERT },
              { // UNEVEN_4B
                PARTITION_HORZ, PARTITION_VERT },
          },
      },
      {
          // do_ext_partition
          {
              // !do_4way
              { // UNEVEN_4A
                PARTITION_HORZ_3, PARTITION_VERT_3 },
              { // UNEVEN_4B
                PARTITION_HORZ_3, PARTITION_VERT_3 },
          },
          {
              // do_4way
              { // UNEVEN_4A
                PARTITION_HORZ_4A, PARTITION_VERT_4A },
              { // UNEVEN_4B
                PARTITION_HORZ_4B, PARTITION_VERT_4B },
          },
      },
    };
#endif  // CONFIG_EXT_RECUR_PARTITIONS

static PARTITION_TYPE read_partition(const AV1_COMMON *const cm,
                                     MACROBLOCKD *xd, int mi_row, int mi_col,
                                     aom_reader *r, int has_rows, int has_cols,
#if CONFIG_EXT_RECUR_PARTITIONS
                                     const PARTITION_TREE *ptree,
                                     const PARTITION_TREE *ptree_luma,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                                     BLOCK_SIZE bsize) {
  const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize);
  assert(ctx >= 0);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

#if CONFIG_EXT_RECUR_PARTITIONS
  (void)has_rows;
  (void)has_cols;
  const int plane = xd->tree_type == CHROMA_PART;
  const int ssx = cm->seq_params.subsampling_x;
  const int ssy = cm->seq_params.subsampling_y;
  const PARTITION_TYPE derived_partition =
      av1_get_normative_forced_partition_type(
          &cm->mi_params, xd->tree_type, ssx, ssy, mi_row, mi_col, bsize,
          ptree_luma, &ptree->chroma_ref_info);
  if (derived_partition != PARTITION_INVALID) {
    return derived_partition;
  }

  const bool do_split = aom_read_symbol(r, ec_ctx->do_split_cdf[plane][ctx], 2,
                                        ACCT_INFO("do_split"));
  if (!do_split) {
    return PARTITION_NONE;
  }
#if CONFIG_BLOCK_256
  const int square_split_ctx = square_split_context(xd, mi_row, mi_col, bsize);
  if (is_square_split_eligible(bsize, cm->sb_size)) {
    const bool do_square_split =
        aom_read_symbol(r, ec_ctx->do_square_split_cdf[plane][square_split_ctx],
                        2, ACCT_INFO("do_square_split"));
    if (do_square_split) {
      return PARTITION_SPLIT;
    }
  }
#endif  // CONFIG_BLOCK_256

  RECT_PART_TYPE rect_type = rect_type_implied_by_bsize(bsize, xd->tree_type);
  if (rect_type == RECT_INVALID) {
    rect_type = aom_read_symbol(r, ec_ctx->rect_type_cdf[plane][ctx],
                                NUM_RECT_PARTS, ACCT_INFO("rect_type"));
  }

  bool do_ext_partition = false;
  bool do_uneven_4way_partition = false;
  UNEVEN_4WAY_PART_TYPE uneven_4way_partition_type = UNEVEN_4A;

  const bool ext_partition_allowed =
      cm->seq_params.enable_ext_partitions &&
      is_ext_partition_allowed(bsize, rect_type, xd->tree_type);
  if (ext_partition_allowed) {
    do_ext_partition =
        aom_read_symbol(r, ec_ctx->do_ext_partition_cdf[plane][rect_type][ctx],
                        2, ACCT_INFO("do_ext_partition"));
    if (do_ext_partition) {
      const bool uneven_4way_partition_allowed =
          is_uneven_4way_partition_allowed(bsize, rect_type, xd->tree_type);
      if (uneven_4way_partition_allowed) {
        do_uneven_4way_partition = aom_read_symbol(
            r, ec_ctx->do_uneven_4way_partition_cdf[plane][rect_type][ctx], 2,
            ACCT_INFO("do_uneven_4way_partition"));
        if (do_uneven_4way_partition) {
          uneven_4way_partition_type = aom_read_symbol(
              r, ec_ctx->uneven_4way_partition_type_cdf[plane][rect_type][ctx],
              NUM_UNEVEN_4WAY_PARTS, ACCT_INFO("uneven_4way_partition_type"));
        }
      }
    }
  }
  return rect_part_table[do_ext_partition][do_uneven_4way_partition]
                        [uneven_4way_partition_type][rect_type];
#else   // !CONFIG_EXT_RECUR_PARTITIONS
  if (!has_rows && !has_cols) return PARTITION_SPLIT;

  const int plane = xd->tree_type == CHROMA_PART;
  if (plane == 1 && bsize == BLOCK_8X8) {
    return PARTITION_NONE;
  }
  int parent_block_width = block_size_wide[bsize];
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  if (plane && parent_block_width >= SHARED_PART_SIZE) {
    int luma_split_flag = get_luma_split_flag(bsize, mi_params, mi_row, mi_col);
    // if luma blocks uses smaller blocks, then chroma will also split
    if (luma_split_flag > 3) return PARTITION_SPLIT;
  }

  assert(ctx >= 0);
  aom_cdf_prob *partition_cdf = ec_ctx->partition_cdf[plane][ctx];
  if (has_rows && has_cols) {
    return (PARTITION_TYPE)aom_read_symbol(r, partition_cdf,
                                           partition_cdf_length(bsize),
                                           ACCT_INFO("partition_cdf"));
  } else if (!has_rows && has_cols) {
    assert(bsize > BLOCK_8X8);
    aom_cdf_prob cdf[2];
    partition_gather_vert_alike(cdf, partition_cdf, bsize);
    assert(cdf[1] == AOM_ICDF(CDF_PROB_TOP));
    return aom_read_cdf(r, cdf, 2, ACCT_INFO("partition_cdf")) ? PARTITION_SPLIT
                                                               : PARTITION_HORZ;
  } else {
    assert(has_rows && !has_cols);
    assert(bsize > BLOCK_8X8);
    aom_cdf_prob cdf[2];
    partition_gather_horz_alike(cdf, partition_cdf, bsize);
    assert(cdf[1] == AOM_ICDF(CDF_PROB_TOP));
    return aom_read_cdf(r, cdf, 2, ACCT_INFO("partition_cdf")) ? PARTITION_SPLIT
                                                               : PARTITION_VERT;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
}
#if CONFIG_FLEX_MVRES
// Set the superblock level parameters
static void set_sb_mv_precision(SB_INFO *sbi, AV1Decoder *const pbi) {
  AV1_COMMON *const cm = &pbi->common;
  sbi->sb_mv_precision = cm->features.fr_mv_precision;
}
#endif

// TODO(slavarnway): eliminate bsize and subsize in future commits
static AOM_INLINE void decode_partition(AV1Decoder *const pbi,
                                        ThreadData *const td, int mi_row,
                                        int mi_col, aom_reader *reader,
                                        BLOCK_SIZE bsize,
#if CONFIG_FLEX_MVRES
                                        SB_INFO *sbi,
#endif
                                        PARTITION_TREE *ptree,
#if CONFIG_EXT_RECUR_PARTITIONS
                                        const PARTITION_TREE *ptree_luma,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                                        int parse_decode_flag) {
  assert(bsize < BLOCK_SIZES_ALL);
  AV1_COMMON *const cm = &pbi->common;
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;
  // Half block width/height.
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
#if CONFIG_EXT_RECUR_PARTITIONS
  // One-eighth block width/height.
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
#else
  // Quarter block width/height.
  const int qbs_w = mi_size_wide[bsize] / 4;
  const int qbs_h = mi_size_high[bsize] / 4;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  PARTITION_TYPE partition;
  const int has_rows = (mi_row + hbs_h) < cm->mi_params.mi_rows;
  const int has_cols = (mi_col + hbs_w) < cm->mi_params.mi_cols;

  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols)
    return;

  // parse_decode_flag takes the following values :
  // 01 - do parse only
  // 10 - do decode only
  // 11 - do parse and decode
  static const block_visitor_fn_t block_visit[4] = { NULL, parse_decode_block,
                                                     decode_block,
                                                     parse_decode_block };
#if CONFIG_FLEX_MVRES
  const int is_sb_root = bsize == cm->sb_size;
#endif

  if (parse_decode_flag & 1) {
#if CONFIG_FLEX_MVRES
    if (is_sb_root) {
      set_sb_mv_precision(sbi, pbi);
    }
#endif
    const int plane_start = get_partition_plane_start(xd->tree_type);
    const int plane_end =
        get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
    for (int plane = plane_start; plane < plane_end; ++plane) {
      int rcol0, rcol1, rrow0, rrow1;
      if ((cm->rst_info[plane].frame_restoration_type != RESTORE_NONE
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
           || cm->rst_info[plane].frame_cross_restoration_type != RESTORE_NONE
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
           ) &&
          av1_loop_restoration_corners_in_sb(cm, plane, mi_row, mi_col, bsize,
                                             &rcol0, &rcol1, &rrow0, &rrow1)) {
        const int rstride = cm->rst_info[plane].horz_units_per_tile;
        for (int rrow = rrow0; rrow < rrow1; ++rrow) {
          for (int rcol = rcol0; rcol < rcol1; ++rcol) {
            const int runit_idx = rcol + rrow * rstride;
            loop_restoration_read_sb_coeffs(cm, xd, reader, plane, runit_idx);
          }
        }
      }
    }

    ptree->bsize = bsize;
    ptree->mi_row = mi_row;
    ptree->mi_col = mi_col;
    ptree->is_settled = 1;
    PARTITION_TREE *parent = ptree->parent;
    set_chroma_ref_info(
        xd->tree_type, mi_row, mi_col, ptree->index, bsize,
        &ptree->chroma_ref_info, parent ? &parent->chroma_ref_info : NULL,
        parent ? parent->bsize : BLOCK_INVALID,
        parent ? parent->partition : PARTITION_NONE, ss_x, ss_y);

    partition =
        !is_partition_point(bsize)
            ? PARTITION_NONE
            : read_partition(cm, xd, mi_row, mi_col, reader, has_rows, has_cols,
#if CONFIG_EXT_RECUR_PARTITIONS
                             ptree, ptree_luma,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                             bsize);

#if CONFIG_EXT_RECUR_PARTITIONS
    if (!is_luma_chroma_share_same_partition(xd->tree_type, ptree_luma,
                                             bsize)) {
      ptree_luma = NULL;
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

    ptree->partition = partition;

    switch (partition) {
#if CONFIG_EXT_RECUR_PARTITIONS
      case PARTITION_HORZ_4A:
      case PARTITION_HORZ_4B:
      case PARTITION_VERT_4A:
      case PARTITION_VERT_4B:
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      case PARTITION_SPLIT:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
        ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
        ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
        break;
#if CONFIG_EXT_RECUR_PARTITIONS
      case PARTITION_HORZ:
      case PARTITION_VERT:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
        break;
      case PARTITION_HORZ_3:
      case PARTITION_VERT_3:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
        ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
        ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
        break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      default: break;
    }
  } else {
    partition = ptree->partition;
  }

  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
  if (subsize == BLOCK_INVALID) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Partition %d is invalid for block size %dx%d",
                       partition, block_size_wide[bsize],
                       block_size_high[bsize]);
    assert(0);
  }
  // Check the bitstream is conformant: if there is subsampling on the
  // chroma planes, subsize must subsample to a valid block size.
  const struct macroblockd_plane *const pd_u = &xd->plane[1];
#if CONFIG_EXT_RECUR_PARTITIONS
  BLOCK_SIZE test_subsize = subsize;
  if (xd->tree_type == SHARED_PART) {
    const PARTITION_TREE *parent = ptree;
    CHROMA_REF_INFO chroma_ref_info;
    const int index =
        (partition == PARTITION_HORZ || partition == PARTITION_VERT) +
        (partition == PARTITION_HORZ_3 || partition == PARTITION_VERT_3);
    set_chroma_ref_info(xd->tree_type, mi_row, mi_col, index, bsize,
                        &chroma_ref_info,
                        parent ? &parent->chroma_ref_info : NULL,
                        parent ? parent->bsize : BLOCK_INVALID,
                        parent ? parent->partition : PARTITION_NONE,
                        xd->plane[1].subsampling_x, xd->plane[1].subsampling_y);
    test_subsize = chroma_ref_info.bsize_base;
    assert(test_subsize != BLOCK_INVALID);
  }
  if (xd->tree_type != LUMA_PART &&
      get_plane_block_size(test_subsize, pd_u->subsampling_x,
                           pd_u->subsampling_y) == BLOCK_INVALID) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Block size %dx%d invalid with this subsampling mode",
                       block_size_wide[test_subsize],
                       block_size_high[test_subsize]);
  }
#else
  if (get_plane_block_size(subsize, pd_u->subsampling_x, pd_u->subsampling_y) ==
      BLOCK_INVALID) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Block size %dx%d invalid with this subsampling mode",
                       block_size_wide[subsize], block_size_high[subsize]);
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#define DEC_BLOCK_STX_ARG
#define DEC_BLOCK_EPT_ARG partition,
#define DEC_BLOCK(db_r, db_c, db_subsize, index)                               \
  block_visit[parse_decode_flag](pbi, td, DEC_BLOCK_STX_ARG(db_r), (db_c),     \
                                 reader, DEC_BLOCK_EPT_ARG(db_subsize), ptree, \
                                 index)
#if CONFIG_FLEX_MVRES && CONFIG_EXT_RECUR_PARTITIONS
#define DEC_PARTITION(db_r, db_c, db_subsize, index)                 \
  decode_partition(pbi, td, DEC_BLOCK_STX_ARG(db_r), (db_c), reader, \
                   (db_subsize), sbi, ptree->sub_tree[(index)],      \
                   get_partition_subtree_const(ptree_luma, index),   \
                   parse_decode_flag)
#elif CONFIG_FLEX_MVRES
#define DEC_PARTITION(db_r, db_c, db_subsize, index)                 \
  decode_partition(pbi, td, DEC_BLOCK_STX_ARG(db_r), (db_c), reader, \
                   (db_subsize), sbi, ptree->sub_tree[(index)],      \
                   parse_decode_flag)
#elif CONFIG_EXT_RECUR_PARTITIONS
#define DEC_PARTITION(db_r, db_c, db_subsize, index)                 \
  decode_partition(pbi, td, DEC_BLOCK_STX_ARG(db_r), (db_c), reader, \
                   (db_subsize), ptree->sub_tree[(index)],           \
                   get_partition_subtree_const(ptree_luma, index),   \
                   parse_decode_flag)
#else
#define DEC_PARTITION(db_r, db_c, db_subsize, index)                 \
  decode_partition(pbi, td, DEC_BLOCK_STX_ARG(db_r), (db_c), reader, \
                   (db_subsize), ptree->sub_tree[(index)], parse_decode_flag)
#endif  // CONFIG_FLEX_MVRES && CONFIG_EXT_RECUR_PARTITIONS

#if !CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE bsize2 = get_partition_subsize(bsize, PARTITION_SPLIT);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  switch (partition) {
    case PARTITION_NONE: DEC_BLOCK(mi_row, mi_col, subsize, 0); break;
    case PARTITION_HORZ:
#if CONFIG_EXT_RECUR_PARTITIONS
      DEC_PARTITION(mi_row, mi_col, subsize, 0);
      if ((mi_row + hbs_h) < cm->mi_params.mi_rows)
        DEC_PARTITION(mi_row + hbs_h, mi_col, subsize, 1);
#else
      DEC_BLOCK(mi_row, mi_col, subsize, 0);
      if (has_rows) DEC_BLOCK(mi_row + hbs_h, mi_col, subsize, 1);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;
    case PARTITION_VERT:
#if CONFIG_EXT_RECUR_PARTITIONS
      DEC_PARTITION(mi_row, mi_col, subsize, 0);
      if ((mi_col + hbs_w) < cm->mi_params.mi_cols)
        DEC_PARTITION(mi_row, mi_col + hbs_w, subsize, 1);
#else
      DEC_BLOCK(mi_row, mi_col, subsize, 0);
      if (has_cols) DEC_BLOCK(mi_row, mi_col + hbs_w, subsize, 1);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      int this_mi_row = mi_row;
      DEC_PARTITION(this_mi_row, mi_col, subsize, 0);
      this_mi_row += ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      DEC_PARTITION(this_mi_row, mi_col, bsize_med, 1);
      this_mi_row += 2 * ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      DEC_PARTITION(this_mi_row, mi_col, bsize_big, 2);
      this_mi_row += 4 * ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      DEC_PARTITION(this_mi_row, mi_col, subsize, 3);
      break;
    }
    case PARTITION_HORZ_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      int this_mi_row = mi_row;
      DEC_PARTITION(this_mi_row, mi_col, subsize, 0);
      this_mi_row += ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      DEC_PARTITION(this_mi_row, mi_col, bsize_big, 1);
      this_mi_row += 4 * ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      DEC_PARTITION(this_mi_row, mi_col, bsize_med, 2);
      this_mi_row += 2 * ebs_h;
      if (this_mi_row >= cm->mi_params.mi_rows) break;
      DEC_PARTITION(this_mi_row, mi_col, subsize, 3);
      break;
    }
    case PARTITION_VERT_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      int this_mi_col = mi_col;
      DEC_PARTITION(mi_row, this_mi_col, subsize, 0);
      this_mi_col += ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      DEC_PARTITION(mi_row, this_mi_col, bsize_med, 1);
      this_mi_col += 2 * ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      DEC_PARTITION(mi_row, this_mi_col, bsize_big, 2);
      this_mi_col += 4 * ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      DEC_PARTITION(mi_row, this_mi_col, subsize, 3);
      break;
    }
    case PARTITION_VERT_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      int this_mi_col = mi_col;
      DEC_PARTITION(mi_row, this_mi_col, subsize, 0);
      this_mi_col += ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      DEC_PARTITION(mi_row, this_mi_col, bsize_big, 1);
      this_mi_col += 4 * ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      DEC_PARTITION(mi_row, this_mi_col, bsize_med, 2);
      this_mi_col += 2 * ebs_w;
      if (this_mi_col >= cm->mi_params.mi_cols) break;
      DEC_PARTITION(mi_row, this_mi_col, subsize, 3);
      break;
    }
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3: {
      for (int i = 0; i < 4; ++i) {
        BLOCK_SIZE this_bsize = get_h_partition_subsize(bsize, i, partition);
        const int offset_r = get_h_partition_offset_mi_row(bsize, i, partition);
        const int offset_c = get_h_partition_offset_mi_col(bsize, i, partition);

        assert(this_bsize != BLOCK_INVALID);
        assert(offset_r >= 0 && offset_c >= 0);

        const int this_mi_row = mi_row + offset_r;
        const int this_mi_col = mi_col + offset_c;
        if (partition == PARTITION_HORZ_3) {
          if (this_mi_row >= cm->mi_params.mi_rows) break;
        } else {
          if (this_mi_col >= cm->mi_params.mi_cols) break;
        }

        DEC_PARTITION(this_mi_row, this_mi_col, this_bsize, i);
      }
      break;
    }
    case PARTITION_SPLIT:
      DEC_PARTITION(mi_row, mi_col, subsize, 0);
      DEC_PARTITION(mi_row, mi_col + hbs_w, subsize, 1);
      DEC_PARTITION(mi_row + hbs_h, mi_col, subsize, 2);
      DEC_PARTITION(mi_row + hbs_h, mi_col + hbs_w, subsize, 3);
      break;
#else   // !CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_SPLIT:
      DEC_PARTITION(mi_row, mi_col, subsize, 0);
      DEC_PARTITION(mi_row, mi_col + hbs_w, subsize, 1);
      DEC_PARTITION(mi_row + hbs_h, mi_col, subsize, 2);
      DEC_PARTITION(mi_row + hbs_h, mi_col + hbs_w, subsize, 3);
      break;
    case PARTITION_HORZ_A:
      DEC_BLOCK(mi_row, mi_col, bsize2, 0);
      DEC_BLOCK(mi_row, mi_col + hbs_w, bsize2, 1);
      DEC_BLOCK(mi_row + hbs_h, mi_col, subsize, 2);
      break;
    case PARTITION_HORZ_B:
      DEC_BLOCK(mi_row, mi_col, subsize, 0);
      DEC_BLOCK(mi_row + hbs_h, mi_col, bsize2, 1);
      DEC_BLOCK(mi_row + hbs_h, mi_col + hbs_w, bsize2, 2);
      break;
    case PARTITION_VERT_A:
      DEC_BLOCK(mi_row, mi_col, bsize2, 0);
      DEC_BLOCK(mi_row + hbs_h, mi_col, bsize2, 1);
      DEC_BLOCK(mi_row, mi_col + hbs_w, subsize, 2);
      break;
    case PARTITION_VERT_B:
      DEC_BLOCK(mi_row, mi_col, subsize, 0);
      DEC_BLOCK(mi_row, mi_col + hbs_w, bsize2, 1);
      DEC_BLOCK(mi_row + hbs_h, mi_col + hbs_w, bsize2, 2);
      break;
    case PARTITION_HORZ_4:
      for (int i = 0; i < 4; ++i) {
        int this_mi_row = mi_row + i * qbs_h;
        if (i > 0 && this_mi_row >= cm->mi_params.mi_rows) break;
        DEC_BLOCK(this_mi_row, mi_col, subsize, i);
      }
      break;
    case PARTITION_VERT_4:
      for (int i = 0; i < 4; ++i) {
        int this_mi_col = mi_col + i * qbs_w;
        if (i > 0 && this_mi_col >= cm->mi_params.mi_cols) break;
        DEC_BLOCK(mi_row, this_mi_col, subsize, i);
      }
      break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    default: assert(0 && "Invalid partition type");
  }

#undef DEC_PARTITION
#undef DEC_BLOCK
#undef DEC_BLOCK_EPT_ARG
#undef DEC_BLOCK_STX_ARG

  if (parse_decode_flag & 1)
    update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
}

static AOM_INLINE void setup_bool_decoder(
    const uint8_t *data, const uint8_t *data_end, const size_t read_size,
    struct aom_internal_error_info *error_info, aom_reader *r,
    uint8_t allow_update_cdf) {
  // Validate the calculated partition length. If the buffer
  // described by the partition can't be fully read, then restrict
  // it to the portion that can be (for EC mode) or throw an error.
  if (!read_is_valid(data, read_size, data_end))
    aom_internal_error(error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Truncated packet or corrupt tile length");

  if (aom_reader_init(r, data, read_size))
    aom_internal_error(error_info, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate bool decoder %d", 1);

  r->allow_update_cdf = allow_update_cdf;
}

static AOM_INLINE void decode_partition_sb(AV1Decoder *const pbi,
                                           ThreadData *const td, int mi_row,
                                           int mi_col, aom_reader *reader,
                                           BLOCK_SIZE bsize,
                                           int parse_decode_flag) {
  assert(bsize < BLOCK_SIZES_ALL);
  AV1_COMMON *const cm = &pbi->common;
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  const int total_loop_num =
      (frame_is_intra_only(cm) && !cm->seq_params.monochrome &&
       cm->seq_params.enable_sdp)
          ? 2
          : 1;
  xd->tree_type = (total_loop_num == 1 ? SHARED_PART : LUMA_PART);
  if (parse_decode_flag & 1) {
    av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
  }
  decode_partition(pbi, td, mi_row, mi_col, reader, bsize,
#if CONFIG_FLEX_MVRES
                   xd->sbi,
#endif  // CONFIG_FLEX_MVRES
                   td->dcb.xd.sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
#if CONFIG_EXT_RECUR_PARTITIONS
                   NULL,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                   parse_decode_flag);
  if (total_loop_num == 2) {
    xd->tree_type = CHROMA_PART;
    if (parse_decode_flag & 1) {
      av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
    }
    decode_partition(pbi, td, mi_row, mi_col, reader, bsize,
#if CONFIG_FLEX_MVRES
                     xd->sbi,
#endif  // CONFIG_FLEX_MVRES
                     td->dcb.xd.sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
#if CONFIG_EXT_RECUR_PARTITIONS
                     td->dcb.xd.sbi->ptree_root[0],
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                     parse_decode_flag);
    xd->tree_type = SHARED_PART;
  }
#if CONFIG_INSPECTION
  if (pbi->inspect_sb_cb != NULL) {
    (*pbi->inspect_sb_cb)(pbi, pbi->inspect_ctx);
  }
#endif  // CONFIG_INSPECTION
}

static AOM_INLINE void setup_segmentation(AV1_COMMON *const cm,
                                          struct aom_read_bit_buffer *rb) {
  struct segmentation *const seg = &cm->seg;

  seg->update_map = 0;
  seg->update_data = 0;
  seg->temporal_update = 0;

  seg->enabled = aom_rb_read_bit(rb);
  if (!seg->enabled) {
    if (cm->cur_frame->seg_map) {
      memset(cm->cur_frame->seg_map, 0,
             (cm->cur_frame->mi_rows * cm->cur_frame->mi_cols));
    }

    memset(seg, 0, sizeof(*seg));
    segfeatures_copy(&cm->cur_frame->seg, seg);
    return;
  }
  if (cm->seg.enabled && cm->prev_frame &&
      (cm->mi_params.mi_rows == cm->prev_frame->mi_rows) &&
      (cm->mi_params.mi_cols == cm->prev_frame->mi_cols)) {
    cm->last_frame_seg_map = cm->prev_frame->seg_map;
  } else {
    cm->last_frame_seg_map = NULL;
  }
  // Read update flags
#if CONFIG_PRIMARY_REF_FRAME_OPT
  if (cm->features.derived_primary_ref_frame == PRIMARY_REF_NONE) {
#else
  if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
    // These frames can't use previous frames, so must signal map + features
    seg->update_map = 1;
    seg->temporal_update = 0;
    seg->update_data = 1;
  } else {
    seg->update_map = aom_rb_read_bit(rb);
    if (seg->update_map) {
      seg->temporal_update = aom_rb_read_bit(rb);
    } else {
      seg->temporal_update = 0;
    }
    seg->update_data = aom_rb_read_bit(rb);
  }

  // Segmentation data update
  if (seg->update_data) {
    av1_clearall_segfeatures(seg);

    for (int i = 0; i < MAX_SEGMENTS; i++) {
      for (int j = 0; j < SEG_LVL_MAX; j++) {
        int data = 0;
        const int feature_enabled = aom_rb_read_bit(rb);
        if (feature_enabled) {
          av1_enable_segfeature(seg, i, j);

          const int data_max = av1_seg_feature_data_max(j);
          const int data_min = -data_max;
          const int ubits = get_unsigned_bits(data_max);

          if (av1_is_segfeature_signed(j)) {
            data = aom_rb_read_inv_signed_literal(rb, ubits);
          } else {
            data = aom_rb_read_literal(rb, ubits);
          }

          data = clamp(data, data_min, data_max);
        }
        av1_set_segdata(seg, i, j, data);
      }
    }
    av1_calculate_segdata(seg);
  } else if (cm->prev_frame) {
    segfeatures_copy(seg, &cm->prev_frame->seg);
  }
  segfeatures_copy(&cm->cur_frame->seg, seg);
}

// Same function as av1_read_uniform but reading from uncompressed header rb
static int rb_read_uniform(struct aom_read_bit_buffer *const rb, int n) {
  const int l = get_unsigned_bits(n);
  const int m = (1 << l) - n;
  const int v = aom_rb_read_literal(rb, l - 1);
  assert(l != 0);
  if (v < m)
    return v;
  else
    return (v << 1) - m + aom_rb_read_bit(rb);
}

#if CONFIG_LR_FLEX_SYNTAX
// Converts decoded index to frame restoration type depending on lr tools
// that are enabled for the frame for a given plane.
static RestorationType index_to_frame_restoration_type(
    const AV1_COMMON *const cm, int plane, int ndx) {
  RestorationType r = RESTORE_NONE;
  for (r = RESTORE_NONE; r < RESTORE_TYPES; ++r) {
    if (((cm->features.lr_tools_disable_mask[plane] >> r) & 1) == 0) {
      ndx--;
      if (ndx < 0) break;
    }
  }
  assert(r < RESTORE_TYPES);
  return r;
}
#endif  // CONFIG_LR_FLEX_SYNTAX

static AOM_INLINE void decode_restoration_mode(AV1_COMMON *cm,
                                               struct aom_read_bit_buffer *rb) {
  assert(!cm->features.all_lossless);
  const int num_planes = av1_num_planes(cm);
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  for (int p = 0; p < num_planes; ++p) {
    RestorationInfo *rsi = &cm->rst_info[p];
    rsi->frame_restoration_type = RESTORE_NONE;
    rsi->frame_cross_restoration_type = RESTORE_NONE;
  }
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  if (is_global_intrabc_allowed(cm)) return;
#if CONFIG_FLEXIBLE_RU_SIZE
  int luma_none = 1, chroma_none = 1;
#else
  int all_none = 1, chroma_none = 1;
#endif  // CONFIG_FLEXIBLE_RU_SIZE
  for (int p = 0; p < num_planes; ++p) {
    RestorationInfo *rsi = &cm->rst_info[p];
#if CONFIG_LR_FLEX_SYNTAX
    uint8_t plane_lr_tools_disable_mask =
        cm->seq_params.lr_tools_disable_mask[p > 0];
#if CONFIG_PC_WIENER
    // If superres is used turn off PC_WIENER since tx_skip values will
    // be misaligned.
    if (av1_superres_scaled(cm))
      plane_lr_tools_disable_mask |= (1 << RESTORE_PC_WIENER);
#endif  // CONFIG_PC_WIENER
    av1_set_lr_tools(plane_lr_tools_disable_mask, p, &cm->features);
    const int ndx = rb_read_uniform(rb, cm->features.lr_frame_tools_count[p]);
    rsi->frame_restoration_type = index_to_frame_restoration_type(cm, p, ndx);
    if (rsi->frame_restoration_type == RESTORE_SWITCHABLE &&
        cm->features.lr_tools_count[p] > 2) {
      if (aom_rb_read_bit(rb)) {
        int tools_count = cm->features.lr_tools_count[p];
        for (int i = 1; i < RESTORE_SWITCHABLE_TYPES; ++i) {
          if (!(plane_lr_tools_disable_mask & (1 << i))) {
            const int disable_tool = aom_rb_read_bit(rb);
            plane_lr_tools_disable_mask |= (disable_tool << i);
            tools_count -= disable_tool;
            // if tools_count becomes 2 break from the loop since we
            // do not allow any other tool to be disabled.
            if (tools_count == 2) break;
          }
        }
        av1_set_lr_tools(plane_lr_tools_disable_mask, p, &cm->features);
      }
    }
#else
    if (aom_rb_read_bit(rb)) {
      rsi->frame_restoration_type =
          aom_rb_read_bit(rb) ? RESTORE_SGRPROJ : RESTORE_WIENER;
    } else {
      if (aom_rb_read_bit(rb)) {
        rsi->frame_restoration_type = RESTORE_SWITCHABLE;
      } else {
#if CONFIG_PC_WIENER
        if (aom_rb_read_bit(rb)) {
#if CONFIG_WIENER_NONSEP
          if (aom_rb_read_bit(rb)) {
            rsi->frame_restoration_type = RESTORE_PC_WIENER;
          } else {
            rsi->frame_restoration_type = RESTORE_WIENER_NONSEP;
          }
#else
          rsi->frame_restoration_type = RESTORE_PC_WIENER;
#endif  // CONFIG_WIENER_NONSEP
        } else {
          rsi->frame_restoration_type = RESTORE_NONE;
        }
#elif CONFIG_WIENER_NONSEP
        rsi->frame_restoration_type =
            aom_rb_read_bit(rb) ? RESTORE_WIENER_NONSEP : RESTORE_NONE;
#else
        rsi->frame_restoration_type = RESTORE_NONE;
#endif  // CONFIG_PC_WIENER
      }
    }
#endif  // CONFIG_LR_FLEX_SYNTAX

#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    if (p > 0) {
      if (aom_rb_read_bit(rb)) {
        rsi->frame_cross_restoration_type = RESTORE_WIENER_NONSEP;
      }
    }
    if (rsi->frame_restoration_type != RESTORE_NONE ||
        rsi->frame_cross_restoration_type != RESTORE_NONE) {
#else
    if (rsi->frame_restoration_type != RESTORE_NONE) {
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
#if CONFIG_FLEXIBLE_RU_SIZE
      luma_none &= p > 0;
#else
      all_none = 0;
#endif  // CONFIG_FLEXIBLE_RU_SIZE
      chroma_none &= p == 0;
    }
#if CONFIG_WIENER_NONSEP
    const int is_wiener_nonsep_possible =
        rsi->frame_restoration_type == RESTORE_WIENER_NONSEP ||
        rsi->frame_restoration_type == RESTORE_SWITCHABLE;
    if (is_wiener_nonsep_possible)
      rsi->num_filter_classes = p == AOM_PLANE_Y
                                    ? NUM_WIENERNS_CLASS_INIT_LUMA
                                    : NUM_WIENERNS_CLASS_INIT_CHROMA;
#endif  // CONFIG_WIENER_NONSEP
  }
#if CONFIG_FLEXIBLE_RU_SIZE
  const int frame_width = cm->superres_upscaled_width;
  const int frame_height = cm->superres_upscaled_height;
  set_restoration_unit_size(frame_width, frame_height,
                            cm->seq_params.subsampling_x,
                            cm->seq_params.subsampling_y, cm->rst_info);
  int size = cm->rst_info[0].max_restoration_unit_size;

  cm->rst_info[0].restoration_unit_size =
      cm->rst_info[0].max_restoration_unit_size;
  if (!luma_none) {
    if (aom_rb_read_bit(rb))
      cm->rst_info[0].restoration_unit_size = size >> 1;
    else {
      if (aom_rb_read_bit(rb))
        cm->rst_info[0].restoration_unit_size = size;
      else
        cm->rst_info[0].restoration_unit_size = size >> 2;
    }
  }
  if (num_planes > 1) {
    cm->rst_info[1].restoration_unit_size =
        cm->rst_info[1].max_restoration_unit_size;
    if (!chroma_none) {
      size = cm->rst_info[1].max_restoration_unit_size;
      if (aom_rb_read_bit(rb))
        cm->rst_info[1].restoration_unit_size = size >> 1;
      else {
        if (aom_rb_read_bit(rb))
          cm->rst_info[1].restoration_unit_size = size;
        else
          cm->rst_info[1].restoration_unit_size = size >> 2;
      }
    }
    cm->rst_info[2].restoration_unit_size =
        cm->rst_info[1].restoration_unit_size;
  }
#else
  if (!all_none) {
#if CONFIG_BLOCK_256
    assert(cm->sb_size == BLOCK_64X64 || cm->sb_size == BLOCK_128X128 ||
           cm->sb_size == BLOCK_256X256);
#else
    assert(cm->sb_size == BLOCK_64X64 || cm->sb_size == BLOCK_128X128);
#endif  // CONFIG_BLOCK_256
    const int sb_size =
#if CONFIG_BLOCK_256
        cm->sb_size == BLOCK_256X256 ? 256 :
#endif  // CONFIG_BLOCK_256
        cm->sb_size == BLOCK_128X128 ? 128
                                     : 64;

    for (int p = 0; p < num_planes; ++p)
      cm->rst_info[p].restoration_unit_size = sb_size;

    RestorationInfo *rsi = &cm->rst_info[0];

#if CONFIG_BLOCK_256
    if (sb_size <= 128) {
      rsi->restoration_unit_size <<= aom_rb_read_bit(rb);
    }
    if (sb_size == 64) {
      rsi->restoration_unit_size <<= aom_rb_read_bit(rb);
    }
#else
    if (sb_size == 64) {
      rsi->restoration_unit_size <<= aom_rb_read_bit(rb);
    }
    // TODO(any): We could save a bit by adding a special case for sb_size ==
    // 128
    if (rsi->restoration_unit_size > 64) {
      rsi->restoration_unit_size <<= aom_rb_read_bit(rb);
    }
#endif  // CONFIG_BLOCK_256
  } else {
    const int size = RESTORATION_UNITSIZE_MAX;
    for (int p = 0; p < num_planes; ++p)
      cm->rst_info[p].restoration_unit_size = size;
  }

  if (num_planes > 1) {
    int s = AOMMIN(cm->seq_params.subsampling_x, cm->seq_params.subsampling_y);
    if (s && !chroma_none) {
      cm->rst_info[1].restoration_unit_size =
          cm->rst_info[0].restoration_unit_size >> (aom_rb_read_bit(rb) * s);
    } else {
      cm->rst_info[1].restoration_unit_size =
          cm->rst_info[0].restoration_unit_size;
    }
    cm->rst_info[2].restoration_unit_size =
        cm->rst_info[1].restoration_unit_size;
  }
#endif  // CONFIG_FLEXIBLE_RU_SIZE
}

static AOM_INLINE void read_wiener_filter(MACROBLOCKD *xd, int wiener_win,
                                          WienerInfo *wiener_info,
                                          WienerInfoBank *bank,
                                          aom_reader *rb) {
#if CONFIG_LR_MERGE_COEFFS
  const int exact_match = aom_read_symbol(rb, xd->tile_ctx->merged_param_cdf, 2,
                                          ACCT_INFO("exact_match"));
  int k;
  for (k = 0; k < bank->bank_size - 1; ++k) {
    if (aom_read_literal(rb, 1, ACCT_INFO("bank_size"))) break;
  }
  const int ref = k;
  if (exact_match) {
    memcpy(wiener_info, av1_constref_from_wiener_bank(bank, ref),
           sizeof(*wiener_info));
    wiener_info->bank_ref = ref;
    if (bank->bank_size == 0) av1_add_to_wiener_bank(bank, wiener_info);
    return;
  }
#else
  const int ref = 0;
  (void)xd;
#endif  // CONFIG_LR_MERGE_COEFFS
  WienerInfo *ref_wiener_info = av1_ref_from_wiener_bank(bank, ref);
  memset(wiener_info->vfilter, 0, sizeof(wiener_info->vfilter));
  memset(wiener_info->hfilter, 0, sizeof(wiener_info->hfilter));

  if (wiener_win == WIENER_WIN)
    wiener_info->vfilter[0] = wiener_info->vfilter[WIENER_WIN - 1] =
        aom_read_primitive_refsubexpfin(
            rb, WIENER_FILT_TAP0_MAXV - WIENER_FILT_TAP0_MINV + 1,
            WIENER_FILT_TAP0_SUBEXP_K,
            ref_wiener_info->vfilter[0] - WIENER_FILT_TAP0_MINV,
            ACCT_INFO("vfilter[0]")) +
        WIENER_FILT_TAP0_MINV;
  else
    wiener_info->vfilter[0] = wiener_info->vfilter[WIENER_WIN - 1] = 0;
  wiener_info->vfilter[1] = wiener_info->vfilter[WIENER_WIN - 2] =
      aom_read_primitive_refsubexpfin(
          rb, WIENER_FILT_TAP1_MAXV - WIENER_FILT_TAP1_MINV + 1,
          WIENER_FILT_TAP1_SUBEXP_K,
          ref_wiener_info->vfilter[1] - WIENER_FILT_TAP1_MINV,
          ACCT_INFO("vfilter[1]")) +
      WIENER_FILT_TAP1_MINV;
  wiener_info->vfilter[2] = wiener_info->vfilter[WIENER_WIN - 3] =
      aom_read_primitive_refsubexpfin(
          rb, WIENER_FILT_TAP2_MAXV - WIENER_FILT_TAP2_MINV + 1,
          WIENER_FILT_TAP2_SUBEXP_K,
          ref_wiener_info->vfilter[2] - WIENER_FILT_TAP2_MINV,
          ACCT_INFO("vfilter[2]")) +
      WIENER_FILT_TAP2_MINV;
  // The central element has an implicit +WIENER_FILT_STEP
  wiener_info->vfilter[WIENER_HALFWIN] =
      -2 * (wiener_info->vfilter[0] + wiener_info->vfilter[1] +
            wiener_info->vfilter[2]);

  if (wiener_win == WIENER_WIN)
    wiener_info->hfilter[0] = wiener_info->hfilter[WIENER_WIN - 1] =
        aom_read_primitive_refsubexpfin(
            rb, WIENER_FILT_TAP0_MAXV - WIENER_FILT_TAP0_MINV + 1,
            WIENER_FILT_TAP0_SUBEXP_K,
            ref_wiener_info->hfilter[0] - WIENER_FILT_TAP0_MINV,
            ACCT_INFO("hfilter[0]")) +
        WIENER_FILT_TAP0_MINV;
  else
    wiener_info->hfilter[0] = wiener_info->hfilter[WIENER_WIN - 1] = 0;
  wiener_info->hfilter[1] = wiener_info->hfilter[WIENER_WIN - 2] =
      aom_read_primitive_refsubexpfin(
          rb, WIENER_FILT_TAP1_MAXV - WIENER_FILT_TAP1_MINV + 1,
          WIENER_FILT_TAP1_SUBEXP_K,
          ref_wiener_info->hfilter[1] - WIENER_FILT_TAP1_MINV,
          ACCT_INFO("hfilter[1]")) +
      WIENER_FILT_TAP1_MINV;
  wiener_info->hfilter[2] = wiener_info->hfilter[WIENER_WIN - 3] =
      aom_read_primitive_refsubexpfin(
          rb, WIENER_FILT_TAP2_MAXV - WIENER_FILT_TAP2_MINV + 1,
          WIENER_FILT_TAP2_SUBEXP_K,
          ref_wiener_info->hfilter[2] - WIENER_FILT_TAP2_MINV,
          ACCT_INFO("hfilter[2]")) +
      WIENER_FILT_TAP2_MINV;
  // The central element has an implicit +WIENER_FILT_STEP
  wiener_info->hfilter[WIENER_HALFWIN] =
      -2 * (wiener_info->hfilter[0] + wiener_info->hfilter[1] +
            wiener_info->hfilter[2]);
  av1_add_to_wiener_bank(bank, wiener_info);
}

static AOM_INLINE void read_sgrproj_filter(MACROBLOCKD *xd,
                                           SgrprojInfo *sgrproj_info,
                                           SgrprojInfoBank *bank,
                                           aom_reader *rb) {
#if CONFIG_LR_MERGE_COEFFS
  const int exact_match = aom_read_symbol(rb, xd->tile_ctx->merged_param_cdf, 2,
                                          ACCT_INFO("exact_match"));
  int k;
  for (k = 0; k < bank->bank_size - 1; ++k) {
    if (aom_read_literal(rb, 1, ACCT_INFO("bank"))) break;
  }
  const int ref = k;
  if (exact_match) {
    memcpy(sgrproj_info, av1_constref_from_sgrproj_bank(bank, ref),
           sizeof(*sgrproj_info));
    sgrproj_info->bank_ref = ref;
    if (bank->bank_size == 0) av1_add_to_sgrproj_bank(bank, sgrproj_info);
    return;
  }
#else
  const int ref = 0;
  (void)xd;
#endif  // CONFIG_LR_MERGE_COEFFS
  SgrprojInfo *ref_sgrproj_info = av1_ref_from_sgrproj_bank(bank, ref);

  sgrproj_info->ep = aom_read_literal(rb, SGRPROJ_PARAMS_BITS, ACCT_INFO("ep"));
  const sgr_params_type *params = &av1_sgr_params[sgrproj_info->ep];

  if (params->r[0] == 0) {
    sgrproj_info->xqd[0] = 0;
    sgrproj_info->xqd[1] =
        aom_read_primitive_refsubexpfin(
            rb, SGRPROJ_PRJ_MAX1 - SGRPROJ_PRJ_MIN1 + 1, SGRPROJ_PRJ_SUBEXP_K,
            ref_sgrproj_info->xqd[1] - SGRPROJ_PRJ_MIN1, ACCT_INFO()) +
        SGRPROJ_PRJ_MIN1;
  } else if (params->r[1] == 0) {
    sgrproj_info->xqd[0] =
        aom_read_primitive_refsubexpfin(
            rb, SGRPROJ_PRJ_MAX0 - SGRPROJ_PRJ_MIN0 + 1, SGRPROJ_PRJ_SUBEXP_K,
            ref_sgrproj_info->xqd[0] - SGRPROJ_PRJ_MIN0, ACCT_INFO()) +
        SGRPROJ_PRJ_MIN0;
    sgrproj_info->xqd[1] = clamp((1 << SGRPROJ_PRJ_BITS) - sgrproj_info->xqd[0],
                                 SGRPROJ_PRJ_MIN1, SGRPROJ_PRJ_MAX1);
  } else {
    sgrproj_info->xqd[0] =
        aom_read_primitive_refsubexpfin(
            rb, SGRPROJ_PRJ_MAX0 - SGRPROJ_PRJ_MIN0 + 1, SGRPROJ_PRJ_SUBEXP_K,
            ref_sgrproj_info->xqd[0] - SGRPROJ_PRJ_MIN0, ACCT_INFO()) +
        SGRPROJ_PRJ_MIN0;
    sgrproj_info->xqd[1] =
        aom_read_primitive_refsubexpfin(
            rb, SGRPROJ_PRJ_MAX1 - SGRPROJ_PRJ_MIN1 + 1, SGRPROJ_PRJ_SUBEXP_K,
            ref_sgrproj_info->xqd[1] - SGRPROJ_PRJ_MIN1, ACCT_INFO()) +
        SGRPROJ_PRJ_MIN1;
  }

  av1_add_to_sgrproj_bank(bank, sgrproj_info);
}

#if CONFIG_WIENER_NONSEP
static void read_wienerns_filter(MACROBLOCKD *xd, int is_uv,
                                 WienerNonsepInfo *wienerns_info,
                                 WienerNonsepInfoBank *bank, aom_reader *rb) {
  int skip_filter_read_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  int ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  const int num_classes = wienerns_info->num_classes;
  assert(num_classes <= WIENERNS_MAX_CLASSES);
#if CONFIG_LR_MERGE_COEFFS
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    const int exact_match = aom_read_symbol(rb, xd->tile_ctx->merged_param_cdf,
                                            2, ACCT_INFO("exact_match"));
    int ref;
    for (ref = 0; ref < bank->bank_size_for_class[c_id] - 1; ++ref) {
      if (aom_read_literal(rb, 1, ACCT_INFO("bank"))) break;
    }
    if (exact_match) {
      copy_nsfilter_taps_for_class(
          wienerns_info, av1_constref_from_wienerns_bank(bank, ref, c_id),
          c_id);
    }
    wienerns_info->bank_ref_for_class[c_id] = ref;
    skip_filter_read_for_class[c_id] = exact_match;
    ref_for_class[c_id] = ref;
  }
#else
  (void)xd;
#endif  // CONFIG_LR_MERGE_COEFFS
  const WienernsFilterParameters *nsfilter_params =
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
      get_wienerns_parameters(xd->current_base_qindex, is_uv,
                              wienerns_info->is_cross_filter);
#else
      get_wienerns_parameters(xd->current_base_qindex, is_uv);
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  const int beg_feat = 0;
  const int end_feat = nsfilter_params->ncoeffs;
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;
  int reduce_step[WIENERNS_REDUCE_STEPS];
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    if (skip_filter_read_for_class[c_id]) continue;
    const int ref = ref_for_class[c_id];

    const WienerNonsepInfo *ref_wienerns_info =
        av1_ref_from_wienerns_bank(bank, ref, c_id);
    assert(ref_wienerns_info->num_classes == num_classes);
    int16_t *wienerns_info_nsfilter = nsfilter_taps(wienerns_info, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);

    memset(reduce_step, 0, sizeof(reduce_step));
    memset(wienerns_info_nsfilter + beg_feat, 0,
           (end_feat - beg_feat) * sizeof(wienerns_info_nsfilter[0]));
    // Whether the number of taps is odd or even. For luma
    // the #taps can be either odd or even. If odd, the last
    // tap corresponds to dc offset. For chroma, the #taps is
    // assumed to be always even.
    // if #taps is odd, the exit points for signaling are:
    // #total_taps - 1, #total_taps - 3, #total_taps - 5.
    // If #taps is even, the exit points for signaling are:
    // #total_taps - 2, #total_taps - 4, #total_taps - 6.
    const int rodd = is_uv ? 0 : (end_feat & 1);
    for (int i = beg_feat; i < end_feat; ++i) {
      if (rodd && i == end_feat - 5 && i != beg_feat) {
        reduce_step[0] =
            aom_read_symbol(rb, xd->tile_ctx->wienerns_reduce_cdf[0], 2,
                            ACCT_INFO("wienerns_reduce_cdf0"));
        if (reduce_step[0]) break;
      }
      if (!rodd && i == end_feat - 4 && i != beg_feat) {
        reduce_step[1] =
            aom_read_symbol(rb, xd->tile_ctx->wienerns_reduce_cdf[1], 2,
                            ACCT_INFO("wienerns_reduce_cdf1"));
        if (reduce_step[1]) break;
      }
      if (rodd && i == end_feat - 3 && i != beg_feat) {
        reduce_step[2] =
            aom_read_symbol(rb, xd->tile_ctx->wienerns_reduce_cdf[2], 2,
                            ACCT_INFO("wienerns_reduce_cdf2"));
        if (reduce_step[2]) break;
      }
      if (!rodd && i == end_feat - 2 && i != beg_feat) {
        reduce_step[3] =
            aom_read_symbol(rb, xd->tile_ctx->wienerns_reduce_cdf[3], 2,
                            ACCT_INFO("wienerns_reduce_cdf3"));
        if (reduce_step[3]) break;
      }
      if (rodd && i == end_feat - 1 && i != beg_feat) {
        reduce_step[4] =
            aom_read_symbol(rb, xd->tile_ctx->wienerns_reduce_cdf[4], 2,
                            ACCT_INFO("wienerns_reduce_cdf4"));
        if (reduce_step[4]) break;
      }
#if ENABLE_LR_4PART_CODE
      wienerns_info_nsfilter[i] =
          aom_read_4part_wref(
              rb,
              ref_wienerns_info_nsfilter[i] -
                  wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
              xd->tile_ctx->wienerns_4part_cdf
                  [wienerns_coeffs[i - beg_feat][WIENERNS_PAR_ID]],
              wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID],
              ACCT_INFO("wienerns_info_nsfilter")) +
          wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID];
#else
      wienerns_info_nsfilter[i] =
          aom_read_primitive_refsubexpfin(
              rb, (1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]),
              wienerns_coeffs[i - beg_feat][WIENERNS_PAR_ID],
              ref_wienerns_info_nsfilter[i] -
                  wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
              ACCT_INFO("wienerns_info_nsfilter")) +
          wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID];
#endif  // ENABLE_LR_4PART_CODE
    }
    av1_add_to_wienerns_bank(bank, wienerns_info, c_id);
  }
}
#endif  // CONFIG_WIENER_NONSEP

static AOM_INLINE void loop_restoration_read_sb_coeffs(
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    AV1_COMMON *const cm, MACROBLOCKD *xd, aom_reader *const r, int plane,
#else
    const AV1_COMMON *const cm, MACROBLOCKD *xd, aom_reader *const r, int plane,
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    int runit_idx) {
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  RestorationInfo *rsi = &cm->rst_info[plane];
#else
  const RestorationInfo *rsi = &cm->rst_info[plane];
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  RestorationUnitInfo *rui = &rsi->unit_info[runit_idx];
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  assert(rsi->frame_restoration_type != RESTORE_NONE ||
         rsi->frame_cross_restoration_type != RESTORE_NONE);
  rui->restoration_type = RESTORE_NONE;
  rui->cross_restoration_type = RESTORE_NONE;
#else
  assert(rsi->frame_restoration_type != RESTORE_NONE);
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER

  assert(!cm->features.all_lossless);

  const int wiener_win = (plane > 0) ? WIENER_WIN_CHROMA : WIENER_WIN;
#if CONFIG_WIENER_NONSEP
  rui->wienerns_info.num_classes = rsi->num_filter_classes;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  rui->wienerns_cross_info.num_classes =
      xd->wienerns_cross_info[plane].filter[0].num_classes;

  rui->wienerns_info.is_cross_filter = 0;
  rui->wienerns_cross_info.is_cross_filter = 1;
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
#endif  // CONFIG_WIENER_NONSEP

  if (rsi->frame_restoration_type == RESTORE_SWITCHABLE) {
#if CONFIG_LR_FLEX_SYNTAX
    rui->restoration_type = cm->features.lr_last_switchable_ndx_0_type[plane];
    for (int re = 0; re <= cm->features.lr_last_switchable_ndx[plane]; re++) {
      if (cm->features.lr_tools_disable_mask[plane] & (1 << re)) continue;
      const int found = aom_read_symbol(
          r, xd->tile_ctx->switchable_flex_restore_cdf[re][plane], 2,
          ACCT_INFO("found"));
      if (found) {
        rui->restoration_type = re;
        break;
      }
    }
#else
    rui->restoration_type = aom_read_symbol(
        r, xd->tile_ctx->switchable_restore_cdf, RESTORE_SWITCHABLE_TYPES,
        ACCT_INFO("restoration_type"));
#endif  // CONFIG_LR_FLEX_SYNTAX
    switch (rui->restoration_type) {
      case RESTORE_WIENER:
        read_wiener_filter(xd, wiener_win, &rui->wiener_info,
                           &xd->wiener_info[plane], r);
        break;
      case RESTORE_SGRPROJ:
        read_sgrproj_filter(xd, &rui->sgrproj_info, &xd->sgrproj_info[plane],
                            r);
        break;
#if CONFIG_WIENER_NONSEP
      case RESTORE_WIENER_NONSEP:
        read_wienerns_filter(xd, plane != AOM_PLANE_Y, &rui->wienerns_info,
                             &xd->wienerns_info[plane], r);
        break;
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
      case RESTORE_PC_WIENER:
        // No side-information for now.
        break;
#endif  // CONFIG_PC_WIENER
      default: assert(rui->restoration_type == RESTORE_NONE); break;
    }
  } else if (rsi->frame_restoration_type == RESTORE_WIENER) {
    if (aom_read_symbol(r, xd->tile_ctx->wiener_restore_cdf, 2,
                        ACCT_INFO("wiener_restore_cdf"))) {
      rui->restoration_type = RESTORE_WIENER;
      read_wiener_filter(xd, wiener_win, &rui->wiener_info,
                         &xd->wiener_info[plane], r);
    } else {
      rui->restoration_type = RESTORE_NONE;
    }
  } else if (rsi->frame_restoration_type == RESTORE_SGRPROJ) {
    if (aom_read_symbol(r, xd->tile_ctx->sgrproj_restore_cdf, 2,
                        ACCT_INFO("sgrproj_restore_cdf"))) {
      rui->restoration_type = RESTORE_SGRPROJ;
      read_sgrproj_filter(xd, &rui->sgrproj_info, &xd->sgrproj_info[plane], r);
    } else {
      rui->restoration_type = RESTORE_NONE;
    }
#if CONFIG_WIENER_NONSEP
  } else if (rsi->frame_restoration_type == RESTORE_WIENER_NONSEP) {
    if (aom_read_symbol(r, xd->tile_ctx->wienerns_restore_cdf, 2,
                        ACCT_INFO("wienerns_restore_cdf"))) {
      rui->restoration_type = RESTORE_WIENER_NONSEP;
      read_wienerns_filter(xd, plane != AOM_PLANE_Y, &rui->wienerns_info,
                           &xd->wienerns_info[plane], r);
    } else {
      rui->restoration_type = RESTORE_NONE;
    }
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
  } else if (rsi->frame_restoration_type == RESTORE_PC_WIENER) {
    if (aom_read_symbol(r, xd->tile_ctx->pc_wiener_restore_cdf, 2,
                        ACCT_INFO("pc_wiener_restore_cdf"))) {
      rui->restoration_type = RESTORE_PC_WIENER;
      // No side-information for now.
    } else {
      rui->restoration_type = RESTORE_NONE;
    }
#endif  // CONFIG_PC_WIENER
  }

#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  if (rsi->frame_cross_restoration_type == RESTORE_WIENER_NONSEP) {
    if (aom_read_symbol(r, xd->tile_ctx->wienerns_restore_cdf, 2,
                        ACCT_INFO())) {
      rui->cross_restoration_type = RESTORE_WIENER_NONSEP;
      read_wienerns_filter(xd, plane != AOM_PLANE_Y, &rui->wienerns_cross_info,
                           &xd->wienerns_cross_info[plane], r);
    }
  }
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER

#if CONFIG_LR_FLEX_SYNTAX
  assert(((cm->features.lr_tools_disable_mask[plane] >> rui->restoration_type) &
          1) == 0);
#endif  // CONFIG_LR_FLEX_SYNTAX
}
static AOM_INLINE void setup_loopfilter(AV1_COMMON *cm,
                                        struct aom_read_bit_buffer *rb) {
  const int num_planes = av1_num_planes(cm);
  struct loopfilter *lf = &cm->lf;

  if (is_global_intrabc_allowed(cm) || cm->features.coded_lossless) {
    // write default deltas to frame buffer
    av1_set_default_ref_deltas(cm->cur_frame->ref_deltas);
    av1_set_default_mode_deltas(cm->cur_frame->mode_deltas);
    return;
  }
  assert(!cm->features.coded_lossless);

  if (cm->prev_frame) {
    // write deltas to frame buffer
    memcpy(lf->ref_deltas, cm->prev_frame->ref_deltas, SINGLE_REF_FRAMES);
    memcpy(lf->mode_deltas, cm->prev_frame->mode_deltas, MAX_MODE_LF_DELTAS);
  } else {
    av1_set_default_ref_deltas(lf->ref_deltas);
    av1_set_default_mode_deltas(lf->mode_deltas);
  }

  lf->filter_level[0] = aom_rb_read_bit(rb);
#if DF_DUAL
  lf->filter_level[1] = aom_rb_read_bit(rb);
#else
  lf->filter_level[1] = lf->filter_level[0];
#endif  // DF_DUAL
  if (num_planes > 1) {
    if (lf->filter_level[0] || lf->filter_level[1]) {
      lf->filter_level_u = aom_rb_read_bit(rb);
      lf->filter_level_v = aom_rb_read_bit(rb);
    } else {
      lf->filter_level_u = lf->filter_level_v = 0;
    }
  }
  //  lf->sharpness_level = 0;

#if DF_DUAL
  if (lf->filter_level[0]) {
    int luma_delta_q = aom_rb_read_bit(rb);
    if (luma_delta_q) {
      lf->delta_q_luma[0] =
          aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_q_luma[0] = 0;
    }
#if DF_TWO_PARAM
    int luma_delta_side = aom_rb_read_bit(rb);
    if (luma_delta_side) {
      lf->delta_side_luma[0] =
          aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_side_luma[0] = 0;
    }
#else
    lf->delta_side_luma[0] = lf->delta_q_luma[0];
#endif  // DF_TWO_PARAM
  } else {
    lf->delta_q_luma[0] = 0;
    lf->delta_side_luma[0] = 0;
  }
  if (lf->filter_level[1]) {
    int luma_delta_q = aom_rb_read_bit(rb);
    if (luma_delta_q) {
      lf->delta_q_luma[1] =
          aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_q_luma[1] = lf->delta_q_luma[0];
    }
#if DF_TWO_PARAM
    int luma_delta_side = aom_rb_read_bit(rb);
    if (luma_delta_side) {
      lf->delta_side_luma[1] =
          aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_side_luma[1] = lf->delta_side_luma[0];
    }
#else
    lf->delta_side_luma[1] = lf->delta_q_luma[1];
#endif  // DF_TWO_PARAM
  } else {
    lf->delta_q_luma[1] = 0;
    lf->delta_side_luma[1] = 0;
  }
#else
  if (lf->filter_level[0] || lf->filter_level[1]) {
    int luma_delta_q = aom_rb_read_bit(rb);
    if (luma_delta_q) {
      lf->delta_q_luma = aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_q_luma = 0;
    }
#if DF_TWO_PARAM
    int luma_delta_side = aom_rb_read_bit(rb);
    if (luma_delta_side) {
      lf->delta_side_luma =
          aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_side_luma = 0;
    }
#else
    lf->delta_side_luma = lf->delta_q_luma;
#endif  // DF_TWO_PARAM
  } else {
    lf->delta_q_luma = 0;
    lf->delta_side_luma = 0;
  }
#endif  // DF_DUAL

  if (lf->filter_level_u) {
    int u_delta_q = aom_rb_read_bit(rb);
    if (u_delta_q) {
      lf->delta_q_u = aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_q_u = 0;
    }
#if DF_TWO_PARAM
    int u_delta_side = aom_rb_read_bit(rb);
    if (u_delta_side) {
      lf->delta_side_u = aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_side_u = 0;
    }
#else
    lf->delta_side_u = lf->delta_q_u;
#endif  // DF_TWO_PARAM
  } else {
    lf->delta_q_u = 0;
    lf->delta_side_u = 0;
  }
  if (lf->filter_level_v) {
    int v_delta_q = aom_rb_read_bit(rb);
    if (v_delta_q) {
      lf->delta_q_v = aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_q_v = 0;
    }
#if DF_TWO_PARAM
    int v_delta_side = aom_rb_read_bit(rb);
    if (v_delta_side) {
      lf->delta_side_v = aom_rb_read_literal(rb, DF_PAR_BITS) - DF_PAR_OFFSET;
    } else {
      lf->delta_side_v = 0;
    }
#else
    lf->delta_side_v = lf->delta_q_v;
#endif  // DF_TWO_PARAM
  } else {
    lf->delta_q_v = 0;
    lf->delta_side_v = 0;
  }
  lf->mode_ref_delta_update = 0;
  lf->mode_ref_delta_enabled = 0;
}

static AOM_INLINE void setup_cdef(AV1_COMMON *cm,
                                  struct aom_read_bit_buffer *rb) {
  const int num_planes = av1_num_planes(cm);
  CdefInfo *const cdef_info = &cm->cdef_info;

  if (is_global_intrabc_allowed(cm)) return;
#if CONFIG_FIX_CDEF_SYNTAX
  cdef_info->cdef_frame_enable = aom_rb_read_bit(rb);
  if (!cdef_info->cdef_frame_enable) return;
#endif  // CONFIG_FIX_CDEF_SYNTAX
  cdef_info->cdef_damping = aom_rb_read_literal(rb, 2) + 3;
  cdef_info->cdef_bits = aom_rb_read_literal(rb, 2);
  cdef_info->nb_cdef_strengths = 1 << cdef_info->cdef_bits;
  for (int i = 0; i < cdef_info->nb_cdef_strengths; i++) {
    cdef_info->cdef_strengths[i] = aom_rb_read_literal(rb, CDEF_STRENGTH_BITS);
    cdef_info->cdef_uv_strengths[i] =
        num_planes > 1 ? aom_rb_read_literal(rb, CDEF_STRENGTH_BITS) : 0;
  }
}

#if CONFIG_CCSO
#if CONFIG_CCSO_EDGE_CLF
// read offset idx using truncated unary coding
static AOM_INLINE int read_ccso_offset_idx(struct aom_read_bit_buffer *rb) {
  int offset_idx = 0;
  for (int idx = 0; idx < 7; ++idx) {
    const int cur_bit = aom_rb_read_bit(rb);
    if (!cur_bit) break;
    offset_idx++;
  }
  return offset_idx;
}
#endif  // CONFIG_CCSO_EDGE_CLF
static AOM_INLINE void setup_ccso(AV1_COMMON *cm,
                                  struct aom_read_bit_buffer *rb) {
  if (is_global_intrabc_allowed(cm)) return;
#if CONFIG_CCSO_EXT
  const int ccso_offset[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
#if CONFIG_D143_CCSO_FM_FLAG
  cm->ccso_info.ccso_frame_flag = aom_rb_read_literal(rb, 1);
  if (cm->ccso_info.ccso_frame_flag) {
#endif  // CONFIG_D143_CCSO_FM_FLAG
    for (int plane = 0; plane < av1_num_planes(cm); plane++) {
#else
  const int ccso_offset[8] = { 0, 1, -1, 3, -3, 5, -5, -7 };
  for (int plane = 0; plane < 2; plane++) {
#endif
      cm->ccso_info.ccso_enable[plane] = aom_rb_read_literal(rb, 1);
      if (cm->ccso_info.ccso_enable[plane]) {
#if CONFIG_CCSO_BO_ONLY_OPTION
        cm->ccso_info.ccso_bo_only[plane] = aom_rb_read_literal(rb, 1);
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        cm->ccso_info.quant_idx[plane] = aom_rb_read_literal(rb, 2);
        cm->ccso_info.ext_filter_support[plane] = aom_rb_read_literal(rb, 3);
#if CONFIG_CCSO_EXT
#if CONFIG_CCSO_BO_ONLY_OPTION
        if (cm->ccso_info.ccso_bo_only[plane]) {
          cm->ccso_info.max_band_log2[plane] = aom_rb_read_literal(rb, 3);
        } else {
          cm->ccso_info.max_band_log2[plane] = aom_rb_read_literal(rb, 2);
        }
#else
      cm->ccso_info.max_band_log2[plane] = aom_rb_read_literal(rb, 2);
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
        const int max_band = 1 << cm->ccso_info.max_band_log2[plane];
#endif
#if CONFIG_CCSO_EDGE_CLF
        const int edge_clf = cm->ccso_info.edge_clf[plane] =
            aom_rb_read_bit(rb);
        const int max_edge_interval = edge_clf_to_edge_interval[edge_clf];
#if CONFIG_CCSO_BO_ONLY_OPTION
        const int num_edge_offset_intervals =
            cm->ccso_info.ccso_bo_only[plane] ? 1 : max_edge_interval;
        for (int d0 = 0; d0 < num_edge_offset_intervals; d0++) {
          for (int d1 = 0; d1 < num_edge_offset_intervals; d1++) {
#else
      for (int d0 = 0; d0 < max_edge_interval; d0++) {
        for (int d1 = 0; d1 < max_edge_interval; d1++) {
#endif  // CONFIG_CCSO_BO_ONLY_OPTION
#else
      for (int d0 = 0; d0 < CCSO_INPUT_INTERVAL; d0++) {
        for (int d1 = 0; d1 < CCSO_INPUT_INTERVAL; d1++) {
#endif  // CONFIG_CCSO_EDGE_CLF
#if !CONFIG_CCSO_EXT
            const int lut_idx_ext = (d0 << 2) + d1;
#else
          for (int band_num = 0; band_num < max_band; band_num++) {
            const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
#endif
#if CONFIG_CCSO_EDGE_CLF
            const int offset_idx = read_ccso_offset_idx(rb);
#else
            const int offset_idx = aom_rb_read_literal(rb, 3);
#endif  // CONFIG_CCSO_EDGE_CLF
            cm->ccso_info.filter_offset[plane][lut_idx_ext] =
                ccso_offset[offset_idx];
          }
#if CONFIG_CCSO_EXT
        }
#endif
      }
    }
  }
#if CONFIG_D143_CCSO_FM_FLAG
}
else {
  cm->ccso_info.ccso_enable[0] = 0;
  cm->ccso_info.ccso_enable[1] = 0;
  cm->ccso_info.ccso_enable[2] = 0;
}
#endif  // CONFIG_D143_CCSO_FM_FLAG
}
#endif

static INLINE int read_delta_q(struct aom_read_bit_buffer *rb) {
  return aom_rb_read_bit(rb) ? aom_rb_read_inv_signed_literal(rb, 6) : 0;
}

static AOM_INLINE void setup_quantization(CommonQuantParams *quant_params,
                                          int num_planes,
                                          aom_bit_depth_t bit_depth,
                                          bool separate_uv_delta_q,
                                          struct aom_read_bit_buffer *rb) {
  quant_params->base_qindex = aom_rb_read_literal(
      rb, bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT : QINDEX_BITS);
  quant_params->y_dc_delta_q = read_delta_q(rb);
  if (num_planes > 1) {
    int diff_uv_delta = 0;
    if (separate_uv_delta_q) diff_uv_delta = aom_rb_read_bit(rb);
    quant_params->u_dc_delta_q = read_delta_q(rb);
    quant_params->u_ac_delta_q = read_delta_q(rb);
    if (diff_uv_delta) {
      quant_params->v_dc_delta_q = read_delta_q(rb);
      quant_params->v_ac_delta_q = read_delta_q(rb);
    } else {
      quant_params->v_dc_delta_q = quant_params->u_dc_delta_q;
      quant_params->v_ac_delta_q = quant_params->u_ac_delta_q;
    }
  } else {
    quant_params->u_dc_delta_q = 0;
    quant_params->u_ac_delta_q = 0;
    quant_params->v_dc_delta_q = 0;
    quant_params->v_ac_delta_q = 0;
  }
  quant_params->using_qmatrix = aom_rb_read_bit(rb);
  if (quant_params->using_qmatrix) {
    quant_params->qmatrix_level_y = aom_rb_read_literal(rb, QM_LEVEL_BITS);
    quant_params->qmatrix_level_u = aom_rb_read_literal(rb, QM_LEVEL_BITS);
    if (!separate_uv_delta_q)
      quant_params->qmatrix_level_v = quant_params->qmatrix_level_u;
    else
      quant_params->qmatrix_level_v = aom_rb_read_literal(rb, QM_LEVEL_BITS);
  } else {
    quant_params->qmatrix_level_y = 0;
    quant_params->qmatrix_level_u = 0;
    quant_params->qmatrix_level_v = 0;
  }
}

// Build y/uv dequant values based on segmentation.
static AOM_INLINE void setup_segmentation_dequant(AV1_COMMON *const cm,
                                                  MACROBLOCKD *const xd) {
  const int bit_depth = cm->seq_params.bit_depth;
  // When segmentation is disabled, only the first value is used.  The
  // remaining are don't cares.
  const int max_segments = cm->seg.enabled ? MAX_SEGMENTS : 1;
  CommonQuantParams *const quant_params = &cm->quant_params;
  for (int i = 0; i < max_segments; ++i) {
    const int qindex = xd->qindex[i];
    quant_params->y_dequant_QTX[i][0] =
        av1_dc_quant_QTX(qindex, quant_params->y_dc_delta_q,
                         cm->seq_params.base_y_dc_delta_q, bit_depth);
    quant_params->y_dequant_QTX[i][1] = av1_ac_quant_QTX(qindex, 0, bit_depth);
    quant_params->u_dequant_QTX[i][0] =
        av1_dc_quant_QTX(qindex, quant_params->u_dc_delta_q,
                         cm->seq_params.base_uv_dc_delta_q, bit_depth);
    quant_params->u_dequant_QTX[i][1] =
        av1_ac_quant_QTX(qindex, quant_params->u_ac_delta_q, bit_depth);
    quant_params->v_dequant_QTX[i][0] =
        av1_dc_quant_QTX(qindex, quant_params->v_dc_delta_q,
                         cm->seq_params.base_uv_dc_delta_q, bit_depth);
    quant_params->v_dequant_QTX[i][1] =
        av1_ac_quant_QTX(qindex, quant_params->v_ac_delta_q, bit_depth);
    const int use_qmatrix = av1_use_qmatrix(quant_params, xd, i);
    // NB: depends on base index so there is only 1 set per frame
    // No quant weighting when lossless or signalled not using QM
    const int qmlevel_y =
        use_qmatrix ? quant_params->qmatrix_level_y : NUM_QM_LEVELS - 1;
    for (int j = 0; j < TX_SIZES_ALL; ++j) {
      quant_params->y_iqmatrix[i][j] =
          av1_iqmatrix(quant_params, qmlevel_y, AOM_PLANE_Y, j);
    }
    const int qmlevel_u =
        use_qmatrix ? quant_params->qmatrix_level_u : NUM_QM_LEVELS - 1;
    for (int j = 0; j < TX_SIZES_ALL; ++j) {
      quant_params->u_iqmatrix[i][j] =
          av1_iqmatrix(quant_params, qmlevel_u, AOM_PLANE_U, j);
    }
    const int qmlevel_v =
        use_qmatrix ? quant_params->qmatrix_level_v : NUM_QM_LEVELS - 1;
    for (int j = 0; j < TX_SIZES_ALL; ++j) {
      quant_params->v_iqmatrix[i][j] =
          av1_iqmatrix(quant_params, qmlevel_v, AOM_PLANE_V, j);
    }
  }
}

static InterpFilter read_frame_interp_filter(struct aom_read_bit_buffer *rb) {
  return aom_rb_read_bit(rb) ? SWITCHABLE
                             : aom_rb_read_literal(rb, LOG_SWITCHABLE_FILTERS);
}

static AOM_INLINE void setup_render_size(AV1_COMMON *cm,
                                         struct aom_read_bit_buffer *rb) {
  cm->render_width = cm->superres_upscaled_width;
  cm->render_height = cm->superres_upscaled_height;
  if (aom_rb_read_bit(rb))
    av1_read_frame_size(rb, 16, 16, &cm->render_width, &cm->render_height);
}

// TODO(afergs): make "struct aom_read_bit_buffer *const rb"?
static AOM_INLINE void setup_superres(AV1_COMMON *const cm,
                                      struct aom_read_bit_buffer *rb,
                                      int *width, int *height) {
  cm->superres_upscaled_width = *width;
  cm->superres_upscaled_height = *height;
  cm->superres_scale_denominator = SCALE_NUMERATOR;

  const SequenceHeader *const seq_params = &cm->seq_params;
  if (!seq_params->enable_superres) return;

  if (aom_rb_read_bit(rb)) {
    cm->superres_scale_denominator =
        (uint8_t)aom_rb_read_literal(rb, SUPERRES_SCALE_BITS);
    cm->superres_scale_denominator += SUPERRES_SCALE_DENOMINATOR_MIN;
    // Don't edit cm->width or cm->height directly, or the buffers won't get
    // resized correctly
    av1_calculate_scaled_superres_size(width, height,
                                       cm->superres_scale_denominator);
  } else {
    // 1:1 scaling - ie. no scaling, scale not provided
    cm->superres_scale_denominator = SCALE_NUMERATOR;
  }
}

static AOM_INLINE void resize_context_buffers(AV1_COMMON *cm, int width,
                                              int height) {
#if CONFIG_SIZE_LIMIT
  if (width > DECODE_WIDTH_LIMIT || height > DECODE_HEIGHT_LIMIT)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Dimensions of %dx%d beyond allowed size of %dx%d.",
                       width, height, DECODE_WIDTH_LIMIT, DECODE_HEIGHT_LIMIT);
#endif
  if (cm->width != width || cm->height != height) {
    const int new_mi_rows =
        ALIGN_POWER_OF_TWO(height, MI_SIZE_LOG2) >> MI_SIZE_LOG2;
    const int new_mi_cols =
        ALIGN_POWER_OF_TWO(width, MI_SIZE_LOG2) >> MI_SIZE_LOG2;

    // Allocations in av1_alloc_context_buffers() depend on individual
    // dimensions as well as the overall size.
    if (new_mi_cols > cm->mi_params.mi_cols ||
        new_mi_rows > cm->mi_params.mi_rows) {
      if (av1_alloc_context_buffers(cm, width, height)) {
        // The cm->mi_* values have been cleared and any existing context
        // buffers have been freed. Clear cm->width and cm->height to be
        // consistent and to force a realloc next time.
        cm->width = 0;
        cm->height = 0;
        aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate context buffers");
      }
    } else {
      cm->mi_params.set_mb_mi(&cm->mi_params, width, height);
    }
    av1_init_mi_buffers(&cm->mi_params);
    cm->width = width;
    cm->height = height;
  }

  ensure_mv_buffer(cm->cur_frame, cm);
  cm->cur_frame->width = cm->width;
  cm->cur_frame->height = cm->height;
}

static AOM_INLINE void setup_tip_frame_size(AV1_COMMON *cm) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tip_frame->buf;
  if (aom_realloc_frame_buffer(
          tip_frame_buf, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, AOM_DEC_BORDER_IN_PIXELS,
          cm->features.byte_alignment, NULL, NULL, NULL, 0)) {
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate frame buffer");
  }

  if (tip_frame_buf) {
    tip_frame_buf->bit_depth = (unsigned int)seq_params->bit_depth;
    tip_frame_buf->color_primaries = seq_params->color_primaries;
    tip_frame_buf->transfer_characteristics =
        seq_params->transfer_characteristics;
    tip_frame_buf->matrix_coefficients = seq_params->matrix_coefficients;
    tip_frame_buf->monochrome = seq_params->monochrome;
    tip_frame_buf->chroma_sample_position = seq_params->chroma_sample_position;
    tip_frame_buf->color_range = seq_params->color_range;
    tip_frame_buf->render_width = cm->render_width;
    tip_frame_buf->render_height = cm->render_height;
  }

#if CONFIG_TIP_DIRECT_FRAME_MV
  tip_frame_buf = &cm->tip_ref.tmp_tip_frame->buf;
  if (aom_realloc_frame_buffer(
          tip_frame_buf, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, AOM_DEC_BORDER_IN_PIXELS,
          cm->features.byte_alignment, NULL, NULL, NULL, 0)) {
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate frame buffer");
  }

  if (tip_frame_buf) {
    tip_frame_buf->bit_depth = (unsigned int)seq_params->bit_depth;
    tip_frame_buf->color_primaries = seq_params->color_primaries;
    tip_frame_buf->transfer_characteristics =
        seq_params->transfer_characteristics;
    tip_frame_buf->matrix_coefficients = seq_params->matrix_coefficients;
    tip_frame_buf->monochrome = seq_params->monochrome;
    tip_frame_buf->chroma_sample_position = seq_params->chroma_sample_position;
    tip_frame_buf->color_range = seq_params->color_range;
    tip_frame_buf->render_width = cm->render_width;
    tip_frame_buf->render_height = cm->render_height;
  }
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
}

static AOM_INLINE void setup_buffer_pool(AV1_COMMON *cm) {
  BufferPool *const pool = cm->buffer_pool;
  const SequenceHeader *const seq_params = &cm->seq_params;

  lock_buffer_pool(pool);
  if (aom_realloc_frame_buffer(
          &cm->cur_frame->buf, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, AOM_DEC_BORDER_IN_PIXELS,
          cm->features.byte_alignment, &cm->cur_frame->raw_frame_buffer,
          pool->get_fb_cb, pool->cb_priv, 0)) {
    unlock_buffer_pool(pool);
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate frame buffer");
  }
  unlock_buffer_pool(pool);

  cm->cur_frame->buf.bit_depth = (unsigned int)seq_params->bit_depth;
  cm->cur_frame->buf.color_primaries = seq_params->color_primaries;
  cm->cur_frame->buf.transfer_characteristics =
      seq_params->transfer_characteristics;
  cm->cur_frame->buf.matrix_coefficients = seq_params->matrix_coefficients;
  cm->cur_frame->buf.monochrome = seq_params->monochrome;
  cm->cur_frame->buf.chroma_sample_position =
      seq_params->chroma_sample_position;
  cm->cur_frame->buf.color_range = seq_params->color_range;
  cm->cur_frame->buf.render_width = cm->render_width;
  cm->cur_frame->buf.render_height = cm->render_height;
  if (cm->seq_params.enable_tip) {
    const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, TIP_FRAME);
    if (ref_buf == NULL || (ref_buf->buf.y_crop_width != cm->width ||
                            ref_buf->buf.y_crop_height != cm->height)) {
      setup_tip_frame_size(cm);
    }
  }
}

static AOM_INLINE void setup_frame_size(AV1_COMMON *cm,
                                        int frame_size_override_flag,
                                        struct aom_read_bit_buffer *rb) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  int width, height;

  if (frame_size_override_flag) {
    int num_bits_width = seq_params->num_bits_width;
    int num_bits_height = seq_params->num_bits_height;
    av1_read_frame_size(rb, num_bits_width, num_bits_height, &width, &height);
    if (width > seq_params->max_frame_width ||
        height > seq_params->max_frame_height) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Frame dimensions are larger than the maximum values");
    }
  } else {
    width = seq_params->max_frame_width;
    height = seq_params->max_frame_height;
  }

  setup_superres(cm, rb, &width, &height);
  resize_context_buffers(cm, width, height);
  setup_render_size(cm, rb);
  setup_buffer_pool(cm);
}

static AOM_INLINE void setup_seq_sb_size(SequenceHeader *seq_params,
                                         struct aom_read_bit_buffer *rb) {
  static const BLOCK_SIZE sb_sizes[] = {
#if CONFIG_BLOCK_256
    BLOCK_256X256,
#endif
    BLOCK_128X128,
    BLOCK_64X64
  };
  int index = 0;
  bool bit = aom_rb_read_bit(rb);
  if (!bit) {
    index++;
#if CONFIG_BLOCK_256
    bit = aom_rb_read_bit(rb);
    if (!bit) {
      index++;
    }
#endif
  }
  BLOCK_SIZE sb_size = sb_sizes[index];
  seq_params->sb_size = sb_size;
  seq_params->mib_size = mi_size_wide[sb_size];
  seq_params->mib_size_log2 = mi_size_wide_log2[sb_size];
}

static INLINE int valid_ref_frame_img_fmt(aom_bit_depth_t ref_bit_depth,
                                          int ref_xss, int ref_yss,
                                          aom_bit_depth_t this_bit_depth,
                                          int this_xss, int this_yss) {
  return ref_bit_depth == this_bit_depth && ref_xss == this_xss &&
         ref_yss == this_yss;
}

static AOM_INLINE void setup_frame_size_with_refs(
    AV1_COMMON *cm, struct aom_read_bit_buffer *rb) {
  int width, height;
  int found = 0;
  int has_valid_ref_frame = 0;
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    if (aom_rb_read_bit(rb)) {
      const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, i);
      // This will never be NULL in a normal stream, as streams are required to
      // have a shown keyframe before any inter frames, which would refresh all
      // the reference buffers. However, it might be null if we're starting in
      // the middle of a stream, and static analysis will error if we don't do
      // a null check here.
      if (ref_buf == NULL) {
        aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                           "Invalid condition: invalid reference buffer");
      } else {
        const YV12_BUFFER_CONFIG *const buf = &ref_buf->buf;
        width = buf->y_crop_width;
        height = buf->y_crop_height;
        cm->render_width = buf->render_width;
        cm->render_height = buf->render_height;
        setup_superres(cm, rb, &width, &height);
        resize_context_buffers(cm, width, height);
        found = 1;
        break;
      }
    }
  }

  const SequenceHeader *const seq_params = &cm->seq_params;
  if (!found) {
    int num_bits_width = seq_params->num_bits_width;
    int num_bits_height = seq_params->num_bits_height;

    av1_read_frame_size(rb, num_bits_width, num_bits_height, &width, &height);
    setup_superres(cm, rb, &width, &height);
    resize_context_buffers(cm, width, height);
    setup_render_size(cm, rb);
  }

  if (width <= 0 || height <= 0)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid frame size");

  // Check to make sure at least one of frames that this frame references has
  // valid dimensions.
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    const RefCntBuffer *const ref_frame = get_ref_frame_buf(cm, i);
    has_valid_ref_frame |=
        valid_ref_frame_size(ref_frame->buf.y_crop_width,
                             ref_frame->buf.y_crop_height, width, height);
  }
  if (!has_valid_ref_frame)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Referenced frame has invalid size");
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    const RefCntBuffer *const ref_frame = get_ref_frame_buf(cm, i);
    if (!valid_ref_frame_img_fmt(
            ref_frame->buf.bit_depth, ref_frame->buf.subsampling_x,
            ref_frame->buf.subsampling_y, seq_params->bit_depth,
            seq_params->subsampling_x, seq_params->subsampling_y))
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Referenced frame has incompatible color format");
  }
  setup_buffer_pool(cm);
}

static AOM_INLINE void read_tile_info_max_tile(
    AV1_COMMON *const cm, struct aom_read_bit_buffer *const rb) {
  CommonTileParams *const tiles = &cm->tiles;
  int width_mi = ALIGN_POWER_OF_TWO(cm->mi_params.mi_cols, cm->mib_size_log2);
  int height_mi = ALIGN_POWER_OF_TWO(cm->mi_params.mi_rows, cm->mib_size_log2);
  int width_sb = width_mi >> cm->mib_size_log2;
  int height_sb = height_mi >> cm->mib_size_log2;

  av1_get_tile_limits(cm);
  tiles->uniform_spacing = aom_rb_read_bit(rb);

  // Read tile columns
  if (tiles->uniform_spacing) {
    tiles->log2_cols = tiles->min_log2_cols;
    while (tiles->log2_cols < tiles->max_log2_cols) {
      if (!aom_rb_read_bit(rb)) {
        break;
      }
      tiles->log2_cols++;
    }
  } else {
    int i;
    int start_sb;
    for (i = 0, start_sb = 0; width_sb > 0 && i < MAX_TILE_COLS; i++) {
      const int size_sb =
          1 + rb_read_uniform(rb, AOMMIN(width_sb, tiles->max_width_sb));
      tiles->col_start_sb[i] = start_sb;
      start_sb += size_sb;
      width_sb -= size_sb;
    }
    tiles->cols = i;
    tiles->col_start_sb[i] = start_sb + width_sb;
  }
  av1_calculate_tile_cols(cm, cm->mi_params.mi_rows, cm->mi_params.mi_cols,
                          tiles);

  // Read tile rows
  if (tiles->uniform_spacing) {
    tiles->log2_rows = tiles->min_log2_rows;
    while (tiles->log2_rows < tiles->max_log2_rows) {
      if (!aom_rb_read_bit(rb)) {
        break;
      }
      tiles->log2_rows++;
    }
  } else {
    int i;
    int start_sb;
    for (i = 0, start_sb = 0; height_sb > 0 && i < MAX_TILE_ROWS; i++) {
      const int size_sb =
          1 + rb_read_uniform(rb, AOMMIN(height_sb, tiles->max_height_sb));
      tiles->row_start_sb[i] = start_sb;
      start_sb += size_sb;
      height_sb -= size_sb;
    }
    tiles->rows = i;
    tiles->row_start_sb[i] = start_sb + height_sb;
  }
  av1_calculate_tile_rows(cm, cm->mi_params.mi_rows, tiles);
}

void av1_set_single_tile_decoding_mode(AV1_COMMON *const cm) {
  cm->tiles.single_tile_decoding = 0;
  if (cm->tiles.large_scale) {
    struct loopfilter *lf = &cm->lf;
    RestorationInfo *const rst_info = cm->rst_info;
    const CdefInfo *const cdef_info = &cm->cdef_info;

    // Figure out single_tile_decoding by loopfilter_level.
    const int no_loopfilter = !(lf->filter_level[0] || lf->filter_level[1]);
    const int no_cdef = cdef_info->cdef_bits == 0 &&
                        cdef_info->cdef_strengths[0] == 0 &&
                        cdef_info->cdef_uv_strengths[0] == 0;
    const int no_restoration =
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
        rst_info[0].frame_cross_restoration_type == RESTORE_NONE &&
        rst_info[1].frame_cross_restoration_type == RESTORE_NONE &&
        rst_info[2].frame_cross_restoration_type == RESTORE_NONE &&
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
        rst_info[0].frame_restoration_type == RESTORE_NONE &&
        rst_info[1].frame_restoration_type == RESTORE_NONE &&
        rst_info[2].frame_restoration_type == RESTORE_NONE;
    assert(IMPLIES(cm->features.coded_lossless, no_loopfilter && no_cdef));
    assert(IMPLIES(cm->features.all_lossless, no_restoration));
    cm->tiles.single_tile_decoding = no_loopfilter && no_cdef && no_restoration;
  }
}

static AOM_INLINE void read_tile_info(AV1Decoder *const pbi,
                                      struct aom_read_bit_buffer *const rb) {
  AV1_COMMON *const cm = &pbi->common;

  read_tile_info_max_tile(cm, rb);

  pbi->context_update_tile_id = 0;
  if (cm->tiles.rows * cm->tiles.cols > 1) {
    // tile to use for cdf update
    pbi->context_update_tile_id =
        aom_rb_read_literal(rb, cm->tiles.log2_rows + cm->tiles.log2_cols);
    if (pbi->context_update_tile_id >= cm->tiles.rows * cm->tiles.cols) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid context_update_tile_id");
    }
    // tile size magnitude
    pbi->tile_size_bytes = aom_rb_read_literal(rb, 2) + 1;
  }
}

#if EXT_TILE_DEBUG
static AOM_INLINE void read_ext_tile_info(
    AV1Decoder *const pbi, struct aom_read_bit_buffer *const rb) {
  AV1_COMMON *const cm = &pbi->common;

  // This information is stored as a separate byte.
  int mod = rb->bit_offset % CHAR_BIT;
  if (mod > 0) aom_rb_read_literal(rb, CHAR_BIT - mod);
  assert(rb->bit_offset % CHAR_BIT == 0);

  if (cm->tiles.cols * cm->tiles.rows > 1) {
    // Read the number of bytes used to store tile size
    pbi->tile_col_size_bytes = aom_rb_read_literal(rb, 2) + 1;
    pbi->tile_size_bytes = aom_rb_read_literal(rb, 2) + 1;
  }
}
#endif  // EXT_TILE_DEBUG

static size_t mem_get_varsize(const uint8_t *src, int sz) {
  switch (sz) {
    case 1: return src[0];
    case 2: return mem_get_le16(src);
    case 3: return mem_get_le24(src);
    case 4: return mem_get_le32(src);
    default: assert(0 && "Invalid size"); return -1;
  }
}

#if EXT_TILE_DEBUG
// Reads the next tile returning its size and adjusting '*data' accordingly
// based on 'is_last'. On return, '*data' is updated to point to the end of the
// raw tile buffer in the bit stream.
static AOM_INLINE void get_ls_tile_buffer(
    const uint8_t *const data_end, struct aom_internal_error_info *error_info,
    const uint8_t **data, TileBufferDec (*const tile_buffers)[MAX_TILE_COLS],
    int tile_size_bytes, int col, int row, int tile_copy_mode) {
  size_t size;

  size_t copy_size = 0;
  const uint8_t *copy_data = NULL;

  if (!read_is_valid(*data, tile_size_bytes, data_end))
    aom_internal_error(error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Truncated packet or corrupt tile length");
  size = mem_get_varsize(*data, tile_size_bytes);

  // If tile_copy_mode = 1, then the top bit of the tile header indicates copy
  // mode.
  if (tile_copy_mode && (size >> (tile_size_bytes * 8 - 1)) == 1) {
    // The remaining bits in the top byte signal the row offset
    int offset = (size >> (tile_size_bytes - 1) * 8) & 0x7f;

    // Currently, only use tiles in same column as reference tiles.
    copy_data = tile_buffers[row - offset][col].data;
    copy_size = tile_buffers[row - offset][col].size;
    size = 0;
  } else {
    size += AV1_MIN_TILE_SIZE_BYTES;
  }

  *data += tile_size_bytes;

  if (size > (size_t)(data_end - *data))
    aom_internal_error(error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Truncated packet or corrupt tile size");

  if (size > 0) {
    tile_buffers[row][col].data = *data;
    tile_buffers[row][col].size = size;
  } else {
    tile_buffers[row][col].data = copy_data;
    tile_buffers[row][col].size = copy_size;
  }

  *data += size;
}

// Returns the end of the last tile buffer
// (tile_buffers[cm->tiles.rows - 1][cm->tiles.cols - 1]).
static const uint8_t *get_ls_tile_buffers(
    AV1Decoder *pbi, const uint8_t *data, const uint8_t *data_end,
    TileBufferDec (*const tile_buffers)[MAX_TILE_COLS]) {
  AV1_COMMON *const cm = &pbi->common;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  const int have_tiles = tile_cols * tile_rows > 1;
  const uint8_t *raw_data_end;  // The end of the last tile buffer

  if (!have_tiles) {
    const size_t tile_size = data_end - data;
    tile_buffers[0][0].data = data;
    tile_buffers[0][0].size = tile_size;
    raw_data_end = NULL;
  } else {
    // We locate only the tile buffers that are required, which are the ones
    // specified by pbi->dec_tile_col and pbi->dec_tile_row. Also, we always
    // need the last (bottom right) tile buffer, as we need to know where the
    // end of the compressed frame buffer is for proper superframe decoding.

    const uint8_t *tile_col_data_end[MAX_TILE_COLS] = { NULL };
    const uint8_t *const data_start = data;

    const int dec_tile_row = AOMMIN(pbi->dec_tile_row, tile_rows);
    const int single_row = pbi->dec_tile_row >= 0;
    const int tile_rows_start = single_row ? dec_tile_row : 0;
    const int tile_rows_end = single_row ? tile_rows_start + 1 : tile_rows;
    const int dec_tile_col = AOMMIN(pbi->dec_tile_col, tile_cols);
    const int single_col = pbi->dec_tile_col >= 0;
    const int tile_cols_start = single_col ? dec_tile_col : 0;
    const int tile_cols_end = single_col ? tile_cols_start + 1 : tile_cols;

    const int tile_col_size_bytes = pbi->tile_col_size_bytes;
    const int tile_size_bytes = pbi->tile_size_bytes;
    int tile_width, tile_height;
    av1_get_uniform_tile_size(cm, &tile_width, &tile_height);
    const int tile_copy_mode =
        ((AOMMAX(tile_width, tile_height) << MI_SIZE_LOG2) <= 256) ? 1 : 0;
    // Read tile column sizes for all columns (we need the last tile buffer)
    for (int c = 0; c < tile_cols; ++c) {
      const int is_last = c == tile_cols - 1;
      size_t tile_col_size;

      if (!is_last) {
        tile_col_size = mem_get_varsize(data, tile_col_size_bytes);
        data += tile_col_size_bytes;
        tile_col_data_end[c] = data + tile_col_size;
      } else {
        tile_col_size = data_end - data;
        tile_col_data_end[c] = data_end;
      }
      data += tile_col_size;
    }

    data = data_start;

    // Read the required tile sizes.
    for (int c = tile_cols_start; c < tile_cols_end; ++c) {
      const int is_last = c == tile_cols - 1;

      if (c > 0) data = tile_col_data_end[c - 1];

      if (!is_last) data += tile_col_size_bytes;

      // Get the whole of the last column, otherwise stop at the required tile.
      for (int r = 0; r < (is_last ? tile_rows : tile_rows_end); ++r) {
        get_ls_tile_buffer(tile_col_data_end[c], &pbi->common.error, &data,
                           tile_buffers, tile_size_bytes, c, r, tile_copy_mode);
      }
    }

    // If we have not read the last column, then read it to get the last tile.
    if (tile_cols_end != tile_cols) {
      const int c = tile_cols - 1;

      data = tile_col_data_end[c - 1];

      for (int r = 0; r < tile_rows; ++r) {
        get_ls_tile_buffer(tile_col_data_end[c], &pbi->common.error, &data,
                           tile_buffers, tile_size_bytes, c, r, tile_copy_mode);
      }
    }
    raw_data_end = data;
  }
  return raw_data_end;
}
#endif  // EXT_TILE_DEBUG

static const uint8_t *get_ls_single_tile_buffer(
    AV1Decoder *pbi, const uint8_t *data,
    TileBufferDec (*const tile_buffers)[MAX_TILE_COLS]) {
  assert(pbi->dec_tile_row >= 0 && pbi->dec_tile_col >= 0);
  tile_buffers[pbi->dec_tile_row][pbi->dec_tile_col].data = data;
  tile_buffers[pbi->dec_tile_row][pbi->dec_tile_col].size =
      (size_t)pbi->coded_tile_data_size;
  return data + pbi->coded_tile_data_size;
}

// Reads the next tile returning its size and adjusting '*data' accordingly
// based on 'is_last'.
static AOM_INLINE void get_tile_buffer(
    const uint8_t *const data_end, const int tile_size_bytes, int is_last,
    struct aom_internal_error_info *error_info, const uint8_t **data,
    TileBufferDec *const buf) {
  size_t size;

  if (!is_last) {
    if (!read_is_valid(*data, tile_size_bytes, data_end))
      aom_internal_error(error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Not enough data to read tile size");

    size = mem_get_varsize(*data, tile_size_bytes) + AV1_MIN_TILE_SIZE_BYTES;
    *data += tile_size_bytes;

    if (size > (size_t)(data_end - *data))
      aom_internal_error(error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Truncated packet or corrupt tile size");
  } else {
    size = data_end - *data;
  }

  buf->data = *data;
  buf->size = size;

  *data += size;
}

static AOM_INLINE void get_tile_buffers(
    AV1Decoder *pbi, const uint8_t *data, const uint8_t *data_end,
    TileBufferDec (*const tile_buffers)[MAX_TILE_COLS], int start_tile,
    int end_tile) {
  AV1_COMMON *const cm = &pbi->common;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;
  int tc = 0;

  for (int r = 0; r < tile_rows; ++r) {
    for (int c = 0; c < tile_cols; ++c, ++tc) {
      TileBufferDec *const buf = &tile_buffers[r][c];

      const int is_last = (tc == end_tile);
      const size_t hdr_offset = 0;

      if (tc < start_tile || tc > end_tile) continue;

      if (data + hdr_offset >= data_end)
        aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                           "Data ended before all tiles were read.");
      data += hdr_offset;
      get_tile_buffer(data_end, pbi->tile_size_bytes, is_last,
                      &pbi->common.error, &data, buf);
    }
  }
}

static AOM_INLINE void set_cb_buffer(AV1Decoder *pbi, DecoderCodingBlock *dcb,
                                     CB_BUFFER *cb_buffer_base,
                                     const int num_planes, int mi_row,
                                     int mi_col) {
  AV1_COMMON *const cm = &pbi->common;
  int mib_size_log2 = cm->mib_size_log2;
  int stride = (cm->mi_params.mi_cols >> mib_size_log2) + 1;
  int offset = (mi_row >> mib_size_log2) * stride + (mi_col >> mib_size_log2);
  CB_BUFFER *cb_buffer = cb_buffer_base + offset;

  for (int plane = 0; plane < num_planes; ++plane) {
    dcb->dqcoeff_block[plane] = cb_buffer->dqcoeff[plane];
#if CONFIG_INSPECTION
    dcb->dqcoeff_block_copy[plane] = cb_buffer->dqcoeff_copy[plane];
    dcb->qcoeff_block[plane] = cb_buffer->qcoeff[plane];
    dcb->dequant_values[plane] = cb_buffer->dequant_values[plane];
#endif  // CONFIG_INSPECTION
    dcb->eob_data[plane] = cb_buffer->eob_data[plane];
    dcb->bob_data[plane] = cb_buffer->bob_data[plane];
    dcb->cb_offset[plane] = 0;
    dcb->txb_offset[plane] = 0;
  }
  MACROBLOCKD *const xd = &dcb->xd;
  xd->plane[0].color_index_map = cb_buffer->color_index_map[0];
  xd->plane[1].color_index_map = cb_buffer->color_index_map[1];
  xd->color_index_map_offset[0] = 0;
  xd->color_index_map_offset[1] = 0;
}

static AOM_INLINE void decoder_alloc_tile_data(AV1Decoder *pbi,
                                               const int n_tiles) {
  AV1_COMMON *const cm = &pbi->common;
  aom_free(pbi->tile_data);
  CHECK_MEM_ERROR(cm, pbi->tile_data,
                  aom_memalign(32, n_tiles * sizeof(*pbi->tile_data)));
  pbi->allocated_tiles = n_tiles;
  for (int i = 0; i < n_tiles; i++) {
    TileDataDec *const tile_data = pbi->tile_data + i;
    av1_zero(tile_data->dec_row_mt_sync);
  }
  pbi->allocated_row_mt_sync_rows = 0;
}

// Set up nsync by width.
static INLINE int get_sync_range(int width) {
// nsync numbers are picked by testing.
#if 0
  if (width < 640)
    return 1;
  else if (width <= 1280)
    return 2;
  else if (width <= 4096)
    return 4;
  else
    return 8;
#else
  (void)width;
#endif
  return 1;
}

// Allocate memory for decoder row synchronization
static AOM_INLINE void dec_row_mt_alloc(AV1DecRowMTSync *dec_row_mt_sync,
                                        AV1_COMMON *cm, int rows) {
  dec_row_mt_sync->allocated_sb_rows = rows;
#if CONFIG_MULTITHREAD
  {
    int i;

    CHECK_MEM_ERROR(cm, dec_row_mt_sync->mutex_,
                    aom_malloc(sizeof(*(dec_row_mt_sync->mutex_)) * rows));
    if (dec_row_mt_sync->mutex_) {
      for (i = 0; i < rows; ++i) {
        pthread_mutex_init(&dec_row_mt_sync->mutex_[i], NULL);
      }
    }

    CHECK_MEM_ERROR(cm, dec_row_mt_sync->cond_,
                    aom_malloc(sizeof(*(dec_row_mt_sync->cond_)) * rows));
    if (dec_row_mt_sync->cond_) {
      for (i = 0; i < rows; ++i) {
        pthread_cond_init(&dec_row_mt_sync->cond_[i], NULL);
      }
    }
  }
#endif  // CONFIG_MULTITHREAD

  CHECK_MEM_ERROR(cm, dec_row_mt_sync->cur_sb_col,
                  aom_malloc(sizeof(*(dec_row_mt_sync->cur_sb_col)) * rows));

  // Set up nsync.
  dec_row_mt_sync->sync_range = get_sync_range(cm->width);
}

// Deallocate decoder row synchronization related mutex and data
void av1_dec_row_mt_dealloc(AV1DecRowMTSync *dec_row_mt_sync) {
  if (dec_row_mt_sync != NULL) {
#if CONFIG_MULTITHREAD
    int i;
    if (dec_row_mt_sync->mutex_ != NULL) {
      for (i = 0; i < dec_row_mt_sync->allocated_sb_rows; ++i) {
        pthread_mutex_destroy(&dec_row_mt_sync->mutex_[i]);
      }
      aom_free(dec_row_mt_sync->mutex_);
    }
    if (dec_row_mt_sync->cond_ != NULL) {
      for (i = 0; i < dec_row_mt_sync->allocated_sb_rows; ++i) {
        pthread_cond_destroy(&dec_row_mt_sync->cond_[i]);
      }
      aom_free(dec_row_mt_sync->cond_);
    }
#endif  // CONFIG_MULTITHREAD
    aom_free(dec_row_mt_sync->cur_sb_col);

    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av1_zero(*dec_row_mt_sync);
  }
}

static INLINE void sync_read(AV1DecRowMTSync *const dec_row_mt_sync, int r,
                             int c) {
#if CONFIG_MULTITHREAD
  const int nsync = dec_row_mt_sync->sync_range;

  if (r && !(c & (nsync - 1))) {
    pthread_mutex_t *const mutex = &dec_row_mt_sync->mutex_[r - 1];
    pthread_mutex_lock(mutex);

    while (c > dec_row_mt_sync->cur_sb_col[r - 1] - nsync) {
      pthread_cond_wait(&dec_row_mt_sync->cond_[r - 1], mutex);
    }
    pthread_mutex_unlock(mutex);
  }
#else
  (void)dec_row_mt_sync;
  (void)r;
  (void)c;
#endif  // CONFIG_MULTITHREAD
}

static INLINE void sync_write(AV1DecRowMTSync *const dec_row_mt_sync, int r,
                              int c, const int sb_cols) {
#if CONFIG_MULTITHREAD
  const int nsync = dec_row_mt_sync->sync_range;
  int cur;
  int sig = 1;

  if (c < sb_cols - 1) {
    cur = c;
    if (c % nsync) sig = 0;
  } else {
    cur = sb_cols + nsync;
  }

  if (sig) {
    pthread_mutex_lock(&dec_row_mt_sync->mutex_[r]);

    dec_row_mt_sync->cur_sb_col[r] = cur;

    pthread_cond_signal(&dec_row_mt_sync->cond_[r]);
    pthread_mutex_unlock(&dec_row_mt_sync->mutex_[r]);
  }
#else
  (void)dec_row_mt_sync;
  (void)r;
  (void)c;
  (void)sb_cols;
#endif  // CONFIG_MULTITHREAD
}

static AOM_INLINE void decode_tile_sb_row(AV1Decoder *pbi, ThreadData *const td,
                                          TileInfo tile_info,
                                          const int mi_row) {
  AV1_COMMON *const cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);
  TileDataDec *const tile_data =
      pbi->tile_data + tile_info.tile_row * cm->tiles.cols + tile_info.tile_col;
  const int sb_cols_in_tile = av1_get_sb_cols_in_tile(cm, tile_info);
  const int sb_row_in_tile =
      (mi_row - tile_info.mi_row_start) >> cm->mib_size_log2;
  int sb_col_in_tile = 0;

  for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
       mi_col += cm->mib_size, sb_col_in_tile++) {
    av1_reset_is_mi_coded_map(&td->dcb.xd, cm->mib_size);
    td->dcb.xd.sbi = av1_get_sb_info(cm, mi_row, mi_col);
    set_cb_buffer(pbi, &td->dcb, pbi->cb_buffer_base, num_planes, mi_row,
                  mi_col);

    sync_read(&tile_data->dec_row_mt_sync, sb_row_in_tile, sb_col_in_tile);

    DecoderCodingBlock *const dcb = &td->dcb;
    MACROBLOCKD *const xd = &dcb->xd;

    xd->ref_mv_bank.rmb_sb_hits = 0;

#if CONFIG_EXTENDED_WARP_PREDICTION
    xd->warp_param_bank.wpb_sb_hits = 0;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

    // Decoding of the super-block
    decode_partition_sb(pbi, td, mi_row, mi_col, td->bit_reader, cm->sb_size,
                        0x2);

    sync_write(&tile_data->dec_row_mt_sync, sb_row_in_tile, sb_col_in_tile,
               sb_cols_in_tile);
  }
}

static int check_trailing_bits_after_symbol_coder(aom_reader *r) {
  if (aom_reader_has_overflowed(r)) return -1;

  uint32_t nb_bits = aom_reader_tell(r);
  uint32_t nb_bytes = (nb_bits + 7) >> 3;
  const uint8_t *p = aom_reader_find_begin(r) + nb_bytes;

  // aom_reader_tell() returns 1 for a newly initialized decoder, and the
  // return value only increases as values are decoded. So nb_bits > 0, and
  // thus p > p_begin. Therefore accessing p[-1] is safe.
  uint8_t last_byte = p[-1];
  uint8_t pattern = 128 >> ((nb_bits - 1) & 7);
  if ((last_byte & (2 * pattern - 1)) != pattern) return -1;

  // Make sure that all padding bytes are zero as required by the spec.
  const uint8_t *p_end = aom_reader_find_end(r);
  while (p < p_end) {
    if (*p != 0) return -1;
    p++;
  }
  return 0;
}

static AOM_INLINE void set_decode_func_pointers(ThreadData *td,
                                                int parse_decode_flag) {
  td->read_coeffs_tx_intra_block_visit = decode_block_void;
  td->predict_and_recon_intra_block_visit = decode_block_void;
  td->read_coeffs_tx_inter_block_visit = decode_block_void;
  td->inverse_tx_inter_block_visit = decode_block_void;
  td->inverse_cctx_block_visit = decode_block_void;
  td->predict_inter_block_visit = predict_inter_block_void;
  td->cfl_store_inter_block_visit = cfl_store_inter_block_void;

  if (parse_decode_flag & 0x1) {
    td->read_coeffs_tx_intra_block_visit = read_coeffs_tx_intra_block;
    td->read_coeffs_tx_inter_block_visit = av1_read_coeffs_txb_facade;
  }
  if (parse_decode_flag & 0x2) {
    td->predict_and_recon_intra_block_visit =
        predict_and_reconstruct_intra_block;
    td->inverse_tx_inter_block_visit = inverse_transform_inter_block;
    td->inverse_cctx_block_visit = inverse_cross_chroma_transform_block;
    td->predict_inter_block_visit = predict_inter_block;
    td->cfl_store_inter_block_visit = cfl_store_inter_block;
  }
}

static AOM_INLINE void decode_tile(AV1Decoder *pbi, ThreadData *const td,
                                   int tile_row, int tile_col) {
  TileInfo tile_info;

  AV1_COMMON *const cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);

  av1_tile_set_row(&tile_info, cm, tile_row);
  av1_tile_set_col(&tile_info, cm, tile_col);
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;

  av1_zero_above_context(cm, xd, tile_info.mi_col_start, tile_info.mi_col_end,
                         tile_row);
  av1_reset_loop_filter_delta(xd, num_planes);
#if CONFIG_WIENER_NONSEP
  int num_filter_classes[MAX_MB_PLANE];
  for (int p = 0; p < num_planes; ++p)
    num_filter_classes[p] = cm->rst_info[p].num_filter_classes;
#endif  // CONFIG_WIENER_NONSEP
  av1_reset_loop_restoration(xd, 0, num_planes
#if CONFIG_WIENER_NONSEP
                             ,
                             num_filter_classes
#endif  // CONFIG_WIENER_NONSEP
  );

  for (int mi_row = tile_info.mi_row_start; mi_row < tile_info.mi_row_end;
       mi_row += cm->mib_size) {
    av1_zero_left_context(xd);
    av1_zero(xd->ref_mv_bank);
#if !CONFIG_MVP_IMPROVEMENT
    xd->ref_mv_bank_pt = &td->ref_mv_bank;
#endif

#if CONFIG_EXTENDED_WARP_PREDICTION
    av1_zero(xd->warp_param_bank);
#if !WARP_CU_BANK
    xd->warp_param_bank_pt = &td->warp_param_bank;
#endif  //! WARP_CU_BANK
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

    for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
         mi_col += cm->mib_size) {
      av1_reset_is_mi_coded_map(xd, cm->mib_size);
      av1_set_sb_info(cm, xd, mi_row, mi_col);
      set_cb_buffer(pbi, dcb, &td->cb_buffer_base, num_planes, 0, 0);
      // td->ref_mv_bank is initialized as xd->ref_mv_bank, and used
      // for MV referencing during decoding the tile.
      // xd->ref_mv_bank is updated as decoding goes.
      xd->ref_mv_bank.rmb_sb_hits = 0;
#if !CONFIG_MVP_IMPROVEMENT
      td->ref_mv_bank = xd->ref_mv_bank;
#endif  // !CONFIG_MVP_IMPROVEMENT

#if CONFIG_EXTENDED_WARP_PREDICTION
      xd->warp_param_bank.wpb_sb_hits = 0;
#if !WARP_CU_BANK
      td->warp_param_bank = xd->warp_param_bank;
#endif  //! WARP_CU_BANK
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
      decode_partition_sb(pbi, td, mi_row, mi_col, td->bit_reader, cm->sb_size,
                          0x3);

      if (aom_reader_has_overflowed(td->bit_reader)) {
        aom_merge_corrupted_flag(&dcb->corrupted, 1);
        return;
      }
    }
  }

  int corrupted =
      (check_trailing_bits_after_symbol_coder(td->bit_reader)) ? 1 : 0;
  aom_merge_corrupted_flag(&dcb->corrupted, corrupted);
}

#if CONFIG_THROUGHPUT_ANALYSIS
static void aom_accounting_cal_total(AV1Decoder *pbi) {
  if (pbi->decoding_first_frame) {
    pbi->common.sym_stats.frame_dec_order = 0;
    pbi->common.sym_stats.tot_ctx_syms = 0;
    pbi->common.sym_stats.tot_bypass_syms = 0;
    pbi->common.sym_stats.tot_bits = 0;
    pbi->common.sym_stats.peak_ctx_syms = 0;
    pbi->common.sym_stats.peak_bypass_syms = 0;
    pbi->common.sym_stats.peak_bits = 0;
  }
  Accounting accounting = pbi->accounting;
  int64_t frm_ctx_syms = accounting.syms.num_ctx_coded;
  int64_t frm_bypass_syms = accounting.syms.num_bypass_coded;
  int64_t frm_bits = 0;
  for (int i = 0; i < accounting.syms.num_syms; i++) {
    AccountingSymbol sym = accounting.syms.syms[i];
    frm_bits += sym.bits;
  }
  int64_t peak_ctx_syms = pbi->common.sym_stats.peak_ctx_syms;
  int64_t peak_bypass_syms = pbi->common.sym_stats.peak_bypass_syms;
  pbi->common.sym_stats.tot_ctx_syms += frm_ctx_syms;
  pbi->common.sym_stats.tot_bypass_syms += frm_bypass_syms;
  pbi->common.sym_stats.frame_dec_order += 1;
  pbi->common.sym_stats.tot_bits += frm_bits;
  if (frm_ctx_syms * 4 + frm_bypass_syms >
      peak_ctx_syms * 4 + peak_bypass_syms) {
    pbi->common.sym_stats.peak_ctx_syms = frm_ctx_syms;
    pbi->common.sym_stats.peak_bypass_syms = frm_bypass_syms;
    pbi->common.sym_stats.peak_bits = frm_bits;
  }
  tot_ctx_syms = pbi->common.sym_stats.tot_ctx_syms;
  tot_bypass_syms = pbi->common.sym_stats.tot_bypass_syms;
  tot_bits = pbi->common.sym_stats.tot_bits;
  max_ctx_syms = pbi->common.sym_stats.peak_ctx_syms;
  max_bypass_syms = pbi->common.sym_stats.peak_bypass_syms;
  max_bits = pbi->common.sym_stats.peak_bits;
  tot_frames = pbi->common.sym_stats.frame_dec_order;
}
#endif  // CONFIG_THROUGHPUT_ANALYSIS

static const uint8_t *decode_tiles(AV1Decoder *pbi, const uint8_t *data,
                                   const uint8_t *data_end, int start_tile,
                                   int end_tile) {
  AV1_COMMON *const cm = &pbi->common;
  ThreadData *const td = &pbi->td;
  CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  const int n_tiles = tile_cols * tile_rows;
  TileBufferDec(*const tile_buffers)[MAX_TILE_COLS] = pbi->tile_buffers;
  const int dec_tile_row = AOMMIN(pbi->dec_tile_row, tile_rows);
  const int single_row = pbi->dec_tile_row >= 0;
  const int dec_tile_col = AOMMIN(pbi->dec_tile_col, tile_cols);
  const int single_col = pbi->dec_tile_col >= 0;
  int tile_rows_start;
  int tile_rows_end;
  int tile_cols_start;
  int tile_cols_end;
  int inv_col_order;
  int inv_row_order;
  int tile_row, tile_col;
  uint8_t allow_update_cdf;
  const uint8_t *raw_data_end = NULL;

  if (tiles->large_scale) {
    tile_rows_start = single_row ? dec_tile_row : 0;
    tile_rows_end = single_row ? dec_tile_row + 1 : tile_rows;
    tile_cols_start = single_col ? dec_tile_col : 0;
    tile_cols_end = single_col ? tile_cols_start + 1 : tile_cols;
    inv_col_order = pbi->inv_tile_order && !single_col;
    inv_row_order = pbi->inv_tile_order && !single_row;
    allow_update_cdf = 0;
  } else {
    tile_rows_start = 0;
    tile_rows_end = tile_rows;
    tile_cols_start = 0;
    tile_cols_end = tile_cols;
    inv_col_order = pbi->inv_tile_order;
    inv_row_order = pbi->inv_tile_order;
    allow_update_cdf = 1;
  }

  // No tiles to decode.
  if (tile_rows_end <= tile_rows_start || tile_cols_end <= tile_cols_start ||
      // First tile is larger than end_tile.
      tile_rows_start * tiles->cols + tile_cols_start > end_tile ||
      // Last tile is smaller than start_tile.
      (tile_rows_end - 1) * tiles->cols + tile_cols_end - 1 < start_tile)
    return data;

  allow_update_cdf = allow_update_cdf && !cm->features.disable_cdf_update;

  assert(tile_rows <= MAX_TILE_ROWS);
  assert(tile_cols <= MAX_TILE_COLS);

#if EXT_TILE_DEBUG
  if (tiles->large_scale && !pbi->ext_tile_debug)
    raw_data_end = get_ls_single_tile_buffer(pbi, data, tile_buffers);
  else if (tiles->large_scale && pbi->ext_tile_debug)
    raw_data_end = get_ls_tile_buffers(pbi, data, data_end, tile_buffers);
  else
#endif  // EXT_TILE_DEBUG
    get_tile_buffers(pbi, data, data_end, tile_buffers, start_tile, end_tile);

  if (pbi->tile_data == NULL || n_tiles != pbi->allocated_tiles) {
    decoder_alloc_tile_data(pbi, n_tiles);
  }
#if CONFIG_ACCOUNTING
  if (pbi->acct_enabled) {
    aom_accounting_reset(&pbi->accounting);
  }
#endif

  set_decode_func_pointers(&pbi->td, 0x3);

  // Load all tile information into thread_data.
  td->dcb = pbi->dcb;

  td->dcb.corrupted = 0;
  td->dcb.mc_buf[0] = td->mc_buf[0];
  td->dcb.mc_buf[1] = td->mc_buf[1];
  td->dcb.xd.tmp_conv_dst = td->tmp_conv_dst;
  for (int j = 0; j < 2; ++j) {
    td->dcb.xd.tmp_obmc_bufs[j] = td->tmp_obmc_bufs[j];
  }

  for (tile_row = tile_rows_start; tile_row < tile_rows_end; ++tile_row) {
    const int row = inv_row_order ? tile_rows - 1 - tile_row : tile_row;

    for (tile_col = tile_cols_start; tile_col < tile_cols_end; ++tile_col) {
      const int col = inv_col_order ? tile_cols - 1 - tile_col : tile_col;
      TileDataDec *const tile_data = pbi->tile_data + row * tiles->cols + col;
      const TileBufferDec *const tile_bs_buf = &tile_buffers[row][col];

      if (row * tiles->cols + col < start_tile ||
          row * tiles->cols + col > end_tile)
        continue;

      td->bit_reader = &tile_data->bit_reader;
      // av1_zero(td->cb_buffer_base.dqcoeff);
      av1_tile_init(&td->dcb.xd.tile, cm, row, col);
      td->dcb.xd.current_base_qindex = cm->quant_params.base_qindex;
      setup_bool_decoder(tile_bs_buf->data, data_end, tile_bs_buf->size,
                         &cm->error, td->bit_reader, allow_update_cdf);
#if CONFIG_ACCOUNTING
      if (pbi->acct_enabled) {
        td->bit_reader->accounting = &pbi->accounting;
        td->bit_reader->accounting->last_tell_frac =
            aom_reader_tell_frac(td->bit_reader);
      } else {
        td->bit_reader->accounting = NULL;
      }
#endif
      av1_init_macroblockd(cm, &td->dcb.xd);
      av1_init_above_context(&cm->above_contexts, av1_num_planes(cm), row,
                             &td->dcb.xd);

      // Initialise the tile context from the frame context
      tile_data->tctx = *cm->fc;
      td->dcb.xd.tile_ctx = &tile_data->tctx;

      // decode tile
      decode_tile(pbi, td, row, col);
      aom_merge_corrupted_flag(&pbi->dcb.corrupted, td->dcb.corrupted);
      if (pbi->dcb.corrupted)
        aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                           "Failed to decode tile data");
    }
  }

  if (tiles->large_scale) {
    if (n_tiles == 1) {
      // Find the end of the single tile buffer
      return aom_reader_find_end(&pbi->tile_data->bit_reader);
    }
    // Return the end of the last tile buffer
    return raw_data_end;
  }
  TileDataDec *const tile_data = pbi->tile_data + end_tile;
#if CONFIG_THROUGHPUT_ANALYSIS
  if (pbi->acct_enabled) {
    aom_accounting_cal_total(pbi);
  }
#endif  // CONFIG_THROUGHPUT_ANALYSIS
  return aom_reader_find_end(&tile_data->bit_reader);
}

static TileJobsDec *get_dec_job_info(AV1DecTileMT *tile_mt_info) {
  TileJobsDec *cur_job_info = NULL;
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(tile_mt_info->job_mutex);

  if (tile_mt_info->jobs_dequeued < tile_mt_info->jobs_enqueued) {
    cur_job_info = tile_mt_info->job_queue + tile_mt_info->jobs_dequeued;
    tile_mt_info->jobs_dequeued++;
  }

  pthread_mutex_unlock(tile_mt_info->job_mutex);
#else
  (void)tile_mt_info;
#endif
  return cur_job_info;
}

static AOM_INLINE void tile_worker_hook_init(
    AV1Decoder *const pbi, DecWorkerData *const thread_data,
    const TileBufferDec *const tile_buffer, TileDataDec *const tile_data,
    uint8_t allow_update_cdf) {
  AV1_COMMON *cm = &pbi->common;
  ThreadData *const td = thread_data->td;
  int tile_row = tile_data->tile_info.tile_row;
  int tile_col = tile_data->tile_info.tile_col;

  td->bit_reader = &tile_data->bit_reader;
  av1_zero(td->cb_buffer_base.dqcoeff);

  MACROBLOCKD *const xd = &td->dcb.xd;
  av1_tile_init(&xd->tile, cm, tile_row, tile_col);
  xd->current_base_qindex = cm->quant_params.base_qindex;
  setup_bool_decoder(tile_buffer->data, thread_data->data_end,
                     tile_buffer->size, &thread_data->error_info,
                     td->bit_reader, allow_update_cdf);
#if CONFIG_ACCOUNTING
  if (pbi->acct_enabled) {
    td->bit_reader->accounting = &pbi->accounting;
    td->bit_reader->accounting->last_tell_frac =
        aom_reader_tell_frac(td->bit_reader);
  } else {
    td->bit_reader->accounting = NULL;
  }
#endif
  av1_init_macroblockd(cm, xd);
  xd->error_info = &thread_data->error_info;
  av1_init_above_context(&cm->above_contexts, av1_num_planes(cm), tile_row, xd);

  // Initialise the tile context from the frame context
  tile_data->tctx = *cm->fc;
  xd->tile_ctx = &tile_data->tctx;
#if CONFIG_ACCOUNTING
  if (pbi->acct_enabled) {
    tile_data->bit_reader.accounting->last_tell_frac =
        aom_reader_tell_frac(&tile_data->bit_reader);
  }
#endif
}

static int tile_worker_hook(void *arg1, void *arg2) {
  DecWorkerData *const thread_data = (DecWorkerData *)arg1;
  AV1Decoder *const pbi = (AV1Decoder *)arg2;
  AV1_COMMON *cm = &pbi->common;
  ThreadData *const td = thread_data->td;
  uint8_t allow_update_cdf;

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(thread_data->error_info.jmp)) {
    thread_data->error_info.setjmp = 0;
    thread_data->td->dcb.corrupted = 1;
    return 0;
  }
  thread_data->error_info.setjmp = 1;

  allow_update_cdf = cm->tiles.large_scale ? 0 : 1;
  allow_update_cdf = allow_update_cdf && !cm->features.disable_cdf_update;

  set_decode_func_pointers(td, 0x3);

  assert(cm->tiles.cols > 0);
  while (!td->dcb.corrupted) {
    TileJobsDec *cur_job_info = get_dec_job_info(&pbi->tile_mt_info);

    if (cur_job_info != NULL) {
      const TileBufferDec *const tile_buffer = cur_job_info->tile_buffer;
      TileDataDec *const tile_data = cur_job_info->tile_data;
      tile_worker_hook_init(pbi, thread_data, tile_buffer, tile_data,
                            allow_update_cdf);
      // decode tile
      int tile_row = tile_data->tile_info.tile_row;
      int tile_col = tile_data->tile_info.tile_col;
      decode_tile(pbi, td, tile_row, tile_col);
    } else {
      break;
    }
  }
  thread_data->error_info.setjmp = 0;
  return !td->dcb.corrupted;
}

static INLINE int get_max_row_mt_workers_per_tile(AV1_COMMON *cm,
                                                  TileInfo tile) {
  // NOTE: Currently value of max workers is calculated based
  // on the parse and decode time. As per the theoretical estimate
  // when percentage of parse time is equal to percentage of decode
  // time, number of workers needed to parse + decode a tile can not
  // exceed more than 2.
  // TODO(any): Modify this value if parsing is optimized in future.
  int sb_rows = av1_get_sb_rows_in_tile(cm, tile);
  int max_workers =
      sb_rows == 1 ? AOM_MIN_THREADS_PER_TILE : AOM_MAX_THREADS_PER_TILE;
  return max_workers;
}

// The caller must hold pbi->row_mt_mutex_ when calling this function.
// Returns 1 if either the next job is stored in *next_job_info or 1 is stored
// in *end_of_frame.
// NOTE: The caller waits on pbi->row_mt_cond_ if this function returns 0.
// The return value of this function depends on the following variables:
// - frame_row_mt_info->mi_rows_parse_done
// - frame_row_mt_info->mi_rows_decode_started
// - frame_row_mt_info->row_mt_exit
// Therefore we may need to signal or broadcast pbi->row_mt_cond_ if any of
// these variables is modified.
static int get_next_job_info(AV1Decoder *const pbi,
                             AV1DecRowMTJobInfo *next_job_info,
                             int *end_of_frame) {
  AV1_COMMON *cm = &pbi->common;
  TileDataDec *tile_data;
  AV1DecRowMTSync *dec_row_mt_sync;
  AV1DecRowMTInfo *frame_row_mt_info = &pbi->frame_row_mt_info;
  TileInfo tile_info;
  const int tile_rows_start = frame_row_mt_info->tile_rows_start;
  const int tile_rows_end = frame_row_mt_info->tile_rows_end;
  const int tile_cols_start = frame_row_mt_info->tile_cols_start;
  const int tile_cols_end = frame_row_mt_info->tile_cols_end;
  const int start_tile = frame_row_mt_info->start_tile;
  const int end_tile = frame_row_mt_info->end_tile;
  const int sb_mi_size = mi_size_wide[cm->sb_size];
  int num_mis_to_decode, num_threads_working;
  int num_mis_waiting_for_decode;
  int min_threads_working = INT_MAX;
  int max_mis_to_decode = 0;
  int tile_row_idx, tile_col_idx;
  int tile_row = -1;
  int tile_col = -1;

  memset(next_job_info, 0, sizeof(*next_job_info));

  // Frame decode is completed or error is encountered.
  *end_of_frame = (frame_row_mt_info->mi_rows_decode_started ==
                   frame_row_mt_info->mi_rows_to_decode) ||
                  (frame_row_mt_info->row_mt_exit == 1);
  if (*end_of_frame) {
    return 1;
  }

  // Decoding cannot start as bit-stream parsing is not complete.
  assert(frame_row_mt_info->mi_rows_parse_done >=
         frame_row_mt_info->mi_rows_decode_started);
  if (frame_row_mt_info->mi_rows_parse_done ==
      frame_row_mt_info->mi_rows_decode_started)
    return 0;

  // Choose the tile to decode.
  for (tile_row_idx = tile_rows_start; tile_row_idx < tile_rows_end;
       ++tile_row_idx) {
    for (tile_col_idx = tile_cols_start; tile_col_idx < tile_cols_end;
         ++tile_col_idx) {
      if (tile_row_idx * cm->tiles.cols + tile_col_idx < start_tile ||
          tile_row_idx * cm->tiles.cols + tile_col_idx > end_tile)
        continue;

      tile_data = pbi->tile_data + tile_row_idx * cm->tiles.cols + tile_col_idx;
      dec_row_mt_sync = &tile_data->dec_row_mt_sync;

      num_threads_working = dec_row_mt_sync->num_threads_working;
      num_mis_waiting_for_decode = (dec_row_mt_sync->mi_rows_parse_done -
                                    dec_row_mt_sync->mi_rows_decode_started) *
                                   dec_row_mt_sync->mi_cols;
      num_mis_to_decode =
          (dec_row_mt_sync->mi_rows - dec_row_mt_sync->mi_rows_decode_started) *
          dec_row_mt_sync->mi_cols;

      assert(num_mis_to_decode >= num_mis_waiting_for_decode);

      // Pick the tile which has minimum number of threads working on it.
      if (num_mis_waiting_for_decode > 0) {
        if (num_threads_working < min_threads_working) {
          min_threads_working = num_threads_working;
          max_mis_to_decode = 0;
        }
        if (num_threads_working == min_threads_working &&
            num_mis_to_decode > max_mis_to_decode &&
            num_threads_working <
                get_max_row_mt_workers_per_tile(cm, tile_data->tile_info)) {
          max_mis_to_decode = num_mis_to_decode;
          tile_row = tile_row_idx;
          tile_col = tile_col_idx;
        }
      }
    }
  }
  // No job found to process
  if (tile_row == -1 || tile_col == -1) return 0;

  tile_data = pbi->tile_data + tile_row * cm->tiles.cols + tile_col;
  tile_info = tile_data->tile_info;
  dec_row_mt_sync = &tile_data->dec_row_mt_sync;

  next_job_info->tile_row = tile_row;
  next_job_info->tile_col = tile_col;
  next_job_info->mi_row =
      dec_row_mt_sync->mi_rows_decode_started + tile_info.mi_row_start;

  dec_row_mt_sync->num_threads_working++;
  dec_row_mt_sync->mi_rows_decode_started += sb_mi_size;
  frame_row_mt_info->mi_rows_decode_started += sb_mi_size;
  assert(frame_row_mt_info->mi_rows_parse_done >=
         frame_row_mt_info->mi_rows_decode_started);
#if CONFIG_MULTITHREAD
  if (frame_row_mt_info->mi_rows_decode_started ==
      frame_row_mt_info->mi_rows_to_decode) {
    pthread_cond_broadcast(pbi->row_mt_cond_);
  }
#endif

  return 1;
}

static INLINE void signal_parse_sb_row_done(AV1Decoder *const pbi,
                                            TileDataDec *const tile_data,
                                            const int sb_mi_size) {
  AV1DecRowMTInfo *frame_row_mt_info = &pbi->frame_row_mt_info;
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(pbi->row_mt_mutex_);
#endif
  assert(frame_row_mt_info->mi_rows_parse_done >=
         frame_row_mt_info->mi_rows_decode_started);
  tile_data->dec_row_mt_sync.mi_rows_parse_done += sb_mi_size;
  frame_row_mt_info->mi_rows_parse_done += sb_mi_size;
#if CONFIG_MULTITHREAD
  // A new decode job is available. Wake up one worker thread to handle the
  // new decode job.
  // NOTE: This assumes we bump mi_rows_parse_done and mi_rows_decode_started
  // by the same increment (sb_mi_size).
  pthread_cond_signal(pbi->row_mt_cond_);
  pthread_mutex_unlock(pbi->row_mt_mutex_);
#endif
}

// This function is very similar to decode_tile(). It would be good to figure
// out how to share code.
static AOM_INLINE void parse_tile_row_mt(AV1Decoder *pbi, ThreadData *const td,
                                         TileDataDec *const tile_data) {
  AV1_COMMON *const cm = &pbi->common;
  const int sb_mi_size = mi_size_wide[cm->sb_size];
  const int num_planes = av1_num_planes(cm);
  TileInfo tile_info = tile_data->tile_info;
  int tile_row = tile_info.tile_row;
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;

  av1_zero_above_context(cm, xd, tile_info.mi_col_start, tile_info.mi_col_end,
                         tile_row);
  av1_reset_loop_filter_delta(xd, num_planes);
#if CONFIG_WIENER_NONSEP
  int num_filter_classes[MAX_MB_PLANE];
  for (int p = 0; p < num_planes; ++p)
    num_filter_classes[p] = cm->rst_info[p].num_filter_classes;
#endif  // CONFIG_WIENER_NONSEP
  av1_reset_loop_restoration(xd, 0, num_planes
#if CONFIG_WIENER_NONSEP
                             ,
                             num_filter_classes
#endif  // CONFIG_WIENER_NONSEP
  );

  for (int mi_row = tile_info.mi_row_start; mi_row < tile_info.mi_row_end;
       mi_row += cm->mib_size) {
    av1_zero_left_context(xd);
    av1_zero(xd->ref_mv_bank);
#if !CONFIG_MVP_IMPROVEMENT
    xd->ref_mv_bank_pt = &td->ref_mv_bank;
#endif

#if CONFIG_EXTENDED_WARP_PREDICTION
    av1_zero(xd->warp_param_bank);
#if !WARP_CU_BANK
    xd->warp_param_bank_pt = &td->warp_param_bank;
#endif  //! WARP_CU_BANK
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

    for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
         mi_col += cm->mib_size) {
      av1_reset_is_mi_coded_map(xd, cm->mib_size);
      av1_set_sb_info(cm, xd, mi_row, mi_col);
      set_cb_buffer(pbi, dcb, pbi->cb_buffer_base, num_planes, mi_row, mi_col);

      xd->ref_mv_bank.rmb_sb_hits = 0;
#if !CONFIG_MVP_IMPROVEMENT
      td->ref_mv_bank = xd->ref_mv_bank;
#endif  // !CONFIG_MVP_IMPROVEMENT

#if CONFIG_EXTENDED_WARP_PREDICTION
      xd->warp_param_bank.wpb_sb_hits = 0;
#if !WARP_CU_BANK
      td->warp_param_bank = xd->warp_param_bank;
#endif  //! WARP_CU_BANK
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

      // Bit-stream parsing of the superblock
      decode_partition_sb(pbi, td, mi_row, mi_col, td->bit_reader, cm->sb_size,
                          0x1);
      if (aom_reader_has_overflowed(td->bit_reader)) {
        aom_merge_corrupted_flag(&dcb->corrupted, 1);
        return;
      }
    }
    signal_parse_sb_row_done(pbi, tile_data, sb_mi_size);
  }

  int corrupted =
      (check_trailing_bits_after_symbol_coder(td->bit_reader)) ? 1 : 0;
  aom_merge_corrupted_flag(&dcb->corrupted, corrupted);
}

static int row_mt_worker_hook(void *arg1, void *arg2) {
  DecWorkerData *const thread_data = (DecWorkerData *)arg1;
  AV1Decoder *const pbi = (AV1Decoder *)arg2;
  AV1_COMMON *cm = &pbi->common;
  ThreadData *const td = thread_data->td;
  uint8_t allow_update_cdf;
  AV1DecRowMTInfo *frame_row_mt_info = &pbi->frame_row_mt_info;
  td->dcb.corrupted = 0;

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(thread_data->error_info.jmp)) {
    thread_data->error_info.setjmp = 0;
    thread_data->td->dcb.corrupted = 1;
#if CONFIG_MULTITHREAD
    pthread_mutex_lock(pbi->row_mt_mutex_);
#endif
    frame_row_mt_info->row_mt_exit = 1;
#if CONFIG_MULTITHREAD
    pthread_cond_broadcast(pbi->row_mt_cond_);
    pthread_mutex_unlock(pbi->row_mt_mutex_);
#endif
    return 0;
  }
  thread_data->error_info.setjmp = 1;

  allow_update_cdf = cm->tiles.large_scale ? 0 : 1;
  allow_update_cdf = allow_update_cdf && !cm->features.disable_cdf_update;

  set_decode_func_pointers(td, 0x1);

  assert(cm->tiles.cols > 0);
  while (!td->dcb.corrupted) {
    TileJobsDec *cur_job_info = get_dec_job_info(&pbi->tile_mt_info);

    if (cur_job_info != NULL) {
      const TileBufferDec *const tile_buffer = cur_job_info->tile_buffer;
      TileDataDec *const tile_data = cur_job_info->tile_data;
      tile_worker_hook_init(pbi, thread_data, tile_buffer, tile_data,
                            allow_update_cdf);
#if CONFIG_MULTITHREAD
      pthread_mutex_lock(pbi->row_mt_mutex_);
#endif
      tile_data->dec_row_mt_sync.num_threads_working++;
#if CONFIG_MULTITHREAD
      pthread_mutex_unlock(pbi->row_mt_mutex_);
#endif
      // decode tile
      parse_tile_row_mt(pbi, td, tile_data);
#if CONFIG_MULTITHREAD
      pthread_mutex_lock(pbi->row_mt_mutex_);
#endif
      tile_data->dec_row_mt_sync.num_threads_working--;
#if CONFIG_MULTITHREAD
      pthread_mutex_unlock(pbi->row_mt_mutex_);
#endif
    } else {
      break;
    }
  }

  if (td->dcb.corrupted) {
    thread_data->error_info.setjmp = 0;
#if CONFIG_MULTITHREAD
    pthread_mutex_lock(pbi->row_mt_mutex_);
#endif
    frame_row_mt_info->row_mt_exit = 1;
#if CONFIG_MULTITHREAD
    pthread_cond_broadcast(pbi->row_mt_cond_);
    pthread_mutex_unlock(pbi->row_mt_mutex_);
#endif
    return 0;
  }

  set_decode_func_pointers(td, 0x2);

  while (1) {
    AV1DecRowMTJobInfo next_job_info;
    int end_of_frame = 0;

#if CONFIG_MULTITHREAD
    pthread_mutex_lock(pbi->row_mt_mutex_);
#endif
    while (!get_next_job_info(pbi, &next_job_info, &end_of_frame)) {
#if CONFIG_MULTITHREAD
      pthread_cond_wait(pbi->row_mt_cond_, pbi->row_mt_mutex_);
#endif
    }
#if CONFIG_MULTITHREAD
    pthread_mutex_unlock(pbi->row_mt_mutex_);
#endif

    if (end_of_frame) break;

    int tile_row = next_job_info.tile_row;
    int tile_col = next_job_info.tile_col;
    int mi_row = next_job_info.mi_row;

    TileDataDec *tile_data =
        pbi->tile_data + tile_row * cm->tiles.cols + tile_col;
    AV1DecRowMTSync *dec_row_mt_sync = &tile_data->dec_row_mt_sync;
    TileInfo tile_info = tile_data->tile_info;

    av1_tile_init(&td->dcb.xd.tile, cm, tile_row, tile_col);
    av1_init_macroblockd(cm, &td->dcb.xd);
    td->dcb.xd.error_info = &thread_data->error_info;

    decode_tile_sb_row(pbi, td, tile_info, mi_row);

#if CONFIG_MULTITHREAD
    pthread_mutex_lock(pbi->row_mt_mutex_);
#endif
    dec_row_mt_sync->num_threads_working--;
#if CONFIG_MULTITHREAD
    pthread_mutex_unlock(pbi->row_mt_mutex_);
#endif
  }
  thread_data->error_info.setjmp = 0;
  return !td->dcb.corrupted;
}

// sorts in descending order
static int compare_tile_buffers(const void *a, const void *b) {
  const TileJobsDec *const buf1 = (const TileJobsDec *)a;
  const TileJobsDec *const buf2 = (const TileJobsDec *)b;
  return (((int)buf2->tile_buffer->size) - ((int)buf1->tile_buffer->size));
}

static AOM_INLINE void enqueue_tile_jobs(AV1Decoder *pbi, AV1_COMMON *cm,
                                         int tile_rows_start, int tile_rows_end,
                                         int tile_cols_start, int tile_cols_end,
                                         int start_tile, int end_tile) {
  AV1DecTileMT *tile_mt_info = &pbi->tile_mt_info;
  TileJobsDec *tile_job_queue = tile_mt_info->job_queue;
  tile_mt_info->jobs_enqueued = 0;
  tile_mt_info->jobs_dequeued = 0;

  for (int row = tile_rows_start; row < tile_rows_end; row++) {
    for (int col = tile_cols_start; col < tile_cols_end; col++) {
      if (row * cm->tiles.cols + col < start_tile ||
          row * cm->tiles.cols + col > end_tile)
        continue;
      tile_job_queue->tile_buffer = &pbi->tile_buffers[row][col];
      tile_job_queue->tile_data = pbi->tile_data + row * cm->tiles.cols + col;
      tile_job_queue++;
      tile_mt_info->jobs_enqueued++;
    }
  }
}

static AOM_INLINE void alloc_dec_jobs(AV1DecTileMT *tile_mt_info,
                                      AV1_COMMON *cm, int tile_rows,
                                      int tile_cols) {
  tile_mt_info->alloc_tile_rows = tile_rows;
  tile_mt_info->alloc_tile_cols = tile_cols;
  int num_tiles = tile_rows * tile_cols;
#if CONFIG_MULTITHREAD
  {
    CHECK_MEM_ERROR(cm, tile_mt_info->job_mutex,
                    aom_malloc(sizeof(*tile_mt_info->job_mutex) * num_tiles));

    for (int i = 0; i < num_tiles; i++) {
      pthread_mutex_init(&tile_mt_info->job_mutex[i], NULL);
    }
  }
#endif
  CHECK_MEM_ERROR(cm, tile_mt_info->job_queue,
                  aom_malloc(sizeof(*tile_mt_info->job_queue) * num_tiles));
}

void av1_free_mc_tmp_buf(ThreadData *thread_data) {
  int ref;
  for (ref = 0; ref < 2; ref++) {
    aom_free(thread_data->mc_buf[ref]);
    thread_data->mc_buf[ref] = NULL;
  }
  thread_data->mc_buf_size = 0;

  aom_free(thread_data->tmp_conv_dst);
  thread_data->tmp_conv_dst = NULL;
  for (int i = 0; i < 2; ++i) {
    aom_free(thread_data->tmp_obmc_bufs[i]);
    thread_data->tmp_obmc_bufs[i] = NULL;
  }
}

static AOM_INLINE void allocate_mc_tmp_buf(AV1_COMMON *const cm,
                                           ThreadData *thread_data,
                                           int buf_size) {
  for (int ref = 0; ref < 2; ref++) {
    // The mc_buf/hbd_mc_buf must be zeroed to fix a intermittent valgrind error
    // 'Conditional jump or move depends on uninitialised value' from the loop
    // filter. Uninitialized reads in convolve function (e.g. horiz_4tap path in
    // av1_convolve_2d_sr_avx2()) from mc_buf/hbd_mc_buf are seen to be the
    // potential reason for this issue.
    uint16_t *hbd_mc_buf;
    CHECK_MEM_ERROR(cm, hbd_mc_buf, (uint16_t *)aom_memalign(16, buf_size));
    memset(hbd_mc_buf, 0, buf_size);
    thread_data->mc_buf[ref] = hbd_mc_buf;
  }
  thread_data->mc_buf_size = buf_size;

  CHECK_MEM_ERROR(cm, thread_data->tmp_conv_dst,
                  aom_memalign(32, MAX_SB_SIZE * MAX_SB_SIZE *
                                       sizeof(*thread_data->tmp_conv_dst)));
  for (int i = 0; i < 2; ++i) {
    CHECK_MEM_ERROR(
        cm, thread_data->tmp_obmc_bufs[i],
        aom_memalign(16, 2 * MAX_MB_PLANE * MAX_SB_SQUARE *
                             sizeof(*thread_data->tmp_obmc_bufs[i])));
  }
}

static AOM_INLINE void reset_dec_workers(AV1Decoder *pbi,
                                         AVxWorkerHook worker_hook,
                                         int num_workers) {
  const AVxWorkerInterface *const winterface = aom_get_worker_interface();

  // Reset tile decoding hook
  for (int worker_idx = 0; worker_idx < num_workers; ++worker_idx) {
    AVxWorker *const worker = &pbi->tile_workers[worker_idx];
    DecWorkerData *const thread_data = pbi->thread_data + worker_idx;
    thread_data->td->dcb = pbi->dcb;
    thread_data->td->dcb.corrupted = 0;
    thread_data->td->dcb.mc_buf[0] = thread_data->td->mc_buf[0];
    thread_data->td->dcb.mc_buf[1] = thread_data->td->mc_buf[1];
    thread_data->td->dcb.xd.tmp_conv_dst = thread_data->td->tmp_conv_dst;
    for (int j = 0; j < 2; ++j) {
      thread_data->td->dcb.xd.tmp_obmc_bufs[j] =
          thread_data->td->tmp_obmc_bufs[j];
    }
    winterface->sync(worker);

    worker->hook = worker_hook;
    worker->data1 = thread_data;
    worker->data2 = pbi;
  }
#if CONFIG_ACCOUNTING
  if (pbi->acct_enabled) {
#if CONFIG_THROUGHPUT_ANALYSIS
    aom_accounting_cal_total(pbi);
#else
      aom_accounting_dump(&pbi->accounting);
#endif  // CONFIG_THROUGHPUT_ANALYSIS
    aom_accounting_reset(&pbi->accounting);
  }
#endif
}

static AOM_INLINE void launch_dec_workers(AV1Decoder *pbi,
                                          const uint8_t *data_end,
                                          int num_workers) {
  const AVxWorkerInterface *const winterface = aom_get_worker_interface();

  for (int worker_idx = 0; worker_idx < num_workers; ++worker_idx) {
    AVxWorker *const worker = &pbi->tile_workers[worker_idx];
    DecWorkerData *const thread_data = (DecWorkerData *)worker->data1;

    thread_data->data_end = data_end;

    worker->had_error = 0;
    if (worker_idx == num_workers - 1) {
      winterface->execute(worker);
    } else {
      winterface->launch(worker);
    }
  }
}

static AOM_INLINE void sync_dec_workers(AV1Decoder *pbi, int num_workers) {
  const AVxWorkerInterface *const winterface = aom_get_worker_interface();
  int corrupted = 0;

  for (int worker_idx = num_workers; worker_idx > 0; --worker_idx) {
    AVxWorker *const worker = &pbi->tile_workers[worker_idx - 1];
    aom_merge_corrupted_flag(&corrupted, !winterface->sync(worker));
  }

  pbi->dcb.corrupted = corrupted;
}

static AOM_INLINE void decode_mt_init(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  const AVxWorkerInterface *const winterface = aom_get_worker_interface();
  int worker_idx;

  // Create workers and thread_data
  if (pbi->num_workers == 0) {
    const int num_threads = pbi->max_threads;
    CHECK_MEM_ERROR(cm, pbi->tile_workers,
                    aom_malloc(num_threads * sizeof(*pbi->tile_workers)));
    CHECK_MEM_ERROR(cm, pbi->thread_data,
                    aom_malloc(num_threads * sizeof(*pbi->thread_data)));

    for (worker_idx = 0; worker_idx < num_threads; ++worker_idx) {
      AVxWorker *const worker = &pbi->tile_workers[worker_idx];
      DecWorkerData *const thread_data = pbi->thread_data + worker_idx;
      ++pbi->num_workers;

      winterface->init(worker);
      worker->thread_name = "aom tile worker";
      if (worker_idx < num_threads - 1 && !winterface->reset(worker)) {
        aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                           "Tile decoder thread creation failed");
      }

      if (worker_idx < num_threads - 1) {
        // Allocate thread data.
        CHECK_MEM_ERROR(cm, thread_data->td,
                        aom_memalign(32, sizeof(*thread_data->td)));
        av1_zero(*thread_data->td);
      } else {
        // Main thread acts as a worker and uses the thread data in pbi
        thread_data->td = &pbi->td;
      }
      thread_data->error_info.error_code = AOM_CODEC_OK;
      thread_data->error_info.setjmp = 0;
    }
  }
  const int buf_size = MC_TEMP_BUF_PELS << 1;
  for (worker_idx = 0; worker_idx < pbi->max_threads - 1; ++worker_idx) {
    DecWorkerData *const thread_data = pbi->thread_data + worker_idx;
    if (thread_data->td->mc_buf_size != buf_size) {
      av1_free_mc_tmp_buf(thread_data->td);
      allocate_mc_tmp_buf(cm, thread_data->td, buf_size);
    }
  }
}

static AOM_INLINE void tile_mt_queue(AV1Decoder *pbi, int tile_cols,
                                     int tile_rows, int tile_rows_start,
                                     int tile_rows_end, int tile_cols_start,
                                     int tile_cols_end, int start_tile,
                                     int end_tile) {
  AV1_COMMON *const cm = &pbi->common;
  if (pbi->tile_mt_info.alloc_tile_cols != tile_cols ||
      pbi->tile_mt_info.alloc_tile_rows != tile_rows) {
    av1_dealloc_dec_jobs(&pbi->tile_mt_info);
    alloc_dec_jobs(&pbi->tile_mt_info, cm, tile_rows, tile_cols);
  }
  enqueue_tile_jobs(pbi, cm, tile_rows_start, tile_rows_end, tile_cols_start,
                    tile_cols_end, start_tile, end_tile);
  qsort(pbi->tile_mt_info.job_queue, pbi->tile_mt_info.jobs_enqueued,
        sizeof(pbi->tile_mt_info.job_queue[0]), compare_tile_buffers);
}

static const uint8_t *decode_tiles_mt(AV1Decoder *pbi, const uint8_t *data,
                                      const uint8_t *data_end, int start_tile,
                                      int end_tile) {
  AV1_COMMON *const cm = &pbi->common;
  CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  const int n_tiles = tile_cols * tile_rows;
  TileBufferDec(*const tile_buffers)[MAX_TILE_COLS] = pbi->tile_buffers;
  const int dec_tile_row = AOMMIN(pbi->dec_tile_row, tile_rows);
  const int single_row = pbi->dec_tile_row >= 0;
  const int dec_tile_col = AOMMIN(pbi->dec_tile_col, tile_cols);
  const int single_col = pbi->dec_tile_col >= 0;
  int tile_rows_start;
  int tile_rows_end;
  int tile_cols_start;
  int tile_cols_end;
  int tile_count_tg;
  int num_workers;
  const uint8_t *raw_data_end = NULL;

  if (tiles->large_scale) {
    tile_rows_start = single_row ? dec_tile_row : 0;
    tile_rows_end = single_row ? dec_tile_row + 1 : tile_rows;
    tile_cols_start = single_col ? dec_tile_col : 0;
    tile_cols_end = single_col ? tile_cols_start + 1 : tile_cols;
  } else {
    tile_rows_start = 0;
    tile_rows_end = tile_rows;
    tile_cols_start = 0;
    tile_cols_end = tile_cols;
  }
  tile_count_tg = end_tile - start_tile + 1;
  num_workers = AOMMIN(pbi->max_threads, tile_count_tg);

  // No tiles to decode.
  if (tile_rows_end <= tile_rows_start || tile_cols_end <= tile_cols_start ||
      // First tile is larger than end_tile.
      tile_rows_start * tile_cols + tile_cols_start > end_tile ||
      // Last tile is smaller than start_tile.
      (tile_rows_end - 1) * tile_cols + tile_cols_end - 1 < start_tile)
    return data;

  assert(tile_rows <= MAX_TILE_ROWS);
  assert(tile_cols <= MAX_TILE_COLS);
  assert(tile_count_tg > 0);
  assert(num_workers > 0);
  assert(start_tile <= end_tile);
  assert(start_tile >= 0 && end_tile < n_tiles);

  decode_mt_init(pbi);

  // get tile size in tile group
#if EXT_TILE_DEBUG
  if (tiles->large_scale) assert(pbi->ext_tile_debug == 1);
  if (tiles->large_scale)
    raw_data_end = get_ls_tile_buffers(pbi, data, data_end, tile_buffers);
  else
#endif  // EXT_TILE_DEBUG
    get_tile_buffers(pbi, data, data_end, tile_buffers, start_tile, end_tile);

  if (pbi->tile_data == NULL || n_tiles != pbi->allocated_tiles) {
    decoder_alloc_tile_data(pbi, n_tiles);
  }

  for (int row = 0; row < tile_rows; row++) {
    for (int col = 0; col < tile_cols; col++) {
      TileDataDec *tile_data = pbi->tile_data + row * tiles->cols + col;
      av1_tile_init(&tile_data->tile_info, cm, row, col);
    }
  }

  tile_mt_queue(pbi, tile_cols, tile_rows, tile_rows_start, tile_rows_end,
                tile_cols_start, tile_cols_end, start_tile, end_tile);

  reset_dec_workers(pbi, tile_worker_hook, num_workers);
  launch_dec_workers(pbi, data_end, num_workers);
  sync_dec_workers(pbi, num_workers);

  if (pbi->dcb.corrupted)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Failed to decode tile data");

  if (tiles->large_scale) {
    if (n_tiles == 1) {
      // Find the end of the single tile buffer
      return aom_reader_find_end(&pbi->tile_data->bit_reader);
    }
    // Return the end of the last tile buffer
    return raw_data_end;
  }
  TileDataDec *const tile_data = pbi->tile_data + end_tile;

  return aom_reader_find_end(&tile_data->bit_reader);
}

static AOM_INLINE void dec_alloc_cb_buf(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  int size = ((cm->mi_params.mi_rows >> cm->mib_size_log2) + 1) *
             ((cm->mi_params.mi_cols >> cm->mib_size_log2) + 1);

  if (pbi->cb_buffer_alloc_size < size) {
    av1_dec_free_cb_buf(pbi);
    CHECK_MEM_ERROR(cm, pbi->cb_buffer_base,
                    aom_memalign(32, sizeof(*pbi->cb_buffer_base) * size));
    memset(pbi->cb_buffer_base, 0, sizeof(*pbi->cb_buffer_base) * size);
    pbi->cb_buffer_alloc_size = size;
  }
}

static AOM_INLINE void row_mt_frame_init(AV1Decoder *pbi, int tile_rows_start,
                                         int tile_rows_end, int tile_cols_start,
                                         int tile_cols_end, int start_tile,
                                         int end_tile, int max_sb_rows) {
  AV1_COMMON *const cm = &pbi->common;
  AV1DecRowMTInfo *frame_row_mt_info = &pbi->frame_row_mt_info;

  frame_row_mt_info->tile_rows_start = tile_rows_start;
  frame_row_mt_info->tile_rows_end = tile_rows_end;
  frame_row_mt_info->tile_cols_start = tile_cols_start;
  frame_row_mt_info->tile_cols_end = tile_cols_end;
  frame_row_mt_info->start_tile = start_tile;
  frame_row_mt_info->end_tile = end_tile;
  frame_row_mt_info->mi_rows_to_decode = 0;
  frame_row_mt_info->mi_rows_parse_done = 0;
  frame_row_mt_info->mi_rows_decode_started = 0;
  frame_row_mt_info->row_mt_exit = 0;

  for (int tile_row = tile_rows_start; tile_row < tile_rows_end; ++tile_row) {
    for (int tile_col = tile_cols_start; tile_col < tile_cols_end; ++tile_col) {
      if (tile_row * cm->tiles.cols + tile_col < start_tile ||
          tile_row * cm->tiles.cols + tile_col > end_tile)
        continue;

      TileDataDec *const tile_data =
          pbi->tile_data + tile_row * cm->tiles.cols + tile_col;
      TileInfo tile_info = tile_data->tile_info;

      tile_data->dec_row_mt_sync.mi_rows_parse_done = 0;
      tile_data->dec_row_mt_sync.mi_rows_decode_started = 0;
      tile_data->dec_row_mt_sync.num_threads_working = 0;
      tile_data->dec_row_mt_sync.mi_rows = ALIGN_POWER_OF_TWO(
          tile_info.mi_row_end - tile_info.mi_row_start, cm->mib_size_log2);
      tile_data->dec_row_mt_sync.mi_cols = ALIGN_POWER_OF_TWO(
          tile_info.mi_col_end - tile_info.mi_col_start, cm->mib_size_log2);

      frame_row_mt_info->mi_rows_to_decode +=
          tile_data->dec_row_mt_sync.mi_rows;

      // Initialize cur_sb_col to -1 for all SB rows.
      memset(tile_data->dec_row_mt_sync.cur_sb_col, -1,
             sizeof(*tile_data->dec_row_mt_sync.cur_sb_col) * max_sb_rows);
    }
  }

#if CONFIG_MULTITHREAD
  if (pbi->row_mt_mutex_ == NULL) {
    CHECK_MEM_ERROR(cm, pbi->row_mt_mutex_,
                    aom_malloc(sizeof(*(pbi->row_mt_mutex_))));
    if (pbi->row_mt_mutex_) {
      pthread_mutex_init(pbi->row_mt_mutex_, NULL);
    }
  }

  if (pbi->row_mt_cond_ == NULL) {
    CHECK_MEM_ERROR(cm, pbi->row_mt_cond_,
                    aom_malloc(sizeof(*(pbi->row_mt_cond_))));
    if (pbi->row_mt_cond_) {
      pthread_cond_init(pbi->row_mt_cond_, NULL);
    }
  }
#endif
}

static const uint8_t *decode_tiles_row_mt(AV1Decoder *pbi, const uint8_t *data,
                                          const uint8_t *data_end,
                                          int start_tile, int end_tile) {
  AV1_COMMON *const cm = &pbi->common;
  CommonTileParams *const tiles = &cm->tiles;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  const int n_tiles = tile_cols * tile_rows;
  TileBufferDec(*const tile_buffers)[MAX_TILE_COLS] = pbi->tile_buffers;
  const int dec_tile_row = AOMMIN(pbi->dec_tile_row, tile_rows);
  const int single_row = pbi->dec_tile_row >= 0;
  const int dec_tile_col = AOMMIN(pbi->dec_tile_col, tile_cols);
  const int single_col = pbi->dec_tile_col >= 0;
  int tile_rows_start;
  int tile_rows_end;
  int tile_cols_start;
  int tile_cols_end;
  int tile_count_tg;
  int num_workers = 0;
  int max_threads;
  const uint8_t *raw_data_end = NULL;
  int max_sb_rows = 0;

  if (tiles->large_scale) {
    tile_rows_start = single_row ? dec_tile_row : 0;
    tile_rows_end = single_row ? dec_tile_row + 1 : tile_rows;
    tile_cols_start = single_col ? dec_tile_col : 0;
    tile_cols_end = single_col ? tile_cols_start + 1 : tile_cols;
  } else {
    tile_rows_start = 0;
    tile_rows_end = tile_rows;
    tile_cols_start = 0;
    tile_cols_end = tile_cols;
  }
  tile_count_tg = end_tile - start_tile + 1;
  max_threads = pbi->max_threads;

  // No tiles to decode.
  if (tile_rows_end <= tile_rows_start || tile_cols_end <= tile_cols_start ||
      // First tile is larger than end_tile.
      tile_rows_start * tile_cols + tile_cols_start > end_tile ||
      // Last tile is smaller than start_tile.
      (tile_rows_end - 1) * tile_cols + tile_cols_end - 1 < start_tile)
    return data;

  assert(tile_rows <= MAX_TILE_ROWS);
  assert(tile_cols <= MAX_TILE_COLS);
  assert(tile_count_tg > 0);
  assert(max_threads > 0);
  assert(start_tile <= end_tile);
  assert(start_tile >= 0 && end_tile < n_tiles);

  (void)tile_count_tg;

  decode_mt_init(pbi);

  // get tile size in tile group
#if EXT_TILE_DEBUG
  if (tiles->large_scale) assert(pbi->ext_tile_debug == 1);
  if (tiles->large_scale)
    raw_data_end = get_ls_tile_buffers(pbi, data, data_end, tile_buffers);
  else
#endif  // EXT_TILE_DEBUG
    get_tile_buffers(pbi, data, data_end, tile_buffers, start_tile, end_tile);

  if (pbi->tile_data == NULL || n_tiles != pbi->allocated_tiles) {
    if (pbi->tile_data != NULL) {
      for (int i = 0; i < pbi->allocated_tiles; i++) {
        TileDataDec *const tile_data = pbi->tile_data + i;
        av1_dec_row_mt_dealloc(&tile_data->dec_row_mt_sync);
      }
    }
    decoder_alloc_tile_data(pbi, n_tiles);
  }

  for (int row = 0; row < tile_rows; row++) {
    for (int col = 0; col < tile_cols; col++) {
      TileDataDec *tile_data = pbi->tile_data + row * tiles->cols + col;
      av1_tile_init(&tile_data->tile_info, cm, row, col);

      max_sb_rows = AOMMAX(max_sb_rows,
                           av1_get_sb_rows_in_tile(cm, tile_data->tile_info));
      num_workers += get_max_row_mt_workers_per_tile(cm, tile_data->tile_info);
    }
  }
  num_workers = AOMMIN(num_workers, max_threads);

  if (pbi->allocated_row_mt_sync_rows != max_sb_rows) {
    for (int i = 0; i < n_tiles; ++i) {
      TileDataDec *const tile_data = pbi->tile_data + i;
      av1_dec_row_mt_dealloc(&tile_data->dec_row_mt_sync);
      dec_row_mt_alloc(&tile_data->dec_row_mt_sync, cm, max_sb_rows);
    }
    pbi->allocated_row_mt_sync_rows = max_sb_rows;
  }

  tile_mt_queue(pbi, tile_cols, tile_rows, tile_rows_start, tile_rows_end,
                tile_cols_start, tile_cols_end, start_tile, end_tile);

  dec_alloc_cb_buf(pbi);

  row_mt_frame_init(pbi, tile_rows_start, tile_rows_end, tile_cols_start,
                    tile_cols_end, start_tile, end_tile, max_sb_rows);

  reset_dec_workers(pbi, row_mt_worker_hook, num_workers);
  launch_dec_workers(pbi, data_end, num_workers);
  sync_dec_workers(pbi, num_workers);

  if (pbi->dcb.corrupted)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Failed to decode tile data");

  if (tiles->large_scale) {
    if (n_tiles == 1) {
      // Find the end of the single tile buffer
      return aom_reader_find_end(&pbi->tile_data->bit_reader);
    }
    // Return the end of the last tile buffer
    return raw_data_end;
  }
  TileDataDec *const tile_data = pbi->tile_data + end_tile;

  return aom_reader_find_end(&tile_data->bit_reader);
}

static AOM_INLINE void error_handler(void *data) {
  AV1_COMMON *const cm = (AV1_COMMON *)data;
  aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME, "Truncated packet");
}

// Reads the high_bitdepth and twelve_bit fields in color_config() and sets
// seq_params->bit_depth based on the values of those fields and
// seq_params->profile. Reports errors by calling rb->error_handler() or
// aom_internal_error().
static AOM_INLINE void read_bitdepth(
    struct aom_read_bit_buffer *rb, SequenceHeader *seq_params,
    struct aom_internal_error_info *error_info) {
  const int high_bitdepth = aom_rb_read_bit(rb);
  if (seq_params->profile == PROFILE_2 && high_bitdepth) {
    const int twelve_bit = aom_rb_read_bit(rb);
    seq_params->bit_depth = twelve_bit ? AOM_BITS_12 : AOM_BITS_10;
  } else if (seq_params->profile <= PROFILE_2) {
    seq_params->bit_depth = high_bitdepth ? AOM_BITS_10 : AOM_BITS_8;
  } else {
    aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                       "Unsupported profile/bit-depth combination");
  }
}

void av1_read_film_grain_params(AV1_COMMON *cm,
                                struct aom_read_bit_buffer *rb) {
  aom_film_grain_t *pars = &cm->film_grain_params;
  const SequenceHeader *const seq_params = &cm->seq_params;

  pars->apply_grain = aom_rb_read_bit(rb);
  if (!pars->apply_grain) {
    memset(pars, 0, sizeof(*pars));
    return;
  }

  pars->random_seed = aom_rb_read_literal(rb, 16);
  if (cm->current_frame.frame_type == INTER_FRAME)
    pars->update_parameters = aom_rb_read_bit(rb);
  else
    pars->update_parameters = 1;

  pars->bit_depth = seq_params->bit_depth;

  if (!pars->update_parameters) {
    // inherit parameters from a previous reference frame
    int film_grain_params_ref_idx = aom_rb_read_literal(rb, 3);
    // Section 6.8.20: It is a requirement of bitstream conformance that
    // film_grain_params_ref_idx is equal to ref_frame_idx[ j ] for some value
    // of j in the range 0 to REFS_PER_FRAME - 1.
    int found = 0;
    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      if (film_grain_params_ref_idx == cm->remapped_ref_idx[i]) {
        found = 1;
        break;
      }
    }
    if (!found) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Invalid film grain reference idx %d. ref_frame_idx = "
                         "{%d, %d, %d, %d, %d, %d, %d}",
                         film_grain_params_ref_idx, cm->remapped_ref_idx[0],
                         cm->remapped_ref_idx[1], cm->remapped_ref_idx[2],
                         cm->remapped_ref_idx[3], cm->remapped_ref_idx[4],
                         cm->remapped_ref_idx[5], cm->remapped_ref_idx[6]);
    }
    RefCntBuffer *const buf = cm->ref_frame_map[film_grain_params_ref_idx];
    if (buf == NULL) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Invalid Film grain reference idx");
    }
    if (!buf->film_grain_params_present) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Film grain reference parameters not available");
    }
    uint16_t random_seed = pars->random_seed;
    *pars = buf->film_grain_params;   // inherit paramaters
    pars->random_seed = random_seed;  // with new random seed
    return;
  }

  // Scaling functions parameters
  pars->num_y_points = aom_rb_read_literal(rb, 4);  // max 14
  if (pars->num_y_points > 14)
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Number of points for film grain luma scaling function "
                       "exceeds the maximum value.");
  for (int i = 0; i < pars->num_y_points; i++) {
    pars->scaling_points_y[i][0] = aom_rb_read_literal(rb, 8);
    if (i && pars->scaling_points_y[i - 1][0] >= pars->scaling_points_y[i][0])
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "First coordinate of the scaling function points "
                         "shall be increasing.");
    pars->scaling_points_y[i][1] = aom_rb_read_literal(rb, 8);
  }

  if (!seq_params->monochrome)
    pars->chroma_scaling_from_luma = aom_rb_read_bit(rb);
  else
    pars->chroma_scaling_from_luma = 0;

  if (seq_params->monochrome || pars->chroma_scaling_from_luma ||
      ((seq_params->subsampling_x == 1) && (seq_params->subsampling_y == 1) &&
       (pars->num_y_points == 0))) {
    pars->num_cb_points = 0;
    pars->num_cr_points = 0;
  } else {
    pars->num_cb_points = aom_rb_read_literal(rb, 4);  // max 10
    if (pars->num_cb_points > 10)
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Number of points for film grain cb scaling function "
                         "exceeds the maximum value.");
    for (int i = 0; i < pars->num_cb_points; i++) {
      pars->scaling_points_cb[i][0] = aom_rb_read_literal(rb, 8);
      if (i &&
          pars->scaling_points_cb[i - 1][0] >= pars->scaling_points_cb[i][0])
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "First coordinate of the scaling function points "
                           "shall be increasing.");
      pars->scaling_points_cb[i][1] = aom_rb_read_literal(rb, 8);
    }

    pars->num_cr_points = aom_rb_read_literal(rb, 4);  // max 10
    if (pars->num_cr_points > 10)
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Number of points for film grain cr scaling function "
                         "exceeds the maximum value.");
    for (int i = 0; i < pars->num_cr_points; i++) {
      pars->scaling_points_cr[i][0] = aom_rb_read_literal(rb, 8);
      if (i &&
          pars->scaling_points_cr[i - 1][0] >= pars->scaling_points_cr[i][0])
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "First coordinate of the scaling function points "
                           "shall be increasing.");
      pars->scaling_points_cr[i][1] = aom_rb_read_literal(rb, 8);
    }

    if ((seq_params->subsampling_x == 1) && (seq_params->subsampling_y == 1) &&
        (((pars->num_cb_points == 0) && (pars->num_cr_points != 0)) ||
         ((pars->num_cb_points != 0) && (pars->num_cr_points == 0))))
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "In YCbCr 4:2:0, film grain shall be applied "
                         "to both chroma components or neither.");
  }

  pars->scaling_shift = aom_rb_read_literal(rb, 2) + 8;  // 8 + value

  // AR coefficients
  // Only sent if the corresponsing scaling function has
  // more than 0 points

  pars->ar_coeff_lag = aom_rb_read_literal(rb, 2);

  int num_pos_luma = 2 * pars->ar_coeff_lag * (pars->ar_coeff_lag + 1);
  int num_pos_chroma = num_pos_luma;
  if (pars->num_y_points > 0) ++num_pos_chroma;

  if (pars->num_y_points)
    for (int i = 0; i < num_pos_luma; i++)
      pars->ar_coeffs_y[i] = aom_rb_read_literal(rb, 8) - 128;

  if (pars->num_cb_points || pars->chroma_scaling_from_luma)
    for (int i = 0; i < num_pos_chroma; i++)
      pars->ar_coeffs_cb[i] = aom_rb_read_literal(rb, 8) - 128;

  if (pars->num_cr_points || pars->chroma_scaling_from_luma)
    for (int i = 0; i < num_pos_chroma; i++)
      pars->ar_coeffs_cr[i] = aom_rb_read_literal(rb, 8) - 128;

  pars->ar_coeff_shift = aom_rb_read_literal(rb, 2) + 6;  // 6 + value

  pars->grain_scale_shift = aom_rb_read_literal(rb, 2);

  if (pars->num_cb_points) {
    pars->cb_mult = aom_rb_read_literal(rb, 8);
    pars->cb_luma_mult = aom_rb_read_literal(rb, 8);
    pars->cb_offset = aom_rb_read_literal(rb, 9);
  }

  if (pars->num_cr_points) {
    pars->cr_mult = aom_rb_read_literal(rb, 8);
    pars->cr_luma_mult = aom_rb_read_literal(rb, 8);
    pars->cr_offset = aom_rb_read_literal(rb, 9);
  }

  pars->overlap_flag = aom_rb_read_bit(rb);

  pars->clip_to_restricted_range = aom_rb_read_bit(rb);
}

static AOM_INLINE void read_film_grain(AV1_COMMON *cm,
                                       struct aom_read_bit_buffer *rb) {
  if (cm->seq_params.film_grain_params_present &&
      (cm->show_frame || cm->showable_frame)) {
    av1_read_film_grain_params(cm, rb);
  } else {
    memset(&cm->film_grain_params, 0, sizeof(cm->film_grain_params));
  }
  cm->film_grain_params.bit_depth = cm->seq_params.bit_depth;
  memcpy(&cm->cur_frame->film_grain_params, &cm->film_grain_params,
         sizeof(aom_film_grain_t));
}

void av1_read_color_config(struct aom_read_bit_buffer *rb,
                           SequenceHeader *seq_params,
                           struct aom_internal_error_info *error_info) {
  read_bitdepth(rb, seq_params, error_info);

  // monochrome bit (not needed for PROFILE_1)
  const int is_monochrome =
      seq_params->profile != PROFILE_1 ? aom_rb_read_bit(rb) : 0;
  seq_params->monochrome = is_monochrome;
  int color_description_present_flag = aom_rb_read_bit(rb);
  if (color_description_present_flag) {
    seq_params->color_primaries = aom_rb_read_literal(rb, 8);
    seq_params->transfer_characteristics = aom_rb_read_literal(rb, 8);
    seq_params->matrix_coefficients = aom_rb_read_literal(rb, 8);
  } else {
    seq_params->color_primaries = AOM_CICP_CP_UNSPECIFIED;
    seq_params->transfer_characteristics = AOM_CICP_TC_UNSPECIFIED;
    seq_params->matrix_coefficients = AOM_CICP_MC_UNSPECIFIED;
  }
  if (is_monochrome) {
    // [16,235] (including xvycc) vs [0,255] range
    seq_params->color_range = aom_rb_read_bit(rb);
    seq_params->subsampling_y = seq_params->subsampling_x = 1;
    seq_params->chroma_sample_position = AOM_CSP_UNKNOWN;
    seq_params->separate_uv_delta_q = 0;
  } else {
    if (seq_params->color_primaries == AOM_CICP_CP_BT_709 &&
        seq_params->transfer_characteristics == AOM_CICP_TC_SRGB &&
        seq_params->matrix_coefficients == AOM_CICP_MC_IDENTITY) {
      seq_params->subsampling_y = seq_params->subsampling_x = 0;
      seq_params->color_range = 1;  // assume full color-range
      if (!(seq_params->profile == PROFILE_1 ||
            (seq_params->profile == PROFILE_2 &&
             seq_params->bit_depth == AOM_BITS_12))) {
        aom_internal_error(
            error_info, AOM_CODEC_UNSUP_BITSTREAM,
            "sRGB colorspace not compatible with specified profile");
      }
    } else {
      // [16,235] (including xvycc) vs [0,255] range
      seq_params->color_range = aom_rb_read_bit(rb);
      if (seq_params->profile == PROFILE_0) {
        // 420 only
        seq_params->subsampling_x = seq_params->subsampling_y = 1;
      } else if (seq_params->profile == PROFILE_1) {
        // 444 only
        seq_params->subsampling_x = seq_params->subsampling_y = 0;
      } else {
        assert(seq_params->profile == PROFILE_2);
        if (seq_params->bit_depth == AOM_BITS_12) {
          seq_params->subsampling_x = aom_rb_read_bit(rb);
          if (seq_params->subsampling_x)
            seq_params->subsampling_y = aom_rb_read_bit(rb);  // 422 or 420
          else
            seq_params->subsampling_y = 0;  // 444
        } else {
          // 422
          seq_params->subsampling_x = 1;
          seq_params->subsampling_y = 0;
        }
      }
      if (seq_params->matrix_coefficients == AOM_CICP_MC_IDENTITY &&
          (seq_params->subsampling_x || seq_params->subsampling_y)) {
        aom_internal_error(
            error_info, AOM_CODEC_UNSUP_BITSTREAM,
            "Identity CICP Matrix incompatible with non 4:4:4 color sampling");
      }
      if (seq_params->subsampling_x && seq_params->subsampling_y) {
        seq_params->chroma_sample_position = aom_rb_read_literal(rb, 2);
      }
    }
    seq_params->separate_uv_delta_q = aom_rb_read_bit(rb);
  }

  seq_params->base_y_dc_delta_q =
      DELTA_DCQUANT_MIN + aom_rb_read_literal(rb, DELTA_DCQUANT_BITS);
  if (!is_monochrome) {
    seq_params->base_uv_dc_delta_q =
        DELTA_DCQUANT_MIN + aom_rb_read_literal(rb, DELTA_DCQUANT_BITS);
  }
}

void av1_read_timing_info_header(aom_timing_info_t *timing_info,
                                 struct aom_internal_error_info *error,
                                 struct aom_read_bit_buffer *rb) {
  timing_info->num_units_in_display_tick =
      aom_rb_read_unsigned_literal(rb,
                                   32);  // Number of units in a display tick
  timing_info->time_scale = aom_rb_read_unsigned_literal(rb, 32);  // Time scale
  if (timing_info->num_units_in_display_tick == 0 ||
      timing_info->time_scale == 0) {
    aom_internal_error(
        error, AOM_CODEC_UNSUP_BITSTREAM,
        "num_units_in_display_tick and time_scale must be greater than 0.");
  }
  timing_info->equal_picture_interval =
      aom_rb_read_bit(rb);  // Equal picture interval bit
  if (timing_info->equal_picture_interval) {
    const uint32_t num_ticks_per_picture_minus_1 = aom_rb_read_uvlc(rb);
    if (num_ticks_per_picture_minus_1 == UINT32_MAX) {
      aom_internal_error(
          error, AOM_CODEC_UNSUP_BITSTREAM,
          "num_ticks_per_picture_minus_1 cannot be (1 << 32) − 1.");
    }
    timing_info->num_ticks_per_picture = num_ticks_per_picture_minus_1 + 1;
  }
}

void av1_read_decoder_model_info(aom_dec_model_info_t *decoder_model_info,
                                 struct aom_read_bit_buffer *rb) {
  decoder_model_info->encoder_decoder_buffer_delay_length =
      aom_rb_read_literal(rb, 5) + 1;
  decoder_model_info->num_units_in_decoding_tick =
      aom_rb_read_unsigned_literal(rb,
                                   32);  // Number of units in a decoding tick
  decoder_model_info->buffer_removal_time_length =
      aom_rb_read_literal(rb, 5) + 1;
  decoder_model_info->frame_presentation_time_length =
      aom_rb_read_literal(rb, 5) + 1;
}

void av1_read_op_parameters_info(aom_dec_model_op_parameters_t *op_params,
                                 int buffer_delay_length,
                                 struct aom_read_bit_buffer *rb) {
  op_params->decoder_buffer_delay =
      aom_rb_read_unsigned_literal(rb, buffer_delay_length);
  op_params->encoder_buffer_delay =
      aom_rb_read_unsigned_literal(rb, buffer_delay_length);
  op_params->low_delay_mode_flag = aom_rb_read_bit(rb);
}

static AOM_INLINE void read_temporal_point_info(
    AV1_COMMON *const cm, struct aom_read_bit_buffer *rb) {
  cm->frame_presentation_time = aom_rb_read_unsigned_literal(
      rb, cm->seq_params.decoder_model_info.frame_presentation_time_length);
}

void av1_read_sequence_header(AV1_COMMON *cm, struct aom_read_bit_buffer *rb,
                              SequenceHeader *seq_params) {
  const int num_bits_width = aom_rb_read_literal(rb, 4) + 1;
  const int num_bits_height = aom_rb_read_literal(rb, 4) + 1;
  const int max_frame_width = aom_rb_read_literal(rb, num_bits_width) + 1;
  const int max_frame_height = aom_rb_read_literal(rb, num_bits_height) + 1;

  seq_params->num_bits_width = num_bits_width;
  seq_params->num_bits_height = num_bits_height;
  seq_params->max_frame_width = max_frame_width;
  seq_params->max_frame_height = max_frame_height;

  if (seq_params->reduced_still_picture_hdr) {
    seq_params->frame_id_numbers_present_flag = 0;
  } else {
    seq_params->frame_id_numbers_present_flag = aom_rb_read_bit(rb);
  }
  if (seq_params->frame_id_numbers_present_flag) {
    // We must always have delta_frame_id_length < frame_id_length,
    // in order for a frame to be referenced with a unique delta.
    // Avoid wasting bits by using a coding that enforces this restriction.
    seq_params->delta_frame_id_length = aom_rb_read_literal(rb, 4) + 2;
    seq_params->frame_id_length =
        aom_rb_read_literal(rb, 3) + seq_params->delta_frame_id_length + 1;
    if (seq_params->frame_id_length > 16)
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid frame_id_length");
  }

  setup_seq_sb_size(seq_params, rb);

  seq_params->enable_filter_intra = aom_rb_read_bit(rb);
  seq_params->enable_intra_edge_filter = aom_rb_read_bit(rb);
  if (seq_params->reduced_still_picture_hdr) {
#if CONFIG_EXTENDED_WARP_PREDICTION
    seq_params->seq_enabled_motion_modes = (1 << SIMPLE_TRANSLATION);
#else
    seq_params->enable_interintra_compound = 0;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
    seq_params->enable_masked_compound = 0;
#if !CONFIG_EXTENDED_WARP_PREDICTION
    seq_params->enable_warped_motion = 0;
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION
    seq_params->order_hint_info.enable_order_hint = 0;
    seq_params->order_hint_info.enable_ref_frame_mvs = 0;
    seq_params->force_screen_content_tools = 2;  // SELECT_SCREEN_CONTENT_TOOLS
    seq_params->force_integer_mv = 2;            // SELECT_INTEGER_MV
    seq_params->order_hint_info.order_hint_bits_minus_1 = -1;
#if CONFIG_OPTFLOW_REFINEMENT
    seq_params->enable_opfl_refine = AOM_OPFL_REFINE_NONE;
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_AFFINE_REFINEMENT
    seq_params->enable_affine_refine = 0;
#endif  // CONFIG_AFFINE_REFINEMENT
  } else {
#if CONFIG_EXTENDED_WARP_PREDICTION
    int seq_enabled_motion_modes = (1 << SIMPLE_TRANSLATION);
    for (int motion_mode = INTERINTRA; motion_mode < MOTION_MODES;
         motion_mode++) {
      int enabled = aom_rb_read_bit(rb);
      if (enabled) {
        seq_enabled_motion_modes |= (1 << motion_mode);
      }
    }
    seq_params->seq_enabled_motion_modes = seq_enabled_motion_modes;
#else
    seq_params->enable_interintra_compound = aom_rb_read_bit(rb);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
    seq_params->enable_masked_compound = aom_rb_read_bit(rb);
#if !CONFIG_EXTENDED_WARP_PREDICTION
    seq_params->enable_warped_motion = aom_rb_read_bit(rb);
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION
    seq_params->order_hint_info.enable_order_hint = aom_rb_read_bit(rb);
    seq_params->order_hint_info.enable_ref_frame_mvs =
        seq_params->order_hint_info.enable_order_hint ? aom_rb_read_bit(rb) : 0;

    if (aom_rb_read_bit(rb)) {
      seq_params->force_screen_content_tools =
          2;  // SELECT_SCREEN_CONTENT_TOOLS
    } else {
      seq_params->force_screen_content_tools = aom_rb_read_bit(rb);
    }

    if (seq_params->force_screen_content_tools > 0) {
      if (aom_rb_read_bit(rb)) {
        seq_params->force_integer_mv = 2;  // SELECT_INTEGER_MV
      } else {
        seq_params->force_integer_mv = aom_rb_read_bit(rb);
      }
    } else {
      seq_params->force_integer_mv = 2;  // SELECT_INTEGER_MV
    }
    seq_params->order_hint_info.order_hint_bits_minus_1 =
        seq_params->order_hint_info.enable_order_hint
            ? aom_rb_read_literal(rb, 3)
            : -1;
  }

  seq_params->enable_superres = aom_rb_read_bit(rb);
  seq_params->enable_cdef = aom_rb_read_bit(rb);
  seq_params->enable_restoration = aom_rb_read_bit(rb);
#if CONFIG_LR_FLEX_SYNTAX
  if (seq_params->enable_restoration) {
    for (int i = 1; i < RESTORE_SWITCHABLE_TYPES; ++i) {
      seq_params->lr_tools_disable_mask[0] |= (aom_rb_read_bit(rb) << i);
    }
    if (aom_rb_read_bit(rb)) {
      seq_params->lr_tools_disable_mask[1] = DEF_UV_LR_TOOLS_DISABLE_MASK;
      for (int i = 1; i < RESTORE_SWITCHABLE_TYPES; ++i) {
        if (DEF_UV_LR_TOOLS_DISABLE_MASK & (1 << i)) continue;
        seq_params->lr_tools_disable_mask[1] |= (aom_rb_read_bit(rb) << i);
      }
    } else {
      seq_params->lr_tools_disable_mask[1] =
          (seq_params->lr_tools_disable_mask[0] | DEF_UV_LR_TOOLS_DISABLE_MASK);
    }
  }
#endif  // CONFIG_LR_FLEX_SYNTAX
}

void av1_read_sequence_header_beyond_av1(struct aom_read_bit_buffer *rb,
                                         SequenceHeader *seq_params) {
  // printf("print sps\n");
  seq_params->enable_refmvbank = aom_rb_read_bit(rb);
  seq_params->explicit_ref_frame_map = aom_rb_read_bit(rb);
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  // 0 : use show_existing_frame, 1: use implicit derivation
  seq_params->enable_frame_output_order = aom_rb_read_bit(rb);
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  // A bit is sent here to indicate if the max number of references is 7. If
  // this bit is 0, then two more bits are sent to indicate the exact number
  // of references allowed (range: 3 to 6).
  if (aom_rb_read_bit(rb)) {
    seq_params->max_reference_frames = 3 + aom_rb_read_literal(rb, 2);
  } else {
    seq_params->max_reference_frames = 7;
  }
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  seq_params->num_same_ref_compound = aom_rb_read_literal(rb, 2);
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  seq_params->enable_sdp = aom_rb_read_bit(rb);
  seq_params->enable_ist = aom_rb_read_bit(rb);
  seq_params->enable_cctx = seq_params->monochrome ? 0 : aom_rb_read_bit(rb);
  seq_params->enable_mrls = aom_rb_read_bit(rb);
  seq_params->enable_tip = aom_rb_read_literal(rb, 2);
  if (seq_params->enable_tip) {
    seq_params->enable_tip_hole_fill = aom_rb_read_bit(rb);
  } else {
    seq_params->enable_tip_hole_fill = 0;
  }
#if CONFIG_BAWP
  seq_params->enable_bawp = aom_rb_read_bit(rb);
#endif  // CONFIG_BAWP
  seq_params->enable_cwp = aom_rb_read_bit(rb);
#if CONFIG_D071_IMP_MSK_BLD
  seq_params->enable_imp_msk_bld = aom_rb_read_bit(rb);
#endif  // CONFIG_D071_IMP_MSK_BLD
  seq_params->enable_fsc = aom_rb_read_bit(rb);
#if CONFIG_CCSO
  seq_params->enable_ccso = aom_rb_read_bit(rb);
#endif
  seq_params->enable_pef = aom_rb_read_bit(rb);
#if CONFIG_TIP_IMPLICIT_QUANT
  if (seq_params->enable_tip == 1 && seq_params->enable_pef) {
    seq_params->enable_tip_explicit_qp = aom_rb_read_bit(rb);
  } else {
    seq_params->enable_tip_explicit_qp = 0;
  }
#endif  // CONFIG_TIP_IMPLICIT_QUANT
  seq_params->enable_orip = aom_rb_read_bit(rb);
#if CONFIG_IDIF
  seq_params->enable_idif = aom_rb_read_bit(rb);
#endif  // CONFIG_IDIF
#if CONFIG_OPTFLOW_REFINEMENT
  seq_params->enable_opfl_refine = seq_params->order_hint_info.enable_order_hint
                                       ? aom_rb_read_literal(rb, 2)
                                       : AOM_OPFL_REFINE_NONE;
#if CONFIG_AFFINE_REFINEMENT
  seq_params->enable_affine_refine =
      seq_params->enable_opfl_refine ? aom_rb_read_bit(rb) : 0;
#endif  // CONFIG_AFFINE_REFINEMENT
#endif  // CONFIG_OPTFLOW_REFINEMENT
  seq_params->enable_ibp = aom_rb_read_bit(rb);
  seq_params->enable_adaptive_mvd = aom_rb_read_bit(rb);

#if CONFIG_REFINEMV
  seq_params->enable_refinemv = aom_rb_read_bit(rb);
#endif  // CONFIG_REFINEMV
#if CONFIG_FLEX_MVRES
  seq_params->enable_flex_mvres = aom_rb_read_bit(rb);
#endif  // CONFIG_FLEX_MVRES

#if CONFIG_ADAPTIVE_DS_FILTER
  seq_params->enable_cfl_ds_filter = aom_rb_read_literal(rb, 2);
#endif  // CONFIG_ADAPTIVE_DS_FILTER

  seq_params->enable_parity_hiding = aom_rb_read_bit(rb);
#if CONFIG_EXT_RECUR_PARTITIONS
  seq_params->enable_ext_partitions = aom_rb_read_bit(rb);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_IMPROVED_GLOBAL_MOTION
  if (seq_params->reduced_still_picture_hdr) {
    seq_params->enable_global_motion = 0;
  } else {
    seq_params->enable_global_motion = aom_rb_read_bit(rb);
  }
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
#if CONFIG_REFRESH_FLAG
  seq_params->enable_short_refresh_frame_flags = aom_rb_read_bit(rb);
#endif  // CONFIG_REFRESH_FLAG
}

static int read_global_motion_params(WarpedMotionParams *params,
                                     const WarpedMotionParams *ref_params,
                                     struct aom_read_bit_buffer *rb,
#if !CONFIG_FLEX_MVRES
                                     int allow_hp) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
  (void)allow_hp;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
#else
                                     MvSubpelPrecision precision) {
  const int precision_loss = get_gm_precision_loss(precision);
#if CONFIG_IMPROVED_GLOBAL_MOTION
  (void)precision_loss;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
#endif  // !CONFIG_FLEX_MVRES
  TransformationType type = aom_rb_read_bit(rb);
  if (type != IDENTITY) {
    if (aom_rb_read_bit(rb)) {
      type = ROTZOOM;
    } else {
#if CONFIG_IMPROVED_GLOBAL_MOTION
      type = AFFINE;
#else
      type = aom_rb_read_bit(rb) ? TRANSLATION : AFFINE;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
    }
  }

  *params = default_warp_params;
  params->wmtype = type;

  if (type >= ROTZOOM) {
    params->wmmat[2] = aom_rb_read_signed_primitive_refsubexpfin(
                           rb, GM_ALPHA_MAX + 1, SUBEXPFIN_K,
                           (ref_params->wmmat[2] >> GM_ALPHA_PREC_DIFF) -
                               (1 << GM_ALPHA_PREC_BITS)) *
                           GM_ALPHA_DECODE_FACTOR +
                       (1 << WARPEDMODEL_PREC_BITS);
    params->wmmat[3] = aom_rb_read_signed_primitive_refsubexpfin(
                           rb, GM_ALPHA_MAX + 1, SUBEXPFIN_K,
                           (ref_params->wmmat[3] >> GM_ALPHA_PREC_DIFF)) *
                       GM_ALPHA_DECODE_FACTOR;
  }

  if (type >= AFFINE) {
    params->wmmat[4] = aom_rb_read_signed_primitive_refsubexpfin(
                           rb, GM_ALPHA_MAX + 1, SUBEXPFIN_K,
                           (ref_params->wmmat[4] >> GM_ALPHA_PREC_DIFF)) *
                       GM_ALPHA_DECODE_FACTOR;
    params->wmmat[5] = aom_rb_read_signed_primitive_refsubexpfin(
                           rb, GM_ALPHA_MAX + 1, SUBEXPFIN_K,
                           (ref_params->wmmat[5] >> GM_ALPHA_PREC_DIFF) -
                               (1 << GM_ALPHA_PREC_BITS)) *
                           GM_ALPHA_DECODE_FACTOR +
                       (1 << WARPEDMODEL_PREC_BITS);
  } else {
    params->wmmat[4] = -params->wmmat[3];
    params->wmmat[5] = params->wmmat[2];
  }

  if (type >= TRANSLATION) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
    const int trans_dec_factor = GM_TRANS_DECODE_FACTOR;
    const int trans_prec_diff = GM_TRANS_PREC_DIFF;
    const int trans_max = GM_TRANS_MAX;
#else
    const int trans_bits = (type == TRANSLATION)
#if CONFIG_FLEX_MVRES
                               ? GM_ABS_TRANS_ONLY_BITS - precision_loss
#else
                               ? GM_ABS_TRANS_ONLY_BITS - !allow_hp
#endif
                               : GM_ABS_TRANS_BITS;
    const int trans_dec_factor =
        (type == TRANSLATION)
#if CONFIG_FLEX_MVRES
            ? GM_TRANS_ONLY_DECODE_FACTOR * (1 << precision_loss)
#else
            ? GM_TRANS_ONLY_DECODE_FACTOR * (1 << !allow_hp)
#endif
            : GM_TRANS_DECODE_FACTOR;
    const int trans_prec_diff = (type == TRANSLATION)
#if CONFIG_FLEX_MVRES
                                    ? GM_TRANS_ONLY_PREC_DIFF + precision_loss
#else
                                    ? GM_TRANS_ONLY_PREC_DIFF + !allow_hp
#endif
                                    : GM_TRANS_PREC_DIFF;
    const int trans_max = (1 << trans_bits);
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

    params->wmmat[0] = aom_rb_read_signed_primitive_refsubexpfin(
                           rb, trans_max + 1, SUBEXPFIN_K,
                           (ref_params->wmmat[0] >> trans_prec_diff)) *
                       trans_dec_factor;
    params->wmmat[1] = aom_rb_read_signed_primitive_refsubexpfin(
                           rb, trans_max + 1, SUBEXPFIN_K,
                           (ref_params->wmmat[1] >> trans_prec_diff)) *
                       trans_dec_factor;
  }

  if (params->wmtype <= AFFINE) {
#if CONFIG_EXTENDED_WARP_PREDICTION
    av1_reduce_warp_model(params);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
    int good_shear_params = av1_get_shear_params(params);
    if (!good_shear_params) return 0;
  }

  return 1;
}

static AOM_INLINE void read_global_motion(AV1_COMMON *cm,
                                          struct aom_read_bit_buffer *rb) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
  const SequenceHeader *const seq_params = &cm->seq_params;
  int num_total_refs = cm->ref_frames_info.num_total_refs;
  bool use_global_motion = false;
  if (seq_params->enable_global_motion) {
    use_global_motion = aom_rb_read_bit(rb);
  }
  if (!use_global_motion) {
    for (int frame = 0; frame < INTER_REFS_PER_FRAME; ++frame) {
      cm->global_motion[frame] = default_warp_params;
      cm->cur_frame->global_motion[frame] = default_warp_params;
    }
    return;
  }

  int our_ref = aom_rb_read_primitive_quniform(rb, num_total_refs + 1);
  if (our_ref == num_total_refs) {
    // Special case: Use IDENTITY model
    cm->base_global_motion_model = default_warp_params;
    cm->base_global_motion_distance = 1;
  } else {
    RefCntBuffer *buf = get_ref_frame_buf(cm, our_ref);
    assert(buf);
    int their_num_refs = buf->num_ref_frames;
    if (their_num_refs == 0) {
      // Special case: if an intra/key frame is used as a ref, use an
      // IDENTITY model
      cm->base_global_motion_model = default_warp_params;
      cm->base_global_motion_distance = 1;
    } else {
      int their_ref = aom_rb_read_primitive_quniform(rb, their_num_refs);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      const int our_ref_order_hint = buf->display_order_hint;
      const int their_ref_order_hint = buf->ref_display_order_hint[their_ref];
#else
        const int our_ref_order_hint = buf->order_hint;
        const int their_ref_order_hint = buf->ref_order_hints[their_ref];
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      cm->base_global_motion_model = buf->global_motion[their_ref];
      cm->base_global_motion_distance =
          get_relative_dist(&seq_params->order_hint_info, our_ref_order_hint,
                            their_ref_order_hint);
    }
  }
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

  for (int frame = 0; frame < cm->ref_frames_info.num_total_refs; ++frame) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
    int temporal_distance;
    if (seq_params->order_hint_info.enable_order_hint) {
      const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, frame);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      const int ref_order_hint = ref_buf->display_order_hint;
      const int cur_order_hint = cm->cur_frame->display_order_hint;
#else
        const int ref_order_hint = ref_buf->order_hint;
        const int cur_order_hint = cm->cur_frame->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      temporal_distance = get_relative_dist(&seq_params->order_hint_info,
                                            cur_order_hint, ref_order_hint);
    } else {
      temporal_distance = 1;
    }

    if (temporal_distance == 0) {
      // Don't code global motion for frames at the same temporal instant
      cm->global_motion[frame] = default_warp_params;
      continue;
    }

    WarpedMotionParams ref_params_;
    av1_scale_warp_model(&cm->base_global_motion_model,
                         cm->base_global_motion_distance, &ref_params_,
                         temporal_distance);
    WarpedMotionParams *ref_params = &ref_params_;
#else
    const WarpedMotionParams *ref_params =
        cm->prev_frame ? &cm->prev_frame->global_motion[frame]
                       : &default_warp_params;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
    int good_params =
#if !CONFIG_FLEX_MVRES
        read_global_motion_params(&cm->global_motion[frame], ref_params, rb,
                                  cm->features.allow_high_precision_mv);
#else
        read_global_motion_params(&cm->global_motion[frame], ref_params, rb,
                                  cm->features.fr_mv_precision);
#endif
    if (!good_params) {
#if WARPED_MOTION_DEBUG
      printf("Warning: unexpected global motion shear params from aomenc\n");
#endif
      cm->global_motion[frame].invalid = 1;
    }

    // TODO(sarahparker, debargha): The logic in the commented out code below
    // does not work currently and causes mismatches when resize is on. Fix it
    // before turning the optimization back on.
    /*
    YV12_BUFFER_CONFIG *ref_buf = get_ref_frame(cm, frame);
    if (cm->width == ref_buf->y_crop_width &&
        cm->height == ref_buf->y_crop_height) {
      read_global_motion_params(&cm->global_motion[frame],
                                &cm->prev_frame->global_motion[frame], rb,
                                cm->features.allow_high_precision_mv);
    } else {
      cm->global_motion[frame] = default_warp_params;
    }
    */
    /*
    printf("Dec Ref %d [%d/%d]: %d %d %d %d\n",
           frame, cm->current_frame.frame_number, cm->show_frame,
           cm->global_motion[frame].wmmat[0],
           cm->global_motion[frame].wmmat[1],
           cm->global_motion[frame].wmmat[2],
           cm->global_motion[frame].wmmat[3]);
           */
  }
  memcpy(cm->cur_frame->global_motion, cm->global_motion,
         INTER_REFS_PER_FRAME * sizeof(WarpedMotionParams));
}

// Release the references to the frame buffers in cm->ref_frame_map and reset
// all elements of cm->ref_frame_map to NULL.
static AOM_INLINE void reset_ref_frame_map(AV1_COMMON *const cm) {
  BufferPool *const pool = cm->buffer_pool;

  for (int i = 0; i < REF_FRAMES; i++) {
    decrease_ref_count(cm->ref_frame_map[i], pool);
    cm->ref_frame_map[i] = NULL;
  }
}

// If the refresh_frame_flags bitmask is set, update reference frame id values
// and mark frames as valid for reference.
static AOM_INLINE void update_ref_frame_id(AV1Decoder *const pbi) {
  AV1_COMMON *const cm = &pbi->common;
  int refresh_frame_flags = cm->current_frame.refresh_frame_flags;
  for (int i = 0; i < REF_FRAMES; i++) {
    if ((refresh_frame_flags >> i) & 1) {
      cm->ref_frame_id[i] = cm->current_frame_id;
      pbi->valid_for_referencing[i] = 1;
    }
  }
}

static AOM_INLINE void show_existing_frame_reset(AV1Decoder *const pbi,
                                                 int existing_frame_idx) {
  AV1_COMMON *const cm = &pbi->common;

  assert(cm->show_existing_frame);

  cm->current_frame.frame_type = KEY_FRAME;
  cm->current_frame.refresh_frame_flags = REFRESH_FRAME_ALL;

  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    cm->remapped_ref_idx[i] = INVALID_IDX;
  }

  cm->cur_frame->display_order_hint = 0;

  if (pbi->need_resync) {
    reset_ref_frame_map(cm);
    pbi->need_resync = 0;
  }

  // Note that the displayed frame must be valid for referencing in order to
  // have been selected.
  cm->current_frame_id = cm->ref_frame_id[existing_frame_idx];
  update_ref_frame_id(pbi);

  cm->features.refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;
}

static INLINE void reset_frame_buffers(AV1_COMMON *cm) {
  RefCntBuffer *const frame_bufs = cm->buffer_pool->frame_bufs;
  int i;

  lock_buffer_pool(cm->buffer_pool);
  reset_ref_frame_map(cm);
  assert(cm->cur_frame->ref_count == 1);
  for (i = 0; i < FRAME_BUFFERS; ++i) {
    // Reset all unreferenced frame buffers. We can also reset cm->cur_frame
    // because we are the sole owner of cm->cur_frame.
    if (frame_bufs[i].ref_count > 0 && &frame_bufs[i] != cm->cur_frame) {
      continue;
    }
    frame_bufs[i].order_hint = 0;
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
    frame_bufs[i].display_order_hint = 0;
    av1_zero(frame_bufs[i].ref_display_order_hint);
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
    av1_zero(frame_bufs[i].ref_order_hints);
  }
  av1_zero_unused_internal_frame_buffers(&cm->buffer_pool->int_frame_buffers);
  unlock_buffer_pool(cm->buffer_pool);
}

static INLINE int get_disp_order_hint(AV1_COMMON *const cm) {
  CurrentFrame *const current_frame = &cm->current_frame;
  if (current_frame->frame_type == KEY_FRAME && cm->show_existing_frame)
    return 0;

#if CONFIG_DISPLAY_ORDER_HINT_FIX
  // For key frames, the implicit derivation of display_order_hit is not
  // applied.
  if (current_frame->frame_type == KEY_FRAME) return current_frame->order_hint;
#endif  // CONFIG_DISPLAY_ORDER_HINT_FIX
  // Derive the exact display order hint from the signaled order_hint.
  // This requires scaling up order_hints corresponding to frame
  // numbers that exceed the number of bits available to send the order_hints.

  // Find the reference frame with the largest order_hint
  int max_disp_order_hint = 0;
  for (int map_idx = 0; map_idx < REF_FRAMES; map_idx++) {
    // Get reference frame buffer
    const RefCntBuffer *const buf = cm->ref_frame_map[map_idx];
    if (buf == NULL) continue;
    if ((int)buf->display_order_hint > max_disp_order_hint)
      max_disp_order_hint = buf->display_order_hint;
  }

  // If the order_hint is above the threshold distance of 35 frames (largest
  // possible lag_in_frames) from the found reference frame, we assume it was
  // modified using:
  //     order_hint = display_order_hint % display_order_hint_factor
  // Here, the actual display_order_hint is recovered.
  int cur_disp_order_hint = current_frame->order_hint;
  while (abs(max_disp_order_hint - cur_disp_order_hint) > 35) {
    if (cur_disp_order_hint > max_disp_order_hint) return cur_disp_order_hint;
    int display_order_hint_factor =
        1 << (cm->seq_params.order_hint_info.order_hint_bits_minus_1 + 1);
    cur_disp_order_hint += display_order_hint_factor;
  }
  return cur_disp_order_hint;
}

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
static INLINE int get_ref_frame_disp_order_hint(AV1_COMMON *const cm,
                                                const RefCntBuffer *const buf) {
  // Find the reference frame with the largest order_hint
  int max_disp_order_hint = 0;
  for (int map_idx = 0; map_idx < INTER_REFS_PER_FRAME; map_idx++) {
    if ((int)buf->ref_display_order_hint[map_idx] > max_disp_order_hint)
      max_disp_order_hint = buf->ref_display_order_hint[map_idx];
  }

  // If the order_hint is above the threshold distance of 35 frames (largest
  // possible lag_in_frames) from the found reference frame, we assume it was
  // modified using:
  //     order_hint = display_order_hint % display_order_hint_factor
  // Here, the actual display_order_hint is recovered.
  const int display_order_hint_factor =
      1 << (cm->seq_params.order_hint_info.order_hint_bits_minus_1 + 1);
  int disp_order_hint = buf->order_hint;
  while (abs(max_disp_order_hint - disp_order_hint) > 35) {
    if (disp_order_hint > max_disp_order_hint) return disp_order_hint;

    disp_order_hint += display_order_hint_factor;
  }
  return disp_order_hint;
}
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC

// On success, returns 0. On failure, calls aom_internal_error and does not
// return.
static int read_uncompressed_header(AV1Decoder *pbi,
                                    struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;
  MACROBLOCKD *const xd = &pbi->dcb.xd;
  BufferPool *const pool = cm->buffer_pool;
  RefCntBuffer *const frame_bufs = pool->frame_bufs;
  aom_s_frame_info *sframe_info = &pbi->sframe_info;
  sframe_info->is_s_frame = 0;
  sframe_info->is_s_frame_at_altref = 0;

  if (!pbi->sequence_header_ready) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "No sequence header");
  }

  if (seq_params->reduced_still_picture_hdr) {
    cm->show_existing_frame = 0;
    cm->show_frame = 1;
    current_frame->frame_type = KEY_FRAME;
    if (pbi->sequence_header_changed) {
      // This is the start of a new coded video sequence.
      pbi->sequence_header_changed = 0;
      pbi->decoding_first_frame = 1;
      reset_frame_buffers(cm);
    }
    features->error_resilient_mode = 1;
  } else {
    cm->show_existing_frame = aom_rb_read_bit(rb);
    pbi->reset_decoder_state = 0;

    if (cm->show_existing_frame) {
      if (pbi->sequence_header_changed) {
        aom_internal_error(
            &cm->error, AOM_CODEC_CORRUPT_FRAME,
            "New sequence header starts with a show_existing_frame.");
      }
      // Show an existing frame directly.
      const int existing_frame_idx = aom_rb_read_literal(rb, 3);
      RefCntBuffer *const frame_to_show = cm->ref_frame_map[existing_frame_idx];
      if (frame_to_show == NULL) {
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Buffer does not contain a decoded frame");
      }
      if (seq_params->decoder_model_info_present_flag &&
          seq_params->timing_info.equal_picture_interval == 0) {
        read_temporal_point_info(cm, rb);
      }
      if (seq_params->frame_id_numbers_present_flag) {
        int frame_id_length = seq_params->frame_id_length;
        int display_frame_id = aom_rb_read_literal(rb, frame_id_length);
        /* Compare display_frame_id with ref_frame_id and check valid for
         * referencing */
        if (display_frame_id != cm->ref_frame_id[existing_frame_idx] ||
            pbi->valid_for_referencing[existing_frame_idx] == 0)
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Reference buffer frame ID mismatch");
      }
      lock_buffer_pool(pool);
      assert(frame_to_show->ref_count > 0);
      // cm->cur_frame should be the buffer referenced by the return value
      // of the get_free_fb() call in assign_cur_frame_new_fb() (called by
      // av1_receive_compressed_data()), so the ref_count should be 1.
      assert(cm->cur_frame->ref_count == 1);
      // assign_frame_buffer_p() decrements ref_count directly rather than
      // call decrease_ref_count(). If cm->cur_frame->raw_frame_buffer has
      // already been allocated, it will not be released by
      // assign_frame_buffer_p()!
      assert(!cm->cur_frame->raw_frame_buffer.data);

      FrameHash raw_frame_hash = cm->cur_frame->raw_frame_hash;
      FrameHash grain_frame_hash = cm->cur_frame->grain_frame_hash;

      assign_frame_buffer_p(&cm->cur_frame, frame_to_show);
      pbi->reset_decoder_state = frame_to_show->frame_type == KEY_FRAME;

      // Combine any Decoded Frame Header metadata that was parsed before
      // the referenced frame with any parsed before this
      // show_existing_frame header, e.g. raw frame hash values before the
      // referenced coded frame and post film grain hash values before this
      // header.
      if (raw_frame_hash.is_present)
        cm->cur_frame->raw_frame_hash = raw_frame_hash;
      if (grain_frame_hash.is_present)
        cm->cur_frame->grain_frame_hash = grain_frame_hash;
      unlock_buffer_pool(pool);

      cm->lf.filter_level[0] = 0;
      cm->lf.filter_level[1] = 0;
      cm->show_frame = 1;

      // Section 6.8.2: It is a requirement of bitstream conformance that when
      // show_existing_frame is used to show a previous frame, that the value
      // of showable_frame for the previous frame was equal to 1.
      if (!frame_to_show->showable_frame) {
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Buffer does not contain a showable frame");
      }
      // Section 6.8.2: It is a requirement of bitstream conformance that when
      // show_existing_frame is used to show a previous frame with
      // RefFrameType[ frame_to_show_map_idx ] equal to KEY_FRAME, that the
      // frame is output via the show_existing_frame mechanism at most once.
      if (pbi->reset_decoder_state) frame_to_show->showable_frame = 0;

      cm->film_grain_params = frame_to_show->film_grain_params;

      if (pbi->reset_decoder_state) {
        show_existing_frame_reset(pbi, existing_frame_idx);
      } else {
        current_frame->refresh_frame_flags = 0;
      }

      return 0;
    }

    current_frame->frame_type = (FRAME_TYPE)aom_rb_read_literal(rb, 2);
    if (pbi->sequence_header_changed) {
      if (current_frame->frame_type == KEY_FRAME) {
        // This is the start of a new coded video sequence.
        pbi->sequence_header_changed = 0;
        pbi->decoding_first_frame = 1;
        reset_frame_buffers(cm);
      } else {
        aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                           "Sequence header has changed without a keyframe.");
      }
    }

    cm->show_frame = aom_rb_read_bit(rb);
    if (cm->show_frame == 0) pbi->is_arf_frame_present = 1;
    if (cm->show_frame == 0 && cm->current_frame.frame_type == KEY_FRAME)
      pbi->is_fwd_kf_present = 1;
    if (cm->current_frame.frame_type == S_FRAME) {
      sframe_info->is_s_frame = 1;
      sframe_info->is_s_frame_at_altref = cm->show_frame ? 0 : 1;
    }
    if (seq_params->still_picture &&
        (current_frame->frame_type != KEY_FRAME || !cm->show_frame)) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Still pictures must be coded as shown keyframes");
    }
    cm->showable_frame = current_frame->frame_type != KEY_FRAME;
    if (cm->show_frame) {
      if (seq_params->decoder_model_info_present_flag &&
          seq_params->timing_info.equal_picture_interval == 0)
        read_temporal_point_info(cm, rb);
    } else {
      // See if this frame can be used as show_existing_frame in future
      cm->showable_frame = aom_rb_read_bit(rb);
    }
    cm->cur_frame->showable_frame = cm->showable_frame;
    features->error_resilient_mode =
        frame_is_sframe(cm) ||
                (current_frame->frame_type == KEY_FRAME && cm->show_frame)
            ? 1
            : aom_rb_read_bit(rb);
  }

  av1_set_frame_sb_size(cm, cm->seq_params.sb_size);

  if (current_frame->frame_type == KEY_FRAME && cm->show_frame) {
    /* All frames need to be marked as not valid for referencing */
    for (int i = 0; i < REF_FRAMES; i++) {
      pbi->valid_for_referencing[i] = 0;
    }
  }
  features->disable_cdf_update = aom_rb_read_bit(rb);

  if (seq_params->force_screen_content_tools == 2) {
    features->allow_screen_content_tools = aom_rb_read_bit(rb);
  } else {
    features->allow_screen_content_tools =
        seq_params->force_screen_content_tools;
  }

  if (features->allow_screen_content_tools) {
    if (seq_params->force_integer_mv == 2) {
      features->cur_frame_force_integer_mv = aom_rb_read_bit(rb);
    } else {
      features->cur_frame_force_integer_mv = seq_params->force_integer_mv;
    }
  } else {
    features->cur_frame_force_integer_mv = 0;
  }

  int frame_size_override_flag = 0;
  features->allow_intrabc = 0;
#if CONFIG_IBC_SR_EXT
  features->allow_global_intrabc = 0;
  features->allow_local_intrabc = 0;
#endif  // CONFIG_IBC_SR_EXT
  features->primary_ref_frame = PRIMARY_REF_NONE;

#if CONFIG_PRIMARY_REF_FRAME_OPT
  int signal_primary_ref_frame = -1;
  features->derived_primary_ref_frame = PRIMARY_REF_NONE;
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT

  if (!seq_params->reduced_still_picture_hdr) {
    if (seq_params->frame_id_numbers_present_flag) {
      int frame_id_length = seq_params->frame_id_length;
      int diff_len = seq_params->delta_frame_id_length;
      int prev_frame_id = 0;
      int have_prev_frame_id =
          !pbi->decoding_first_frame &&
          !(current_frame->frame_type == KEY_FRAME && cm->show_frame);
      if (have_prev_frame_id) {
        prev_frame_id = cm->current_frame_id;
      }
      cm->current_frame_id = aom_rb_read_literal(rb, frame_id_length);

      if (have_prev_frame_id) {
        int diff_frame_id;
        if (cm->current_frame_id > prev_frame_id) {
          diff_frame_id = cm->current_frame_id - prev_frame_id;
        } else {
          diff_frame_id =
              (1 << frame_id_length) + cm->current_frame_id - prev_frame_id;
        }
        /* Check current_frame_id for conformance */
        if (prev_frame_id == cm->current_frame_id ||
            diff_frame_id >= (1 << (frame_id_length - 1))) {
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Invalid value of current_frame_id");
        }
      }
      /* Check if some frames need to be marked as not valid for referencing */
      for (int i = 0; i < REF_FRAMES; i++) {
        if (cm->current_frame_id - (1 << diff_len) > 0) {
          if (cm->ref_frame_id[i] > cm->current_frame_id ||
              cm->ref_frame_id[i] < cm->current_frame_id - (1 << diff_len))
            pbi->valid_for_referencing[i] = 0;
        } else {
          if (cm->ref_frame_id[i] > cm->current_frame_id &&
              cm->ref_frame_id[i] < (1 << frame_id_length) +
                                        cm->current_frame_id - (1 << diff_len))
            pbi->valid_for_referencing[i] = 0;
        }
      }
    }

    frame_size_override_flag = frame_is_sframe(cm) ? 1 : aom_rb_read_bit(rb);

    current_frame->order_hint = aom_rb_read_literal(
        rb, seq_params->order_hint_info.order_hint_bits_minus_1 + 1);

    current_frame->display_order_hint = get_disp_order_hint(cm);
    current_frame->frame_number = current_frame->order_hint;

    if (!features->error_resilient_mode && !frame_is_intra_only(cm)) {
#if CONFIG_PRIMARY_REF_FRAME_OPT
      signal_primary_ref_frame = aom_rb_read_literal(rb, 1);
      if (signal_primary_ref_frame)
        features->primary_ref_frame = aom_rb_read_literal(rb, PRIMARY_REF_BITS);
#else
      features->primary_ref_frame = aom_rb_read_literal(rb, PRIMARY_REF_BITS);
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
    }
  }

  if (seq_params->decoder_model_info_present_flag) {
    cm->buffer_removal_time_present = aom_rb_read_bit(rb);
    if (cm->buffer_removal_time_present) {
      for (int op_num = 0;
           op_num < seq_params->operating_points_cnt_minus_1 + 1; op_num++) {
        if (seq_params->op_params[op_num].decoder_model_param_present_flag) {
          if ((((seq_params->operating_point_idc[op_num] >>
                 cm->temporal_layer_id) &
                0x1) &&
               ((seq_params->operating_point_idc[op_num] >>
                 (cm->spatial_layer_id + 8)) &
                0x1)) ||
              seq_params->operating_point_idc[op_num] == 0) {
            cm->buffer_removal_times[op_num] = aom_rb_read_unsigned_literal(
                rb, seq_params->decoder_model_info.buffer_removal_time_length);
          } else {
            cm->buffer_removal_times[op_num] = 0;
          }
        } else {
          cm->buffer_removal_times[op_num] = 0;
        }
      }
    }
  }
#if CONFIG_REFRESH_FLAG
  const int short_refresh_frame_flags =
      cm->seq_params.enable_short_refresh_frame_flags &&
      !cm->features.error_resilient_mode;
  const int refresh_frame_flags_bits =
      short_refresh_frame_flags ? 3 : REF_FRAMES;
#endif  // CONFIG_REFRESH_FLAG
  if (current_frame->frame_type == KEY_FRAME) {
    if (!cm->show_frame) {  // unshown keyframe (forward keyframe)
#if CONFIG_REFRESH_FLAG
      if (short_refresh_frame_flags) {
        const int refresh_idx =
            aom_rb_read_literal(rb, refresh_frame_flags_bits);
        if (refresh_idx == 0) {
          const bool has_refresh_frame_flags = aom_rb_read_literal(rb, 1);
          current_frame->refresh_frame_flags = has_refresh_frame_flags ? 1 : 0;
        } else {
          current_frame->refresh_frame_flags = 1 << refresh_idx;
        }
      } else {
        current_frame->refresh_frame_flags =
            aom_rb_read_literal(rb, refresh_frame_flags_bits);
      }
#else
      current_frame->refresh_frame_flags = aom_rb_read_literal(rb, REF_FRAMES);
#endif        // CONFIG_REFRESH_FLAG
    } else {  // shown keyframe
      current_frame->refresh_frame_flags = REFRESH_FRAME_ALL;
    }

    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      cm->remapped_ref_idx[i] = INVALID_IDX;
    }
    if (pbi->need_resync) {
      reset_ref_frame_map(cm);
      pbi->need_resync = 0;
    }
  } else {
    if (current_frame->frame_type == INTRA_ONLY_FRAME) {
#if CONFIG_REFRESH_FLAG
      if (short_refresh_frame_flags) {
        const int refresh_idx =
            aom_rb_read_literal(rb, refresh_frame_flags_bits);
        if (refresh_idx == 0) {
          const bool has_refresh_frame_flags = aom_rb_read_literal(rb, 1);
          current_frame->refresh_frame_flags = has_refresh_frame_flags ? 1 : 0;
        } else {
          current_frame->refresh_frame_flags = 1 << refresh_idx;
        }
      } else {
        current_frame->refresh_frame_flags =
            aom_rb_read_literal(rb, refresh_frame_flags_bits);
      }
#else
      current_frame->refresh_frame_flags = aom_rb_read_literal(rb, REF_FRAMES);
#endif  // CONFIG_REFRESH_FLAG
      if (current_frame->refresh_frame_flags == REFRESH_FRAME_ALL) {
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Intra only frames cannot have refresh flags 0xFF");
      }
      if (pbi->need_resync) {
        reset_ref_frame_map(cm);
        pbi->need_resync = 0;
      }
    } else if (pbi->need_resync != 1) { /* Skip if need resync */
#if CONFIG_REFRESH_FLAG
      if (frame_is_sframe(cm)) {
        current_frame->refresh_frame_flags = REFRESH_FRAME_ALL;
      } else {
        if (short_refresh_frame_flags) {
          const int refresh_idx =
              aom_rb_read_literal(rb, refresh_frame_flags_bits);
          if (refresh_idx == 0) {
            const bool has_refresh_frame_flags = aom_rb_read_literal(rb, 1);
            current_frame->refresh_frame_flags =
                has_refresh_frame_flags ? 1 : 0;
          } else {
            current_frame->refresh_frame_flags = 1 << refresh_idx;
          }
        } else {
          current_frame->refresh_frame_flags =
              aom_rb_read_literal(rb, refresh_frame_flags_bits);
        }
      }
#else
      current_frame->refresh_frame_flags =
          frame_is_sframe(cm) ? REFRESH_FRAME_ALL
                              : aom_rb_read_literal(rb, REF_FRAMES);
#endif  // CONFIG_REFRESH_FLAG
    }
  }

  if (!frame_is_intra_only(cm) ||
      current_frame->refresh_frame_flags != REFRESH_FRAME_ALL) {
    // Read all ref frame order hints if error_resilient_mode == 1
    if (features->error_resilient_mode &&
        seq_params->order_hint_info.enable_order_hint) {
      for (int ref_idx = 0; ref_idx < REF_FRAMES; ref_idx++) {
        // Read order hint from bit stream
        unsigned int order_hint = aom_rb_read_literal(
            rb, seq_params->order_hint_info.order_hint_bits_minus_1 + 1);
        // Get buffer
        RefCntBuffer *buf = cm->ref_frame_map[ref_idx];
        if (buf == NULL || order_hint != buf->order_hint) {
          if (buf != NULL) {
            lock_buffer_pool(pool);
            decrease_ref_count(buf, pool);
            unlock_buffer_pool(pool);
            cm->ref_frame_map[ref_idx] = NULL;
          }
          // If no corresponding buffer exists, allocate a new buffer with all
          // pixels set to neutral grey.
          int buf_idx = get_free_fb(cm);
          if (buf_idx == INVALID_IDX) {
            aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                               "Unable to find free frame buffer");
          }
          buf = &frame_bufs[buf_idx];
          lock_buffer_pool(pool);
          if (aom_realloc_frame_buffer(
                  &buf->buf, seq_params->max_frame_width,
                  seq_params->max_frame_height, seq_params->subsampling_x,
                  seq_params->subsampling_y, AOM_BORDER_IN_PIXELS,
                  features->byte_alignment, &buf->raw_frame_buffer,
                  pool->get_fb_cb, pool->cb_priv, 0)) {
            decrease_ref_count(buf, pool);
            unlock_buffer_pool(pool);
            aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                               "Failed to allocate frame buffer");
          }
          unlock_buffer_pool(pool);
          // According to the specification, valid bitstreams are required to
          // never use missing reference frames so the filling process for
          // missing frames is not normatively defined and RefValid for missing
          // frames is set to 0.

          // To make libaom more robust when the bitstream has been corrupted
          // by the loss of some frames of data, this code adds a neutral grey
          // buffer in place of missing frames, i.e.
          //
          set_planes_to_neutral_grey(seq_params, &buf->buf, 0);
          //
          // and allows the frames to be used for referencing, i.e.
          //
          pbi->valid_for_referencing[ref_idx] = 1;
          //
          // Please note such behavior is not normative and other decoders may
          // use a different approach.
          cm->ref_frame_map[ref_idx] = buf;
          buf->order_hint = order_hint;
          // TODO(kslu) This is a workaround for error resilient mode. Make
          // it more consistent with get_disp_order_hint().
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
          buf->display_order_hint = get_ref_frame_disp_order_hint(cm, buf);
#else
          buf->display_order_hint = order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
        }
      }
    }
    if (features->error_resilient_mode) {
      // Read all ref frame base_qindex
      for (int ref_idx = 0; ref_idx < REF_FRAMES; ref_idx++) {
        RefCntBuffer *buf = cm->ref_frame_map[ref_idx];
        buf->base_qindex = aom_rb_read_literal(
            rb, cm->seq_params.bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT
                                                       : QINDEX_BITS);
      }
    }
  }

  if (current_frame->frame_type == KEY_FRAME) {
    cm->current_frame.pyramid_level = 1;
    features->tip_frame_mode = TIP_FRAME_DISABLED;
    setup_frame_size(cm, frame_size_override_flag, rb);

    if (features->allow_screen_content_tools && !av1_superres_scaled(cm))
      features->allow_intrabc = aom_rb_read_bit(rb);
#if CONFIG_IBC_SR_EXT
    if (features->allow_intrabc) {
      features->allow_global_intrabc = aom_rb_read_bit(rb);
      features->allow_local_intrabc =
          features->allow_global_intrabc ? aom_rb_read_bit(rb) : 1;
#if CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_MAX_DRL
      features->max_bvp_drl_bits =
          aom_rb_read_primitive_quniform(
              rb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1) +
          MIN_MAX_IBC_DRL_BITS;
#else
      features->max_drl_bits =
          aom_rb_read_primitive_quniform(
              rb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1) +
          MIN_MAX_DRL_BITS;
#endif  // CONFIG_IBC_MAX_DRL
#endif  // CONFIG_IBC_BV_IMPROVEMENT
    }
#endif  // CONFIG_IBC_SR_EXT

    features->allow_ref_frame_mvs = 0;
    cm->prev_frame = NULL;

#if CONFIG_IMPROVED_GLOBAL_MOTION
    cm->cur_frame->num_ref_frames = 0;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
  } else {
    features->allow_ref_frame_mvs = 0;
    features->tip_frame_mode = TIP_FRAME_DISABLED;
    if (current_frame->frame_type == INTRA_ONLY_FRAME) {
      cm->cur_frame->film_grain_params_present =
          seq_params->film_grain_params_present;
      setup_frame_size(cm, frame_size_override_flag, rb);
      if (features->allow_screen_content_tools && !av1_superres_scaled(cm))
        features->allow_intrabc = aom_rb_read_bit(rb);
#if CONFIG_IBC_SR_EXT
      if (features->allow_intrabc) {
        features->allow_global_intrabc = aom_rb_read_bit(rb);
        features->allow_local_intrabc =
            features->allow_global_intrabc ? aom_rb_read_bit(rb) : 1;
#if CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_MAX_DRL
        features->max_bvp_drl_bits =
            aom_rb_read_primitive_quniform(
                rb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1) +
            MIN_MAX_IBC_DRL_BITS;
#else
        features->max_drl_bits =
            aom_rb_read_primitive_quniform(
                rb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1) +
            MIN_MAX_DRL_BITS;
#endif  // CONFIG_IBC_MAX_DRL
#endif  // CONFIG_IBC_BV_IMPROVEMENT
      }
#endif  // CONFIG_IBC_SR_EXT

#if CONFIG_IMPROVED_GLOBAL_MOTION
      cm->cur_frame->num_ref_frames = 0;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

    } else if (pbi->need_resync != 1) { /* Skip if need resync */
      // Implicitly derive the reference mapping
#if CONFIG_PRIMARY_REF_FRAME_OPT
      init_ref_map_pair(cm, cm->ref_frame_map_pairs,
                        current_frame->frame_type == KEY_FRAME);
      int n_ranked = av1_get_ref_frames(cm, current_frame->display_order_hint,
                                        cm->ref_frame_map_pairs);
#else
      RefFrameMapPair ref_frame_map_pairs[REF_FRAMES];
      init_ref_map_pair(cm, ref_frame_map_pairs,
                        current_frame->frame_type == KEY_FRAME);
      int n_ranked = av1_get_ref_frames(cm, current_frame->display_order_hint,
                                        ref_frame_map_pairs);
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT

      // Reference rankings have been implicitly derived in av1_get_ref_frames.
      // However, reference indices can be overwritten if they have been
      // signaled, which happens in error resilient mode or when order hint
      // is unavailable.
      const int explicit_ref_frame_map =
          cm->features.error_resilient_mode || frame_is_sframe(cm) ||
          seq_params->explicit_ref_frame_map ||
          !seq_params->order_hint_info.enable_order_hint;
      if (explicit_ref_frame_map) {
        cm->ref_frames_info.num_total_refs =
            aom_rb_read_literal(rb, REF_FRAMES_LOG2);
        // Check whether num_total_refs read is valid and not greater than
        // n_ranked (using a reference frame more than once is not allowed).
        if (cm->ref_frames_info.num_total_refs <= 0 ||
            (seq_params->order_hint_info.enable_order_hint &&
             cm->ref_frames_info.num_total_refs > n_ranked) ||
            cm->ref_frames_info.num_total_refs >
                seq_params->max_reference_frames)
          aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                             "Invalid num_total_refs");
      }

#if CONFIG_ALLOW_SAME_REF_COMPOUND
      cm->ref_frames_info.num_same_ref_compound =
          AOMMIN(cm->seq_params.num_same_ref_compound,
                 cm->ref_frames_info.num_total_refs);
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND

      if (features->primary_ref_frame >= cm->ref_frames_info.num_total_refs &&
          features->primary_ref_frame != PRIMARY_REF_NONE) {
        aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                           "Invalid primary_ref_frame");
      }
      for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i) {
        int ref = 0;
        if (!explicit_ref_frame_map) {
          ref = cm->remapped_ref_idx[i];
          if (cm->ref_frame_map[ref] == NULL)
            aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                               "Inter frame requests nonexistent reference");
        } else {
          ref = aom_rb_read_literal(rb, REF_FRAMES_LOG2);

          // Most of the time, streams start with a keyframe. In that case,
          // ref_frame_map will have been filled in at that point and will not
          // contain any NULLs. However, streams are explicitly allowed to start
          // with an intra-only frame, so long as they don't then signal a
          // reference to a slot that hasn't been set yet. That's what we are
          // checking here.
          if (cm->ref_frame_map[ref] == NULL)
            aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                               "Inter frame requests nonexistent reference");
          cm->remapped_ref_idx[i] = ref;
        }
        // Check valid for referencing
        if (pbi->valid_for_referencing[ref] == 0)
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Reference frame not valid for referencing");

        if (seq_params->frame_id_numbers_present_flag) {
          int frame_id_length = seq_params->frame_id_length;
          int diff_len = seq_params->delta_frame_id_length;
          int delta_frame_id_minus_1 = aom_rb_read_literal(rb, diff_len);
          int ref_frame_id =
              ((cm->current_frame_id - (delta_frame_id_minus_1 + 1) +
                (1 << frame_id_length)) %
               (1 << frame_id_length));
          // Compare values derived from delta_frame_id_minus_1 and
          // refresh_frame_flags.
          if (ref_frame_id != cm->ref_frame_id[ref])
            aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                               "Reference buffer frame ID mismatch");
        }
      }
      // With explicit_ref_frame_map, cm->remapped_ref_idx has been
      // overwritten. The reference lists also needs to be reset.
      if (explicit_ref_frame_map) {
        RefScoreData scores[REF_FRAMES];
        for (int i = 0; i < REF_FRAMES; i++) scores[i].score = INT_MAX;
        for (int i = 0; i < cm->ref_frames_info.num_total_refs; i++) {
          scores[i].score = i;
          int ref = cm->remapped_ref_idx[i];
          scores[i].distance =
              seq_params->order_hint_info.enable_order_hint
                  ? ((int)current_frame->display_order_hint -
#if CONFIG_PRIMARY_REF_FRAME_OPT
                     (int)cm->ref_frame_map_pairs[ref].disp_order)
#else
                     (int)ref_frame_map_pairs[ref].disp_order)
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
                  : 1;
        }
        av1_get_past_future_cur_ref_lists(cm, scores);
      }
#if CONFIG_IMPROVED_GLOBAL_MOTION
      cm->cur_frame->num_ref_frames = cm->ref_frames_info.num_total_refs;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

      if (!features->error_resilient_mode && frame_size_override_flag) {
        setup_frame_size_with_refs(cm, rb);
      } else {
        setup_frame_size(cm, frame_size_override_flag, rb);
      }

      if (frame_might_allow_ref_frame_mvs(cm))
        features->allow_ref_frame_mvs = aom_rb_read_bit(rb);
      else
        features->allow_ref_frame_mvs = 0;

      features->allow_pef = false;
      if (cm->seq_params.enable_pef) {
        features->allow_pef = aom_rb_read_bit(rb);
        if (features->allow_pef) {
          cm->pef_params.pef_delta = aom_rb_read_bit(rb) + 1;
        }
      }

#if CONFIG_TIP_DIRECT_FRAME_MV
      cm->tip_global_motion.as_int = 0;
      cm->tip_interp_filter = MULTITAP_SHARP;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
      if (cm->seq_params.enable_tip) {
        features->tip_frame_mode = aom_rb_read_literal(rb, 2);
#if CONFIG_OPTFLOW_ON_TIP
        features->use_optflow_tip = 1;
#endif  // CONFIG_OPTFLOW_ON_TIP
        if (features->tip_frame_mode >= TIP_FRAME_MODES) {
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Invalid TIP mode.");
        }
        if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT &&
            av1_superres_scaled(cm)) {
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Invalid TIP Direct mode with superres.");
        }

        if (features->tip_frame_mode && cm->seq_params.enable_tip_hole_fill) {
          features->allow_tip_hole_fill = aom_rb_read_bit(rb);
        } else {
          features->allow_tip_hole_fill = false;
        }
#if CONFIG_TIP_DIRECT_FRAME_MV
        if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
          int all_zero = aom_rb_read_bit(rb);
          if (!all_zero) {
            cm->tip_global_motion.as_mv.row = aom_rb_read_literal(rb, 4);
            cm->tip_global_motion.as_mv.col = aom_rb_read_literal(rb, 4);
            if (cm->tip_global_motion.as_mv.row != 0) {
              int sign = aom_rb_read_bit(rb);
              if (sign) cm->tip_global_motion.as_mv.row *= -1;
            }
            if (cm->tip_global_motion.as_mv.col != 0) {
              int sign = aom_rb_read_bit(rb);
              if (sign) cm->tip_global_motion.as_mv.col *= -1;
            }
          }
          cm->tip_interp_filter =
              aom_rb_read_bit(rb) ? MULTITAP_SHARP : EIGHTTAP_REGULAR;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
        }
      } else {
        features->tip_frame_mode = TIP_FRAME_DISABLED;
      }

      if (features->tip_frame_mode != TIP_FRAME_AS_OUTPUT) {
#if CONFIG_IBC_SR_EXT
        if (features->allow_screen_content_tools && !av1_superres_scaled(cm)) {
          features->allow_intrabc = aom_rb_read_bit(rb);
          features->allow_global_intrabc = 0;
          features->allow_local_intrabc = features->allow_intrabc;
        }
#endif  // CONFIG_IBC_SR_EXT

        features->max_drl_bits =
            aom_rb_read_primitive_quniform(
                rb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1) +
            MIN_MAX_DRL_BITS;
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        if (features->allow_intrabc) {
          features->max_bvp_drl_bits =
              aom_rb_read_primitive_quniform(
                  rb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1) +
              MIN_MAX_IBC_DRL_BITS;
        }
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL

#if CONFIG_FLEX_MVRES
        if (features->cur_frame_force_integer_mv) {
          features->fr_mv_precision = MV_PRECISION_ONE_PEL;
        } else {
          features->fr_mv_precision = aom_rb_read_bit(rb)
                                          ? MV_PRECISION_ONE_EIGHTH_PEL
                                          : MV_PRECISION_QTR_PEL;
          features->most_probable_fr_mv_precision = features->fr_mv_precision;
        }
        if (features->fr_mv_precision == MV_PRECISION_ONE_PEL) {
          features->use_pb_mv_precision = 0;
        } else {
          features->use_pb_mv_precision = cm->seq_params.enable_flex_mvres;
        }
#else
      if (features->cur_frame_force_integer_mv) {
        features->allow_high_precision_mv = 0;
      } else {
        features->allow_high_precision_mv = aom_rb_read_bit(rb);
      }
#endif

        features->interp_filter = read_frame_interp_filter(rb);
#if CONFIG_EXTENDED_WARP_PREDICTION
        int seq_enabled_motion_modes = cm->seq_params.seq_enabled_motion_modes;
        int frame_enabled_motion_modes = (1 << SIMPLE_TRANSLATION);
        for (int motion_mode = INTERINTRA; motion_mode < MOTION_MODES;
             motion_mode++) {
          if (seq_enabled_motion_modes & (1 << motion_mode)) {
            int enabled = aom_rb_read_bit(rb);
            if (enabled) {
              frame_enabled_motion_modes |= (1 << motion_mode);
            }
          }
        }
        features->enabled_motion_modes = frame_enabled_motion_modes;
#else
      features->switchable_motion_mode = aom_rb_read_bit(rb);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
#if CONFIG_OPTFLOW_REFINEMENT
        if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_AUTO) {
          features->opfl_refine_type = aom_rb_read_literal(rb, 2);
          if (features->opfl_refine_type == AOM_OPFL_REFINE_AUTO)
            aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                               "Invalid frame level optical flow refine type");
        } else {
          features->opfl_refine_type = cm->seq_params.enable_opfl_refine;
        }
#endif  // CONFIG_OPTFLOW_REFINEMENT
      }
    }

#if !CONFIG_PRIMARY_REF_FRAME_OPT
    cm->prev_frame = get_primary_ref_frame_buf(cm);
    if (features->primary_ref_frame != PRIMARY_REF_NONE &&
        get_primary_ref_frame_buf(cm) == NULL) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Reference frame containing this frame's initial "
                         "frame context is unavailable.");
    }
#endif  // !CONFIG_PRIMARY_REF_FRAME_OPT

    if (!(current_frame->frame_type == INTRA_ONLY_FRAME) &&
        pbi->need_resync != 1) {
      for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i) {
        const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, i);
        if (!ref_buf) continue;
        struct scale_factors *const ref_scale_factors =
            get_ref_scale_factors(cm, i);
        av1_setup_scale_factors_for_frame(
            ref_scale_factors, ref_buf->buf.y_crop_width,
            ref_buf->buf.y_crop_height, cm->width, cm->height);
        if ((!av1_is_valid_scale(ref_scale_factors)))
          aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                             "Reference frame has invalid dimensions");
      }

      if (cm->seq_params.enable_tip) {
        const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, TIP_FRAME);
        if (ref_buf) {
          struct scale_factors *const ref_scale_factors =
              get_ref_scale_factors(cm, TIP_FRAME);
          av1_setup_scale_factors_for_frame(
              ref_scale_factors, ref_buf->buf.y_crop_width,
              ref_buf->buf.y_crop_height, cm->width, cm->height);
          if ((!av1_is_valid_scale(ref_scale_factors)))
            aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                               "Reference frame has invalid dimensions");
        }
      }
    }
  }

  av1_setup_frame_buf_refs(cm);

  av1_setup_frame_sign_bias(cm);

  cm->cur_frame->frame_type = current_frame->frame_type;

  update_ref_frame_id(pbi);

  cm->cur_frame->buf.bit_depth = seq_params->bit_depth;
  cm->cur_frame->buf.color_primaries = seq_params->color_primaries;
  cm->cur_frame->buf.transfer_characteristics =
      seq_params->transfer_characteristics;
  cm->cur_frame->buf.matrix_coefficients = seq_params->matrix_coefficients;
  cm->cur_frame->buf.monochrome = seq_params->monochrome;
  cm->cur_frame->buf.chroma_sample_position =
      seq_params->chroma_sample_position;
  cm->cur_frame->buf.color_range = seq_params->color_range;
  cm->cur_frame->buf.render_width = cm->render_width;
  cm->cur_frame->buf.render_height = cm->render_height;

#if CONFIG_TIP_DIRECT_FRAME_MV
  YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tmp_tip_frame->buf;
#else
YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tip_frame->buf;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
  tip_frame_buf->bit_depth = seq_params->bit_depth;
  tip_frame_buf->color_primaries = seq_params->color_primaries;
  tip_frame_buf->transfer_characteristics =
      seq_params->transfer_characteristics;
  tip_frame_buf->matrix_coefficients = seq_params->matrix_coefficients;
  tip_frame_buf->monochrome = seq_params->monochrome;
  tip_frame_buf->chroma_sample_position = seq_params->chroma_sample_position;
  tip_frame_buf->color_range = seq_params->color_range;
  tip_frame_buf->render_width = cm->render_width;
  tip_frame_buf->render_height = cm->render_height;

  if (pbi->need_resync) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Keyframe / intra-only frame required to reset decoder"
                       " state");
  }

  if (is_global_intrabc_allowed(cm) ||
      features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    // Set parameters corresponding to no filtering.
    struct loopfilter *lf = &cm->lf;
    lf->filter_level[0] = 0;
    lf->filter_level[1] = 0;
#if CONFIG_FIX_CDEF_SYNTAX
    cm->cdef_info.cdef_frame_enable = 0;
#else
  cm->cdef_info.cdef_bits = 0;
  cm->cdef_info.cdef_strengths[0] = 0;
  cm->cdef_info.nb_cdef_strengths = 1;
  cm->cdef_info.cdef_uv_strengths[0] = 0;
#endif  // CONFIG_FIX_CDEF_SYNTAX
    cm->rst_info[0].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_restoration_type = RESTORE_NONE;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    cm->rst_info[0].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_cross_restoration_type = RESTORE_NONE;
#endif
  }

  if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
#if CONFIG_TIP_IMPLICIT_QUANT
    if (cm->seq_params.enable_tip_explicit_qp) {
      cm->quant_params.base_qindex = aom_rb_read_literal(
          rb, cm->seq_params.bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT
                                                     : QINDEX_BITS);
      if (av1_num_planes(cm) > 1) {
        int diff_uv_delta = 0;
        if (cm->seq_params.separate_uv_delta_q) {
          diff_uv_delta = aom_rb_read_bit(rb);
        }
        cm->quant_params.u_ac_delta_q = read_delta_q(rb);
        if (diff_uv_delta) {
          cm->quant_params.v_ac_delta_q = read_delta_q(rb);
        } else {
          cm->quant_params.v_ac_delta_q = cm->quant_params.u_ac_delta_q;
        }
      } else {
        cm->quant_params.v_ac_delta_q = cm->quant_params.u_ac_delta_q = 0;
      }
      cm->cur_frame->base_qindex = cm->quant_params.base_qindex;
      cm->cur_frame->u_ac_delta_q = cm->quant_params.u_ac_delta_q;
      cm->cur_frame->v_ac_delta_q = cm->quant_params.v_ac_delta_q;
    }
#else
  cm->quant_params.base_qindex = aom_rb_read_literal(
      rb,
      cm->seq_params.bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT : QINDEX_BITS);
  cm->cur_frame->base_qindex = cm->quant_params.base_qindex;
#endif  // CONFIG_TIP_IMPLICIT_QUANT
    features->refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;
    read_tile_info(pbi, rb);
    cm->cur_frame->film_grain_params_present =
        seq_params->film_grain_params_present;
    read_film_grain(cm, rb);
    av1_setup_past_independence(cm);
    if (!cm->tiles.large_scale) {
      cm->cur_frame->frame_context = *cm->fc;
    }
    // TIP frame will be output for displaying
    // No futher processing needed
    return 0;
  }

  const int might_bwd_adapt = !(seq_params->reduced_still_picture_hdr) &&
                              !(features->disable_cdf_update);
  if (might_bwd_adapt) {
    features->refresh_frame_context = aom_rb_read_bit(rb)
                                          ? REFRESH_FRAME_CONTEXT_DISABLED
                                          : REFRESH_FRAME_CONTEXT_BACKWARD;
  } else {
    features->refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;
  }

  read_tile_info(pbi, rb);
  if (!av1_is_min_tile_width_satisfied(cm)) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Minimum tile width requirement not satisfied");
  }

  CommonQuantParams *const quant_params = &cm->quant_params;
  setup_quantization(quant_params, av1_num_planes(cm), cm->seq_params.bit_depth,
                     cm->seq_params.separate_uv_delta_q, rb);
  cm->cur_frame->base_qindex = quant_params->base_qindex;
#if CONFIG_TIP_IMPLICIT_QUANT
  cm->cur_frame->u_ac_delta_q = quant_params->u_ac_delta_q;
  cm->cur_frame->v_ac_delta_q = quant_params->v_ac_delta_q;
#endif  // CONFIG_TIP_IMPLICIT_QUANT
  xd->bd = (int)seq_params->bit_depth;

#if CONFIG_PRIMARY_REF_FRAME_OPT
  if (!seq_params->reduced_still_picture_hdr) {
    features->derived_primary_ref_frame = choose_primary_ref_frame(cm);

    if (!signal_primary_ref_frame)
      features->primary_ref_frame = features->derived_primary_ref_frame;
  }

  // For primary_ref_frame and derived_primary_ref_frame, if one of them is
  // PRIMARY_REF_NONE, the other one is also PRIMARY_REF_NONE.
  if (features->derived_primary_ref_frame == PRIMARY_REF_NONE ||
      features->primary_ref_frame == PRIMARY_REF_NONE) {
    features->primary_ref_frame = PRIMARY_REF_NONE;
    features->derived_primary_ref_frame = PRIMARY_REF_NONE;
  }
  assert(IMPLIES(features->derived_primary_ref_frame == PRIMARY_REF_NONE,
                 features->primary_ref_frame == PRIMARY_REF_NONE));
  assert(IMPLIES(features->primary_ref_frame == PRIMARY_REF_NONE,
                 features->derived_primary_ref_frame == PRIMARY_REF_NONE));

  if (features->primary_ref_frame >= cm->ref_frames_info.num_total_refs &&
      features->primary_ref_frame != PRIMARY_REF_NONE) {
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Invalid primary_ref_frame");
  }

  if (current_frame->frame_type != KEY_FRAME) {
    cm->prev_frame =
        get_primary_ref_frame_buf(cm, features->derived_primary_ref_frame);
    if (features->derived_primary_ref_frame != PRIMARY_REF_NONE &&
        get_primary_ref_frame_buf(cm, features->derived_primary_ref_frame) ==
            NULL) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Reference frame containing this frame's initial "
                         "frame context is unavailable.");
    }
  }
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT

  CommonContexts *const above_contexts = &cm->above_contexts;
  if (above_contexts->num_planes < av1_num_planes(cm) ||
      above_contexts->num_mi_cols < cm->mi_params.mi_cols ||
      above_contexts->num_tile_rows < cm->tiles.rows) {
    av1_free_above_context_buffers(above_contexts);
    if (av1_alloc_above_context_buffers(above_contexts, cm->tiles.rows,
                                        cm->mi_params.mi_cols,
                                        av1_num_planes(cm))) {
      aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate context buffers");
    }
  }

  if (features->primary_ref_frame == PRIMARY_REF_NONE) {
    av1_setup_past_independence(cm);
  }

  setup_segmentation(cm, rb);

  cm->delta_q_info.delta_q_res = 1;
  cm->delta_q_info.delta_lf_res = 1;
  cm->delta_q_info.delta_lf_present_flag = 0;
  cm->delta_q_info.delta_lf_multi = 0;
  cm->delta_q_info.delta_q_present_flag =
      quant_params->base_qindex > 0 ? aom_rb_read_bit(rb) : 0;
  if (cm->delta_q_info.delta_q_present_flag) {
    xd->current_base_qindex = quant_params->base_qindex;
    cm->delta_q_info.delta_q_res = 1 << aom_rb_read_literal(rb, 2);
    if (!is_global_intrabc_allowed(cm))
      cm->delta_q_info.delta_lf_present_flag = aom_rb_read_bit(rb);
    if (cm->delta_q_info.delta_lf_present_flag) {
      cm->delta_q_info.delta_lf_res = 1 << aom_rb_read_literal(rb, 2);
      cm->delta_q_info.delta_lf_multi = aom_rb_read_bit(rb);
      av1_reset_loop_filter_delta(xd, av1_num_planes(cm));
    }
  }

  xd->cur_frame_force_integer_mv = features->cur_frame_force_integer_mv;

  for (int i = 0; i < MAX_SEGMENTS; ++i) {
    const int qindex = av1_get_qindex(&cm->seg, i, quant_params->base_qindex,
                                      cm->seq_params.bit_depth);
    xd->lossless[i] =
        qindex == 0 &&
        (quant_params->y_dc_delta_q + cm->seq_params.base_y_dc_delta_q <= 0) &&
        (quant_params->u_dc_delta_q + cm->seq_params.base_uv_dc_delta_q <= 0) &&
        quant_params->u_ac_delta_q <= 0 &&
        (quant_params->v_dc_delta_q + cm->seq_params.base_uv_dc_delta_q <= 0) &&
        quant_params->v_ac_delta_q <= 0;
    xd->qindex[i] = qindex;
  }
  features->coded_lossless = is_coded_lossless(cm, xd);
  features->all_lossless = features->coded_lossless && !av1_superres_scaled(cm);
  setup_segmentation_dequant(cm, xd);
  if (features->coded_lossless) {
    cm->lf.filter_level[0] = 0;
    cm->lf.filter_level[1] = 0;
  }
  if (features->coded_lossless || !seq_params->enable_cdef) {
#if CONFIG_FIX_CDEF_SYNTAX
    cm->cdef_info.cdef_frame_enable = 0;
#else
  cm->cdef_info.cdef_bits = 0;
  cm->cdef_info.cdef_strengths[0] = 0;
  cm->cdef_info.cdef_uv_strengths[0] = 0;
#endif  // CONFIG_FIX_CDEF_SYNTAX
  }
  if (features->all_lossless || !seq_params->enable_restoration) {
    cm->rst_info[0].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_restoration_type = RESTORE_NONE;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    cm->rst_info[0].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_cross_restoration_type = RESTORE_NONE;
#endif
  }
  setup_loopfilter(cm, rb);

  if (!features->coded_lossless && seq_params->enable_cdef) {
    setup_cdef(cm, rb);
  }
  if (!features->all_lossless && seq_params->enable_restoration) {
    decode_restoration_mode(cm, rb);
  }
#if CONFIG_CCSO
  if (!features->coded_lossless && seq_params->enable_ccso) {
    setup_ccso(cm, rb);
  }
#endif

  if (features->coded_lossless || !cm->seq_params.enable_parity_hiding)
    features->allow_parity_hiding = false;
  else
    features->allow_parity_hiding = aom_rb_read_bit(rb);

  features->tx_mode = read_tx_mode(rb, features->coded_lossless);
  current_frame->reference_mode = read_frame_reference_mode(cm, rb);

  av1_setup_skip_mode_allowed(cm);
  current_frame->skip_mode_info.skip_mode_flag =
      current_frame->skip_mode_info.skip_mode_allowed ? aom_rb_read_bit(rb) : 0;

#if !CONFIG_EXTENDED_WARP_PREDICTION
  if (frame_might_allow_warped_motion(cm))
    features->allow_warped_motion = aom_rb_read_bit(rb);
  else
    features->allow_warped_motion = 0;
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_BAWP
  if (!frame_is_intra_only(cm) && seq_params->enable_bawp)
    features->enable_bawp = aom_rb_read_bit(rb);
  else
    features->enable_bawp = 0;
#endif  // CONFIG_BAWP

  features->enable_cwp = seq_params->enable_cwp;
#if CONFIG_EXTENDED_WARP_PREDICTION
  features->allow_warpmv_mode = 0;
  if (!frame_is_intra_only(cm) &&
      (features->enabled_motion_modes & (1 << WARP_DELTA)) != 0) {
    features->allow_warpmv_mode = aom_rb_read_bit(rb);
  }
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_D071_IMP_MSK_BLD
  features->enable_imp_msk_bld = seq_params->enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD

  features->reduced_tx_set_used = aom_rb_read_bit(rb);

  if (features->allow_ref_frame_mvs && !frame_might_allow_ref_frame_mvs(cm)) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Frame wrongly requests reference frame MVs");
  }

  if (features->tip_frame_mode && !cm->seq_params.enable_tip) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Frame wrongly requests TIP mode");
  }

  if (!frame_is_intra_only(cm)) read_global_motion(cm, rb);

  cm->cur_frame->film_grain_params_present =
      seq_params->film_grain_params_present;
  read_film_grain(cm, rb);

#if EXT_TILE_DEBUG
  if (pbi->ext_tile_debug && cm->tiles.large_scale) {
    read_ext_tile_info(pbi, rb);
    av1_set_single_tile_decoding_mode(cm);
  }
#endif  // EXT_TILE_DEBUG
  return 0;
}

struct aom_read_bit_buffer *av1_init_read_bit_buffer(
    AV1Decoder *pbi, struct aom_read_bit_buffer *rb, const uint8_t *data,
    const uint8_t *data_end) {
  rb->bit_offset = 0;
  rb->error_handler = error_handler;
  rb->error_handler_data = &pbi->common;
  rb->bit_buffer = data;
  rb->bit_buffer_end = data_end;
  return rb;
}

void av1_read_frame_size(struct aom_read_bit_buffer *rb, int num_bits_width,
                         int num_bits_height, int *width, int *height) {
  *width = aom_rb_read_literal(rb, num_bits_width) + 1;
  *height = aom_rb_read_literal(rb, num_bits_height) + 1;
}

BITSTREAM_PROFILE av1_read_profile(struct aom_read_bit_buffer *rb) {
  int profile = aom_rb_read_literal(rb, PROFILE_BITS);
  return (BITSTREAM_PROFILE)profile;
}

static AOM_INLINE void superres_post_decode(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  BufferPool *const pool = cm->buffer_pool;

  if (!av1_superres_scaled(cm)) return;
  assert(!cm->features.all_lossless);

  av1_superres_upscale(cm, pool, 0);
}

static AOM_INLINE void process_tip_mode(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &pbi->dcb.xd;

  if (cm->features.allow_ref_frame_mvs && cm->has_bwd_ref) {
    if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
      av1_dec_setup_tip_frame(cm, xd, pbi->td.mc_buf, pbi->td.tmp_conv_dst);
#if !CONFIG_TIP_DIRECT_FRAME_MV
      if (cm->seq_params.enable_pef && cm->features.allow_pef) {
        enhance_tip_frame(cm, xd);
      }
#endif  // !CONFIG_TIP_DIRECT_FRAME_MV
    } else if (cm->features.tip_frame_mode == TIP_FRAME_AS_REF) {
      av1_setup_tip_motion_field(cm, 0);
      const int mvs_rows =
          ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
      const int mvs_cols =
          ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
      av1_zero_array(cm->tip_ref.available_flag, mvs_rows * mvs_cols);
    }
  }

  if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    av1_copy_tip_frame_tmvp_mvs(cm);
    aom_yv12_copy_frame(&cm->tip_ref.tip_frame->buf, &cm->cur_frame->buf,
                        num_planes);
    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      cm->global_motion[i] = default_warp_params;
      cm->cur_frame->global_motion[i] = default_warp_params;
    }
    av1_setup_past_independence(cm);
    if (!cm->tiles.large_scale) {
      cm->cur_frame->frame_context = *cm->fc;
    }
  }
}

uint32_t av1_decode_frame_headers_and_setup(AV1Decoder *pbi,
                                            struct aom_read_bit_buffer *rb,
                                            const uint8_t *data,
                                            const uint8_t **p_data_end,
                                            int trailing_bits_present) {
  AV1_COMMON *const cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &pbi->dcb.xd;

#if CONFIG_BITSTREAM_DEBUG
  aom_bitstream_queue_set_frame_read(cm->current_frame.order_hint * 2 +
                                     cm->show_frame);
#endif
#if CONFIG_MISMATCH_DEBUG
  mismatch_move_frame_idx_r(1);
#endif  // CONFIG_MISMATCH_DEBUG

  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    cm->global_motion[i] = default_warp_params;
    cm->cur_frame->global_motion[i] = default_warp_params;
  }
  xd->global_motion = cm->global_motion;

  read_uncompressed_header(pbi, rb);

  if (trailing_bits_present) av1_check_trailing_bits(pbi, rb);

  if (!cm->tiles.single_tile_decoding &&
      (pbi->dec_tile_row >= 0 || pbi->dec_tile_col >= 0)) {
    pbi->dec_tile_row = -1;
    pbi->dec_tile_col = -1;
  }

  const uint32_t uncomp_hdr_size =
      (uint32_t)aom_rb_bytes_read(rb);  // Size of the uncompressed header
  YV12_BUFFER_CONFIG *new_fb = &cm->cur_frame->buf;
  xd->cur_buf = new_fb;
  if (av1_allow_intrabc(cm) && xd->tree_type != CHROMA_PART) {
    av1_setup_scale_factors_for_frame(
        &cm->sf_identity, xd->cur_buf->y_crop_width, xd->cur_buf->y_crop_height,
        xd->cur_buf->y_crop_width, xd->cur_buf->y_crop_height);
  }

  if (cm->show_existing_frame) {
    // showing a frame directly
    *p_data_end = data + uncomp_hdr_size;
    if (pbi->reset_decoder_state) {
      // Use the default frame context values.
      *cm->fc = *cm->default_frame_context;
      if (!cm->fc->initialized)
        aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                           "Uninitialized entropy context.");
    }
    return uncomp_hdr_size;
  }

  cm->mi_params.setup_mi(&cm->mi_params);

  if (cm->features.allow_ref_frame_mvs) av1_setup_motion_field(cm);
#if CONFIG_MVP_IMPROVEMENT
  else
    av1_setup_ref_frame_sides(cm);
#endif  // CONFIG_MVP_IMPROVEMENT

  if (cm->seq_params.enable_pef && cm->features.allow_pef) {
    init_pef_parameter(cm, 0, num_planes);
  }

  process_tip_mode(pbi);
  if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    *p_data_end = data + uncomp_hdr_size;
    return uncomp_hdr_size;
  }

  av1_setup_block_planes(xd, cm->seq_params.subsampling_x,
                         cm->seq_params.subsampling_y, num_planes);
  if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
    // use the default frame context values
    *cm->fc = *cm->default_frame_context;
  } else {
#if CONFIG_PRIMARY_REF_FRAME_OPT
    *cm->fc = get_primary_ref_frame_buf(cm, cm->features.primary_ref_frame)
                  ->frame_context;
#else
    *cm->fc = get_primary_ref_frame_buf(cm)->frame_context;
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
  }
  if (!cm->fc->initialized)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Uninitialized entropy context.");

  pbi->dcb.corrupted = 0;
  return uncomp_hdr_size;
}

// Once-per-frame initialization
static AOM_INLINE void setup_frame_info(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;

  if (cm->rst_info[0].frame_restoration_type != RESTORE_NONE ||
      cm->rst_info[1].frame_restoration_type != RESTORE_NONE ||
      cm->rst_info[2].frame_restoration_type != RESTORE_NONE
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
      || cm->rst_info[0].frame_cross_restoration_type != RESTORE_NONE ||
      cm->rst_info[1].frame_cross_restoration_type != RESTORE_NONE ||
      cm->rst_info[2].frame_cross_restoration_type != RESTORE_NONE
#endif
  ) {
    av1_alloc_restoration_buffers(cm);
  }
  const int buf_size = MC_TEMP_BUF_PELS << 1;
  if (pbi->td.mc_buf_size != buf_size) {
    av1_free_mc_tmp_buf(&pbi->td);
    allocate_mc_tmp_buf(cm, &pbi->td, buf_size);
  }
}

void av1_decode_tg_tiles_and_wrapup(AV1Decoder *pbi, const uint8_t *data,
                                    const uint8_t *data_end,
                                    const uint8_t **p_data_end, int start_tile,
                                    int end_tile, int initialize_flag) {
  AV1_COMMON *const cm = &pbi->common;
  CommonTileParams *const tiles = &cm->tiles;
  MACROBLOCKD *const xd = &pbi->dcb.xd;
  const int tile_count_tg = end_tile - start_tile + 1;

  if (initialize_flag) setup_frame_info(pbi);
  const int num_planes = av1_num_planes(cm);
#if CONFIG_LPF_MASK
  av1_loop_filter_frame_init(cm, 0, num_planes);
#endif
#if CONFIG_INSPECTION
  aom_realloc_frame_buffer(&cm->predicted_pixels, cm->width, cm->height,
                           cm->seq_params.subsampling_x,
                           cm->seq_params.subsampling_y,
                           AOM_DEC_BORDER_IN_PIXELS,
                           cm->features.byte_alignment, NULL, NULL, NULL, 0);
  aom_realloc_frame_buffer(&cm->prefiltered_pixels, cm->width, cm->height,
                           cm->seq_params.subsampling_x,
                           cm->seq_params.subsampling_y,
                           AOM_DEC_BORDER_IN_PIXELS,
                           cm->features.byte_alignment, NULL, NULL, NULL, 0);
#endif  // CONFIG_INSPECTION
  if (pbi->max_threads > 1 && !(tiles->large_scale && !pbi->ext_tile_debug) &&
      pbi->row_mt)
    *p_data_end =
        decode_tiles_row_mt(pbi, data, data_end, start_tile, end_tile);
  else if (pbi->max_threads > 1 && tile_count_tg > 1 &&
           !(tiles->large_scale && !pbi->ext_tile_debug))
    *p_data_end = decode_tiles_mt(pbi, data, data_end, start_tile, end_tile);
  else
    *p_data_end = decode_tiles(pbi, data, data_end, start_tile, end_tile);

  // If the bit stream is monochrome, set the U and V buffers to a constant.
  if (num_planes < 3) {
    set_planes_to_neutral_grey(&cm->seq_params, xd->cur_buf, 1);
  }

#if CONFIG_INSPECTION
  memcpy(cm->prefiltered_pixels.buffer_alloc, cm->cur_frame->buf.buffer_alloc,
         cm->prefiltered_pixels.frame_size);
#endif  // CONFIG_INSPECTION

  if (end_tile != tiles->rows * tiles->cols - 1) {
    return;
  }

  if (!is_global_intrabc_allowed(cm) && !tiles->single_tile_decoding) {
    if (cm->lf.filter_level[0] || cm->lf.filter_level[1]) {
      if (pbi->num_workers > 1) {
        av1_loop_filter_frame_mt(
            &cm->cur_frame->buf, cm, &pbi->dcb.xd, 0, num_planes, 0,
#if CONFIG_LPF_MASK
            1,
#endif
            pbi->tile_workers, pbi->num_workers, &pbi->lf_row_sync);
      } else {
        av1_loop_filter_frame(&cm->cur_frame->buf, cm, &pbi->dcb.xd,
#if CONFIG_LPF_MASK
                              1,
#endif
                              0, num_planes, 0);
      }
    }

#if CONFIG_CCSO
    const int use_ccso =
        !pbi->skip_loop_filter && !cm->features.coded_lossless &&
        (cm->ccso_info.ccso_enable[0] || cm->ccso_info.ccso_enable[1]
#if CONFIG_CCSO_EXT
         || cm->ccso_info.ccso_enable[2]
#endif
        );
    uint16_t *ext_rec_y;
    if (use_ccso) {
      av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, 0, 0, 0, num_planes,
                           NULL);
      const int ccso_stride_ext =
          xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
      ext_rec_y =
          aom_malloc(sizeof(*ext_rec_y) *
                     (xd->plane[0].dst.height + (CCSO_PADDING_SIZE << 1)) *
                     (xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1)));
      for (int pli = 0; pli < 1; pli++) {
        int pic_height = xd->plane[pli].dst.height;
        int pic_width = xd->plane[pli].dst.width;
        const int dst_stride = xd->plane[pli].dst.stride;
#if CONFIG_CCSO_EXT
        ext_rec_y += CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
#endif
        for (int r = 0; r < pic_height; ++r) {
          for (int c = 0; c < pic_width; ++c) {
#if CONFIG_CCSO_EXT
            ext_rec_y[c] = xd->plane[pli].dst.buf[c];
#else
              ext_rec_y[(r + CCSO_PADDING_SIZE) * ccso_stride_ext + c +
                        CCSO_PADDING_SIZE] =
                  xd->plane[pli].dst.buf[r * dst_stride + c];
#endif
          }
#if CONFIG_CCSO_EXT
          ext_rec_y += ccso_stride_ext;
          xd->plane[0].dst.buf += dst_stride;
        }
        ext_rec_y -= CCSO_PADDING_SIZE * ccso_stride_ext + CCSO_PADDING_SIZE;
        ext_rec_y -= pic_height * ccso_stride_ext;
        xd->plane[0].dst.buf -= pic_height * ccso_stride_ext;
#else
          }
#endif
      }
      extend_ccso_border(ext_rec_y, CCSO_PADDING_SIZE, xd);
    }
#endif

    const int do_loop_restoration =
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
        cm->rst_info[0].frame_cross_restoration_type != RESTORE_NONE ||
        cm->rst_info[1].frame_cross_restoration_type != RESTORE_NONE ||
        cm->rst_info[2].frame_cross_restoration_type != RESTORE_NONE ||
#endif
        cm->rst_info[0].frame_restoration_type != RESTORE_NONE ||
        cm->rst_info[1].frame_restoration_type != RESTORE_NONE ||
        cm->rst_info[2].frame_restoration_type != RESTORE_NONE;
    const int do_cdef = !pbi->skip_loop_filter &&
                        !cm->features.coded_lossless &&
#if CONFIG_FIX_CDEF_SYNTAX
                        cm->cdef_info.cdef_frame_enable;
#else
                        (cm->cdef_info.cdef_bits ||
                         cm->cdef_info.cdef_strengths[0] ||
                         cm->cdef_info.cdef_uv_strengths[0]);
#endif  // CONFIG_FIX_CDEF_SYNTAX
    const int do_superres = av1_superres_scaled(cm);

    const int optimized_loop_restoration =
#if CONFIG_CCSO
        !use_ccso &&
#endif
        !do_cdef && !do_superres;

    if (!optimized_loop_restoration) {
      if (do_loop_restoration)
        av1_loop_restoration_save_boundary_lines(&pbi->common.cur_frame->buf,
                                                 cm, 0);

      if (do_cdef) {
        av1_cdef_frame(&pbi->common.cur_frame->buf, cm, &pbi->dcb.xd);
      }

#if CONFIG_CCSO
      if (use_ccso) {
        ccso_frame(&cm->cur_frame->buf, cm, xd, ext_rec_y);
        aom_free(ext_rec_y);
      }
#endif

      superres_post_decode(pbi);

      if (do_loop_restoration) {
        av1_loop_restoration_save_boundary_lines(&pbi->common.cur_frame->buf,
                                                 cm, 1);
        if (pbi->num_workers > 1) {
          av1_loop_restoration_filter_frame_mt(
              (YV12_BUFFER_CONFIG *)xd->cur_buf, cm, optimized_loop_restoration,
              pbi->tile_workers, pbi->num_workers, &pbi->lr_row_sync,
              &pbi->lr_ctxt);
        } else {
          av1_loop_restoration_filter_frame((YV12_BUFFER_CONFIG *)xd->cur_buf,
                                            cm, optimized_loop_restoration,
                                            &pbi->lr_ctxt);
        }
      }
    } else {
#if CONFIG_CCSO
      if (use_ccso) {
        ccso_frame(&cm->cur_frame->buf, cm, xd, ext_rec_y);
        aom_free(ext_rec_y);
      }
#endif
      // In no cdef and no superres case. Provide an optimized version of
      // loop_restoration_filter.
      if (do_loop_restoration) {
        if (pbi->num_workers > 1) {
          av1_loop_restoration_filter_frame_mt(
              (YV12_BUFFER_CONFIG *)xd->cur_buf, cm, optimized_loop_restoration,
              pbi->tile_workers, pbi->num_workers, &pbi->lr_row_sync,
              &pbi->lr_ctxt);
        } else {
          av1_loop_restoration_filter_frame((YV12_BUFFER_CONFIG *)xd->cur_buf,
                                            cm, optimized_loop_restoration,
                                            &pbi->lr_ctxt);
        }
      }
    }
  }
#if CONFIG_LPF_MASK
  av1_zero_array(cm->lf.lfm, cm->lf.lfm_num);
#endif

  if (!pbi->dcb.corrupted) {
    if (cm->features.refresh_frame_context == REFRESH_FRAME_CONTEXT_BACKWARD) {
      assert(pbi->context_update_tile_id < pbi->allocated_tiles);
      *cm->fc = pbi->tile_data[pbi->context_update_tile_id].tctx;
      av1_reset_cdf_symbol_counters(cm->fc);
    }
  } else {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Decode failed. Frame data is corrupted.");
  }

#if CONFIG_INSPECTION
  if (pbi->inspect_cb != NULL) {
    (*pbi->inspect_cb)(pbi, pbi->inspect_ctx);
  }
#endif  // CONFIG_INSPECTION

  // Non frame parallel update frame context here.
  if (!tiles->large_scale) {
    cm->cur_frame->frame_context = *cm->fc;
  }
}
