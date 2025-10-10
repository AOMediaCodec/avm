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
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/bru.h"
#include "av1/common/enums.h"
#include "av1/common/filter.h"
#include "av1/common/scan.h"
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

#include "av1/common/gdf.h"

#include "av1/common/ccso.h"
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
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/decodemv.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodetxb.h"
#include "av1/decoder/detokenize.h"

#define AOM_MIN_THREADS_PER_TILE 1
#define AOM_MAX_THREADS_PER_TILE 2

// This is needed by ext_tile related unit tests.
#define EXT_TILE_DEBUG 1
#define MC_TEMP_BUF_PELS                           \
  (((MAX_SB_SIZE) * 2 + (AOM_INTERP_EXTEND) * 2) * \
   ((MAX_SB_SIZE) * 2 + (AOM_INTERP_EXTEND) * 2))

#if CONFIG_LR_FRAMEFILTERS_IN_HEADER
static void read_wienerns_framefilters_hdr(AV1_COMMON *cm, int plane,
                                           struct aom_read_bit_buffer *rb);
#else
static void read_wienerns_framefilters(AV1_COMMON *cm, MACROBLOCKD *xd,
                                       int plane, aom_reader *rb);
#endif  // CONFIG_LR_FRAMEFILTERS_IN_HEADER`

void copy_frame_filters_to_runits_if_needed(AV1_COMMON *cm) {
  const int num_planes = av1_num_planes(cm);
  for (int plane = 0; plane < num_planes; plane++) {
    if (!is_frame_filters_enabled(plane)) continue;
    RestorationInfo *rsi = &cm->rst_info[plane];
    if ((rsi->frame_restoration_type == RESTORE_WIENER_NONSEP ||
         rsi->frame_restoration_type == RESTORE_SWITCHABLE) &&
        rsi->frame_filters_on) {
      assert(rsi->frame_filters_initialized);
      for (int runit_idx = 0;
           runit_idx < rsi->horz_units_per_frame * rsi->vert_units_per_frame;
           ++runit_idx) {
        RestorationUnitInfo *rui = &rsi->unit_info[runit_idx];
        if (rui->restoration_type == RESTORE_WIENER_NONSEP) {
          copy_nsfilter_taps(&rui->wienerns_info, &rsi->frame_filters);
        }
      }
    }
  }
}

#if CONFIG_THROUGHPUT_ANALYSIS
int64_t tot_ctx_syms = { 0 };
int64_t tot_bypass_syms = { 0 };
int64_t max_ctx_syms = { 0 };
int64_t max_bypass_syms = { 0 };
int64_t max_bits = { 0 };
int64_t tot_bits = { 0 };
int64_t tot_frames = { 0 };
int64_t total_context_switch = { 0 };
int64_t total_total_hits = { 0 };
#endif  // CONFIG_THROUGHPUT_ANALYSIS

int av1_check_byte_alignment(AV1_COMMON *const cm,
                             struct aom_read_bit_buffer *const rb) {
  while (rb->bit_offset & 7) {
    if (aom_rb_read_bit(rb)) {
      cm->error.error_code = AOM_CODEC_CORRUPT_FRAME;
      return -1;
    }
  }
  return 0;
}

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

static AOM_INLINE void loop_restoration_read_sb_coeffs(AV1_COMMON *cm,
                                                       MACROBLOCKD *xd,
                                                       aom_reader *const r,
                                                       int plane,
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
  av1_inverse_transform_block(
      &dcb->xd, dqcoeff, plane, tx_type, tx_size, dst, stride, eob,
      replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                          cm->features.allow_screen_content_tools, &dcb->xd),
      reduced_tx_set);
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
  } else {
    // all tx blocks are skipped.
    av1_update_txk_skip_array(cm, dcb->xd.mi_row, dcb->xd.mi_col,
                              dcb->xd.tree_type, &mbmi->chroma_ref_info, plane,
                              row, col, tx_size);
  }
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
    int pixels_c = (cm->mi_params.mi_cols * MI_SIZE) >> pd->subsampling_x;
    int pixels_r = (cm->mi_params.mi_rows * MI_SIZE) >> pd->subsampling_y;
    mismatch_check_block_pre(pd->dst.buf, pd->dst.stride,
                             cm->current_frame.display_order_hint, pixels_c,
                             pixels_r, plane, pixel_c, pixel_r, blk_w, blk_h);
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
      const
#if CONFIG_REDUCED_TX_SET_EXT
          uint8_t
#else
          bool
#endif  // CONFIG_REDUCED_TX_SET_EXT
              reduced_tx_set_used = is_reduced_tx_set_used(cm, plane_type);
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
    int pixels_c = (cm->mi_params.mi_cols * MI_SIZE) >> pd->subsampling_x;
    int pixels_r = (cm->mi_params.mi_rows * MI_SIZE) >> pd->subsampling_y;
    mismatch_check_block_tx(dst, pd->dst.stride,
                            cm->current_frame.display_order_hint, pixels_c,
                            pixels_r, plane, pixel_c, pixel_r, blk_w, blk_h);
  }
#endif  // CONFIG_MISMATCH_DEBUG
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
  av1_inv_cross_chroma_tx_block(dqcoeff_c1, dqcoeff_c2, tx_size, cctx_type,
                                xd->bd);
}

static AOM_INLINE void inverse_transform_inter_block(
    const AV1_COMMON *const cm, DecoderCodingBlock *dcb, aom_reader *const r,
    const int plane, const int blk_row, const int blk_col,
    const TX_SIZE tx_size) {
  (void)r;
  MACROBLOCKD *const xd = &dcb->xd;
  PLANE_TYPE plane_type = get_plane_type(plane);
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const
#if CONFIG_REDUCED_TX_SET_EXT
      uint8_t
#else
      bool
#endif  // CONFIG_REDUCED_TX_SET_EXT
          reduced_tx_set_used = is_reduced_tx_set_used(cm, plane_type);
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
  int pixels_c = (cm->mi_params.mi_cols * MI_SIZE) >> pd->subsampling_x;
  int pixels_r = (cm->mi_params.mi_rows * MI_SIZE) >> pd->subsampling_y;
  mismatch_check_block_tx(dst, pd->dst.stride,
                          cm->current_frame.display_order_hint, pixels_c,
                          pixels_r, plane, pixel_c, pixel_r, blk_w, blk_h);
#endif  // CONFIG_MISMATCH_DEBUG
}

static AOM_INLINE void set_cb_buffer_offsets(DecoderCodingBlock *dcb,
                                             TX_SIZE tx_size, int plane) {
  dcb->cb_offset[plane] += tx_size_wide[tx_size] * tx_size_high[tx_size];
  dcb->txb_offset[plane] =
      dcb->cb_offset[plane] / (TX_SIZE_W_MIN * TX_SIZE_H_MIN);
}

static AOM_INLINE void decode_reconstruct_tx(
    AV1_COMMON *cm, ThreadData *const td, aom_reader *r,
    MB_MODE_INFO *const mbmi, int plane, BLOCK_SIZE plane_bsize, int blk_row,
    int blk_col, TX_SIZE tx_size, int *eob_total) {
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  if (plane == AOM_PLANE_U && is_cctx_allowed(cm, xd)) return;
  int txp_index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);
  // Scale to match transform block unit.
  const int max_blocks_high = max_block_high(xd, plane_bsize, plane);
  const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;

  if (mbmi->tx_partition_type[txp_index] == TX_PARTITION_NONE || plane) {
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
    get_tx_partition_sizes(mbmi->tx_partition_type[txp_index], tx_size,
                           &mbmi->txb_pos, mbmi->sub_txs, xd->error_info);

    for (int txb_idx = 0; txb_idx < mbmi->txb_pos.n_partitions; ++txb_idx) {
      mbmi->txb_idx = 0;
      const TX_SIZE sub_tx = mbmi->sub_txs[txb_idx];
      const int offsetr = blk_row + mbmi->txb_pos.row_offset[txb_idx];
      const int offsetc = blk_col + mbmi->txb_pos.col_offset[txb_idx];
      if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;

      td->read_coeffs_tx_inter_block_visit(cm, dcb, r, plane, offsetr, offsetc,
                                           sub_tx);
      td->inverse_tx_inter_block_visit(cm, dcb, r, plane, offsetr, offsetc,
                                       sub_tx);
      eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
      *eob_total += eob_data->eob;
      set_cb_buffer_offsets(dcb, sub_tx, plane);
    }
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

  set_mi_offsets(mi_params, xd, mi_row, mi_col, x_inside_boundary,
                 y_inside_boundary);
  xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART] = bsize;
  if (xd->tree_type != CHROMA_PART) {
    xd->mi[0]->mi_row_start = mi_row;
    xd->mi[0]->mi_col_start = mi_col;
  }
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
  set_mi_row_col(
#if CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
      cm,
#endif  // CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
      xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows, mi_params->mi_cols,
      chroma_ref_info);
  xd->mi[0]->chroma_mi_row_start = mi_row;
  xd->mi[0]->chroma_mi_col_start = mi_col;

  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes, chroma_ref_info);
}

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
                                     is_intrabc, &x_pad, &y_pad, NULL)) {
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
static void dec_calc_subpel_params_and_extend(
    const MV *const src_mv, InterPredParams *const inter_pred_params,
    MACROBLOCKD *const xd, int mi_x, int mi_y, int ref,
    int use_optflow_refinement, uint16_t **mc_buf, uint16_t **pre,
    SubpelParams *subpel_params, int *src_stride) {
  if (inter_pred_params->use_ref_padding) {
    common_calc_subpel_params_and_extend(
        src_mv, inter_pred_params, xd, mi_x, mi_y, ref, use_optflow_refinement,
        mc_buf, pre, subpel_params, src_stride);
    return;
  }

  PadBlock block;
  MV32 scaled_mv;
  int subpel_x_mv, subpel_y_mv;
  dec_calc_subpel_params(
      src_mv, inter_pred_params, xd, mi_x, mi_y, pre, subpel_params, src_stride,
      &block, use_optflow_refinement, &scaled_mv, &subpel_x_mv, &subpel_y_mv);
  extend_mc_border(inter_pred_params->scale_factors,
                   &inter_pred_params->ref_frame_buf, scaled_mv, block,
                   subpel_x_mv, subpel_y_mv,
                   inter_pred_params->mode == WARP_PRED,
                   inter_pred_params->is_intrabc, mc_buf[ref], pre, src_stride);
}

static void av1_dec_setup_tip_frame(AV1_COMMON *cm, MACROBLOCKD *xd,
                                    uint16_t **mc_buf,
                                    CONV_BUF_TYPE *tmp_conv_dst) {
  av1_setup_tip_motion_field(cm);
  av1_setup_tip_frame(cm, xd, mc_buf, tmp_conv_dst,
                      dec_calc_subpel_params_and_extend,
                      1 /* copy_refined_mvs */
  );
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
  }
  if (cm->seq_params.enable_lf_sub_pu && cm->features.allow_lf_sub_pu) {
    init_tip_lf_parameter(cm, 0, av1_num_planes(cm));
    loop_filter_tip_frame(cm, 0, av1_num_planes(cm));
  }
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
  // set region_type for each mbmi
  xd->mi[0]->region_type = parent->region_type;
  // set tree_type for each mbmi
  xd->mi[0]->tree_type = xd->tree_type;
#if CONFIG_LOCAL_INTRABC_ALIGN_RNG
  xd->mi[0]->sb_root_partition_info = parent->sb_root_partition_info;
#endif  // CONFIG_LOCAL_INTRABC_ALIGN_RNG
  xd->mi[0]->local_rest_type =
      1;  // set non zero default type, it is only matter 1 or 0 in SW
  xd->mi[0]->local_ccso_blk_flag =
      1;  // set non zero default type, it is only matter 1 or 0 in SW
  xd->mi[0]->local_gdf_mode =
      1;  // set non zero default type, it is only matter 1 or 0 in SW
#if CONFIG_CWG_F317
  if (!bru_is_sb_active(cm, mi_col, mi_row) ||
      cm->bridge_frame_info.is_bridge_frame) {
#else
  if (!bru_is_sb_active(cm, mi_col, mi_row)) {
#endif  // CONFIG_CWG_F317
    xd->mi[0]->sb_active_mode = xd->sbi->sb_active_mode;
    bru_set_default_inter_mb_mode_info(cm, xd, xd->mi[0], bsize);
    const int w = mi_size_wide[cm->seq_params.sb_size];
    const int h = mi_size_high[cm->seq_params.sb_size];
    const int x_inside_boundary = AOMMIN(w, cm->mi_params.mi_cols - mi_col);
    const int y_inside_boundary = AOMMIN(h, cm->mi_params.mi_rows - mi_row);
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      bru_zero_sb_mvs(cm, -1, mi_row, mi_col, x_inside_boundary,
                      y_inside_boundary);
    } else {
      bru_zero_sb_mvs(cm, cm->bru.update_ref_idx, mi_row, mi_col,
                      x_inside_boundary, y_inside_boundary);
    }
#else
    bru_zero_sb_mvs(cm, cm->bru.update_ref_idx, mi_row, mi_col,
                    x_inside_boundary, y_inside_boundary);
#endif  // CONFIG_CWG_F317
#if CONFIG_CWG_F317
    if (!cm->bru.frame_inactive_flag &&
        !cm->bridge_frame_info.is_bridge_frame) {
#else
    if (!cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
      if (xd->tree_type != CHROMA_PART) {
        read_gdf(cm, r, xd);
      }
      if (cm->seq_params.enable_ccso && xd->tree_type != CHROMA_PART) {
        read_ccso(cm, r, xd);
      }
    }
  } else
    av1_read_mode_info(pbi, dcb, r, x_mis, y_mis);

  if (xd->tree_type != LUMA_PART) {
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
  }
}

static void dec_build_inter_predictors(const AV1_COMMON *cm,
                                       DecoderCodingBlock *dcb, int plane,
                                       MB_MODE_INFO *mi, int bw, int bh,
                                       int mi_x, int mi_y,
                                       int build_for_refine_mv_only) {
  av1_build_inter_predictors(cm, &dcb->xd, plane, mi, NULL,
                             build_for_refine_mv_only, 1 /* build_for_decode */,
                             bw, bh, mi_x, mi_y, dcb->mc_buf,
                             dec_calc_subpel_params_and_extend);
}

static AOM_INLINE void dec_build_inter_predictor(const AV1_COMMON *cm,
                                                 DecoderCodingBlock *dcb,
                                                 int mi_row, int mi_col,
                                                 BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &dcb->xd;
  const int num_planes = av1_num_planes(cm);
  MB_MODE_INFO *mbmi = xd->mi[0];

  int need_subblock_mvs = xd->is_chroma_ref && mbmi->refinemv_flag &&
                          !is_intrabc_block(mbmi, xd->tree_type);
  assert(IMPLIES(need_subblock_mvs, !is_interintra_pred(mbmi)));
  if (need_subblock_mvs && default_refinemv_modes(mbmi))
    need_subblock_mvs &= (mbmi->comp_group_idx == 0 &&
                          mbmi->interinter_comp.type == COMPOUND_AVERAGE);
  if (need_subblock_mvs) {
    fill_subblock_refine_mv(xd->refinemv_subinfo, xd->plane[0].width,
                            xd->plane[0].height, mbmi->mv[0].as_mv,
                            mbmi->mv[1].as_mv);
  }

  for (int plane = 0; plane < num_planes; ++plane) {
    if (plane && !xd->is_chroma_ref) break;
    const int mi_x = mi_col * MI_SIZE;
    const int mi_y = mi_row * MI_SIZE;
    dec_build_inter_predictors(cm, dcb, plane, xd->mi[0],
                               xd->plane[plane].width, xd->plane[plane].height,
                               mi_x, mi_y, 0);

    int is_intra_inter_allowed = 1;
    if (mbmi->tree_type == SHARED_PART &&
        mbmi->region_type == MIXED_INTER_INTRA_REGION &&
        mbmi->chroma_ref_info.offset_started && plane > 0) {
      is_intra_inter_allowed = 0;
    }

    if (is_interintra_pred(xd->mi[0]) && is_intra_inter_allowed) {
      BUFFER_SET ctx = { { xd->plane[0].dst.buf, xd->plane[1].dst.buf,
                           xd->plane[2].dst.buf },
                         { xd->plane[0].dst.stride, xd->plane[1].dst.stride,
                           xd->plane[2].dst.stride } };
      av1_build_interintra_predictor(cm, xd, xd->plane[plane].dst.buf,
                                     xd->plane[plane].dst.stride, &ctx, plane,
                                     bsize);
    }
  }

  if (mbmi->morph_pred) {
    assert(av1_allow_intrabc(cm, xd, bsize));
    assert(av1_allow_intrabc_morph_pred(cm));
    assert(is_intrabc_block(mbmi, xd->tree_type));
    av1_build_morph_pred(cm, xd, bsize, mi_row, mi_col);
  }
}

static AOM_INLINE void cfl_store_inter_block(AV1_COMMON *const cm,
                                             MACROBLOCKD *const xd) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  if (store_cfl_required(cm, xd) && xd->tree_type == SHARED_PART) {
    cfl_store_block(xd, mbmi->sb_type[PLANE_TYPE_Y], mbmi->tx_size,
                    cm->seq_params.cfl_ds_filter_index);
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
  const int is_compound =
      has_second_ref(mbmi) || is_tip_ref_frame(mbmi->ref_frame[0]);
  for (int ref = 0; ref < 1 + is_compound; ++ref) {
    const MV_REFERENCE_FRAME frame = mbmi->ref_frame[ref];
    if (frame == INTRA_FRAME) {
      assert(is_intrabc_block(mbmi, xd->tree_type));
      assert(ref == 0);
    } else {
      const RefCntBuffer *ref_buf = is_tip_ref_frame(mbmi->ref_frame[0])
                                        ? cm->tip_ref.ref_frame_buffer[ref]
                                        : get_ref_frame_buf(cm, frame);
      const struct scale_factors *ref_scale_factors =
          get_ref_scale_factors_const(cm, frame);

      xd->block_ref_scale_factors[ref] = ref_scale_factors;
      av1_setup_pre_planes(xd, ref, &ref_buf->buf, mi_row, mi_col,
                           ref_scale_factors, num_planes,
                           &mbmi->chroma_ref_info);
    }
  }

  dec_build_inter_predictor(cm, dcb, mi_row, mi_col, bsize);

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
    int pixels_c = (cm->mi_params.mi_cols * MI_SIZE) >> pd->subsampling_x;
    int pixels_r = (cm->mi_params.mi_rows * MI_SIZE) >> pd->subsampling_y;
    mismatch_check_block_pre(
        pd->dst.buf, pd->dst.stride, cm->current_frame.display_order_hint,
        pixels_c, pixels_r, plane, pixel_c, pixel_r, pd->width, pd->height);
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

static AOM_INLINE void copy_frame_mvs_inter_block(AV1_COMMON *const cm,
                                                  DecoderCodingBlock *dcb,
                                                  BLOCK_SIZE bsize) {
  if (!frame_is_intra_only(cm) &&
      cm->seq_params.order_hint_info.enable_ref_frame_mvs) {
    MACROBLOCKD *const xd = &dcb->xd;
    MB_MODE_INFO *const mi = xd->mi[0];
    const int bw = mi_size_wide[bsize];
    const int bh = mi_size_high[bsize];
    const int x_inside_boundary =
        AOMMIN(bw, cm->mi_params.mi_cols - xd->mi_col);
    const int y_inside_boundary =
        AOMMIN(bh, cm->mi_params.mi_rows - xd->mi_row);
    if (enable_refined_mvs_in_tmvp(cm, xd, mi)) {
      av1_copy_frame_refined_mvs(cm, xd, mi, xd->mi_row, xd->mi_col,
                                 x_inside_boundary, y_inside_boundary);
    } else {
      av1_copy_frame_mvs(cm, xd, mi, xd->mi_row, xd->mi_col, x_inside_boundary,
                         y_inside_boundary);
    }
  }
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

void dec_bru_swap_stage(AV1_COMMON *cm, MACROBLOCKD *const xd) {
  if (cm->bru.enabled) {
    RefCntBuffer *tmp_buf = cm->cur_frame;
    if (bru_swap_common(cm)) {
      BufferPool *pool = cm->buffer_pool;
      lock_buffer_pool(pool);
      if (tmp_buf != NULL) {
        assert(tmp_buf->ref_count == 0);
        if (tmp_buf->ref_count == 0 && tmp_buf->raw_frame_buffer.data) {
          pool->release_fb_cb(pool->cb_priv, &tmp_buf->raw_frame_buffer);
          tmp_buf->raw_frame_buffer.data = NULL;
          tmp_buf->raw_frame_buffer.size = 0;
          tmp_buf->raw_frame_buffer.priv = NULL;
        }
      }
      unlock_buffer_pool(pool);
      xd->cur_buf = &cm->cur_frame->buf;
    } else {
      aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                         "Decoder BRU swap stage error");
    }
    // correctly extend borders after swap
    // todo: this can be simplified by only extend support SB border
    // aom_extend_frame_borders(&cm->cur_frame->buf, num_planes);
    // Note, do not touch any recon region except the border
    // first col of sb
    const int sb_cols =
        (cm->mi_params.mi_cols + cm->mib_size - 1) / cm->mib_size;
    const int sb_rows =
        (cm->mi_params.mi_rows + cm->mib_size - 1) / cm->mib_size;
    for (int sb_row = 0; sb_row < sb_rows; sb_row++) {
      const int sb_mi_row = sb_row << cm->mib_size_log2;
      const int sb_mi_col = 0;
      BruActiveMode active_mode =
          av1_get_sb_info(cm, sb_mi_row, sb_mi_col)->sb_active_mode;
      if (active_mode == BRU_SUPPORT_SB) {
        bru_extend_mc_border(cm, sb_mi_row, sb_mi_col, cm->sb_size,
                             &cm->cur_frame->buf);
      }
    }
    // first row of sb
    for (int sb_col = 0; sb_col < sb_cols; sb_col++) {
      const int sb_mi_row = 0;
      const int sb_mi_col = sb_col << cm->mib_size_log2;
      BruActiveMode active_mode =
          av1_get_sb_info(cm, sb_mi_row, sb_mi_col)->sb_active_mode;
      if (active_mode == BRU_SUPPORT_SB) {
        bru_extend_mc_border(cm, sb_mi_row, sb_mi_col, cm->sb_size,
                             &cm->cur_frame->buf);
      }
    }
    // last col of sb
    for (int sb_row = 0; sb_row < sb_rows; sb_row++) {
      const int sb_mi_row = sb_row << cm->mib_size_log2;
      const int sb_mi_col = (sb_cols - 1) << cm->mib_size_log2;
      BruActiveMode active_mode =
          av1_get_sb_info(cm, sb_mi_row, sb_mi_col)->sb_active_mode;
      if (active_mode == BRU_SUPPORT_SB) {
        bru_extend_mc_border(cm, sb_mi_row, sb_mi_col, cm->sb_size,
                             &cm->cur_frame->buf);
      }
    }
    // last row of sb
    for (int sb_col = 0; sb_col < sb_cols; sb_col++) {
      const int sb_mi_row = (sb_rows - 1) << cm->mib_size_log2;
      const int sb_mi_col = sb_col << cm->mib_size_log2;
      BruActiveMode active_mode =
          av1_get_sb_info(cm, sb_mi_row, sb_mi_col)->sb_active_mode;
      if (active_mode == BRU_SUPPORT_SB) {
        bru_extend_mc_border(cm, sb_mi_row, sb_mi_col, cm->sb_size,
                             &cm->cur_frame->buf);
      }
    }
  }
}

#if CONFIG_CWG_F317
static AOM_INLINE int bridge_frame_is_valid_inter(const AV1_COMMON *const cm,
                                                  MACROBLOCKD *const xd) {
  if (!cm->bridge_frame_info.is_bridge_frame) return 1;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int tip_ref_frame = is_tip_ref_frame(mbmi->ref_frame[0]);
  const int is_compound = has_second_ref(mbmi);
  if (tip_ref_frame || is_compound) return 0;
  if (mbmi->ref_frame[0] != cm->bridge_frame_info.bridge_frame_ref_idx_remapped)
    return 0;
  const int_mv mi_mv = mbmi->mv[0];
  // MV must be (0,0)
  if (mi_mv.as_int != 0) {
    return 0;
  }
  return 1;
}

#endif  // CONFIG_CWG_F317
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
#if CONFIG_TU64_TRAVERSED_ORDER
  const int mu128_wide = mi_size_wide[BLOCK_128X128];
  const int mu128_high = mi_size_high[BLOCK_128X128];
#endif  // CONFIG_TU64_TRAVERSED_ORDER

  if (!is_inter_block(mbmi, xd->tree_type)) {
    // When row_mt is used, this function can be called with
    // td->read_coeffs_tx_intra_block_visit == decode_block_void.
    // In that case do not reset since it will erase previously set
    // values.
    // intra cannot be used in non-active SBs
    if (!bru_is_sb_active(cm, xd->mi_col, xd->mi_row)) {
      aom_internal_error(
          &cm->error, AOM_CODEC_ERROR,
          "Invalid BRU activte: only active SB can be predicted by intra");
    }
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      aom_internal_error(
          &cm->error, AOM_CODEC_ERROR,
          "Invalid Bridge frame activate: can not be predicted by intra");
    }
#endif  // CONFIG_CWG_F317
    if (td->read_coeffs_tx_intra_block_visit != decode_block_void)
      av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, bsize, 0,
                              xd->tree_type, &mbmi->chroma_ref_info,
                              plane_start, plane_end);
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

#if CONFIG_TU64_TRAVERSED_ORDER
    // For 256x256 and 256x128 coding blocks, the coefficents/resdiuals are
    // divided into 64x* blocks, and each 64x64 block is coded in 128x128 unit.
    // For example, for a 256x256 coding block, the coding order of 64x64
    // residual blocks are following:
    // A B E F
    // C D G H
    // I J M N
    // K L O P

    // Loop through each 128x128 block within the current coding block
    for (int row128 = 0; row128 < max_blocks_high; row128 += mu128_high) {
      for (int col128 = 0; col128 < max_blocks_wide; col128 += mu128_wide) {
        // Loop through each 64x64 block within the current 128x128 block
        for (row = row128; row < AOMMIN(row128 + mu128_high, max_blocks_high);
             row += mu_blocks_high) {
          for (col = col128; col < AOMMIN(col128 + mu128_wide, max_blocks_wide);
               col += mu_blocks_wide) {
#else
    for (row = 0; row < max_blocks_high; row += mu_blocks_high) {
      for (col = 0; col < max_blocks_wide; col += mu_blocks_wide) {
#endif  // CONFIG_TU64_TRAVERSED_ORDER
            for (int plane = plane_start; plane < plane_end; ++plane) {
              if (plane == AOM_PLANE_Y && !xd->lossless[mbmi->segment_id]) {
                const struct macroblockd_plane *const pd = &xd->plane[plane];
                const int ss_x = pd->subsampling_x;
                const int ss_y = pd->subsampling_y;
                const BLOCK_SIZE plane_bsize =
                    get_mb_plane_block_size(xd, mbmi, plane, ss_x, ss_y);
                const int plane_unit_height =
                    get_plane_tx_unit_height(xd, plane_bsize, plane, row, ss_y);
                const int plane_unit_width =
                    get_plane_tx_unit_width(xd, plane_bsize, plane, col, ss_x);

                const TX_SIZE max_tx_size = max_txsize_rect_lookup[plane_bsize];
                get_tx_partition_sizes(mbmi->tx_partition_type[0], max_tx_size,
                                       &mbmi->txb_pos, mbmi->sub_txs,
                                       xd->error_info);

                for (int txb_idx = 0; txb_idx < mbmi->txb_pos.n_partitions;
                     ++txb_idx) {
                  const TX_SIZE tx_size = mbmi->sub_txs[txb_idx];
                  mbmi->txb_idx = txb_idx;
                  int blk_row = row + mbmi->txb_pos.row_offset[txb_idx];
                  int blk_col = col + mbmi->txb_pos.col_offset[txb_idx];

                  if (blk_row >= plane_unit_height ||
                      blk_col >= plane_unit_width)
                    continue;

                  td->read_coeffs_tx_intra_block_visit(
                      cm, dcb, r, plane, blk_row, blk_col, tx_size);
                  td->predict_and_recon_intra_block_visit(
                      cm, dcb, r, plane, blk_row, blk_col, tx_size);
                  set_cb_buffer_offsets(dcb, tx_size, plane);
                }
                // finish luma coding
              } else {
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

                const bool lossless = xd->lossless[mbmi->segment_id];
                const bool need_parsing =
                    !skip_parsing_recon(row, col, ss_x, ss_y, lossless);
                const bool need_reconstrution =
                    mbmi->uv_mode != UV_CFL_PRED || lossless
                        ? need_parsing
                        : !((row + mu_blocks_high < max_blocks_high) ||
                            (col + mu_blocks_wide < max_blocks_wide));

                for (int blk_row = row >> ss_y; blk_row < plane_unit_height;
                     blk_row += stepr) {
                  for (int blk_col = col >> ss_x; blk_col < plane_unit_width;
                       blk_col += stepc) {
                    if (plane == AOM_PLANE_V && is_cctx_allowed(cm, xd)) {
                      if (need_parsing) {
                        td->read_coeffs_tx_intra_block_visit(
                            cm, dcb, r, AOM_PLANE_U, blk_row, blk_col, tx_size);
                        td->read_coeffs_tx_intra_block_visit(
                            cm, dcb, r, AOM_PLANE_V, blk_row, blk_col, tx_size);
                        td->inverse_cctx_block_visit(cm, dcb, r, -1, blk_row,
                                                     blk_col, tx_size);
                      }

                      if (need_reconstrution) {
                        td->predict_and_recon_intra_block_visit(
                            cm, dcb, r, AOM_PLANE_U, blk_row & 0xf0,
                            blk_col & 0xf0, tx_size);
                        td->predict_and_recon_intra_block_visit(
                            cm, dcb, r, AOM_PLANE_V, blk_row & 0xf0,
                            blk_col & 0xf0, tx_size);
                      }

                      if (need_reconstrution) {
                        set_cb_buffer_offsets(dcb, tx_size, AOM_PLANE_U);
                        set_cb_buffer_offsets(dcb, tx_size, AOM_PLANE_V);
                      }
                    } else {
                      assert(plane == AOM_PLANE_Y || !is_cctx_allowed(cm, xd));
                      if (need_parsing)
                        td->read_coeffs_tx_intra_block_visit(
                            cm, dcb, r, plane, blk_row, blk_col, tx_size);
                      if (need_reconstrution) {
                        td->predict_and_recon_intra_block_visit(
                            cm, dcb, r, plane,
                            blk_row & (lossless ? 0xff : 0xf0),
                            blk_col & (lossless ? 0xff : 0xf0), tx_size);
                      }
                      if (need_reconstrution)
                        set_cb_buffer_offsets(dcb, tx_size, plane);
                    }
                  }
                }
                // finish coding of the chroma components
              }
            }
          }
        }
#if CONFIG_TU64_TRAVERSED_ORDER
      }
    }
#endif  // CONFIG_TU64_TRAVERSED_ORDER
  } else {
    // When row_mt is used, this function can be called with
    // td->read_coeffs_tx_inter_block_visit == decode_block_void.
    // In that case do not reset since it will erase previously set
    // values.
    // check BRU inter prediction motion vector
    if (!bru_is_valid_inter(cm, xd)) {
      aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                         "Invalid BRU inter prediction");
    }
#if CONFIG_CWG_F317
    if (!bridge_frame_is_valid_inter(cm, xd)) {
      aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                         "Invalid Bridge frame inter prediction");
    }
#endif  // CONFIG_CWG_F317
    if (td->read_coeffs_tx_inter_block_visit != decode_block_void)
      av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, bsize, 0,
                              xd->tree_type, &mbmi->chroma_ref_info,
                              plane_start, plane_end);
    td->predict_inter_block_visit(cm, dcb, bsize);
    // Reconstruction
    if (!mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
      int eobtotal = 0;
      if (!bru_is_sb_active(cm, xd->mi_col, xd->mi_row)) {
        aom_internal_error(
            &cm->error, AOM_CODEC_ERROR,
            "Invalid BRU skip_txfm: only active SB has transform");
      }
#if CONFIG_CWG_F317
      if (cm->bridge_frame_info.is_bridge_frame) {
        aom_internal_error(
            &cm->error, AOM_CODEC_ERROR,
            "Invalid Bridge frame skip_txfm: can not have transform");
      }
#endif  // CONFIG_CWG_F317
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

#if CONFIG_TU64_TRAVERSED_ORDER
      // Loop through each 128x128 block within the current coding block
      for (int row128 = 0; row128 < max_blocks_high; row128 += mu128_high) {
        for (int col128 = 0; col128 < max_blocks_wide; col128 += mu128_wide) {
          // Loop through each 64x64 block within the current 128x128 block
          for (row = row128; row < AOMMIN(row128 + mu128_high, max_blocks_high);
               row += mu_blocks_high) {
            for (col = col128;
                 col < AOMMIN(col128 + mu128_wide, max_blocks_wide);
                 col += mu_blocks_wide) {
#else
      for (row = 0; row < max_blocks_high; row += mu_blocks_high) {
        for (col = 0; col < max_blocks_wide; col += mu_blocks_wide) {
#endif  // CONFIG_TU64_TRAVERSED_ORDER
              for (int plane = plane_start; plane < plane_end; ++plane) {
                if (plane && !xd->is_chroma_ref) break;
                const struct macroblockd_plane *const pd = &xd->plane[plane];
                const int ss_x = pd->subsampling_x;
                const int ss_y = pd->subsampling_y;
                const BLOCK_SIZE plane_bsize =
                    get_mb_plane_block_size(xd, mbmi, plane, ss_x, ss_y);
                const TX_SIZE max_tx_size =
                    get_vartx_max_txsize(xd, plane_bsize, plane);
                const int bh_var_tx = tx_size_high_unit[max_tx_size];
                const int bw_var_tx = tx_size_wide_unit[max_tx_size];
                const int plane_unit_height =
                    get_plane_tx_unit_height(xd, plane_bsize, plane, row, ss_y);
                const int plane_unit_width =
                    get_plane_tx_unit_width(xd, plane_bsize, plane, col, ss_x);

                // When chroma TU is assocated with multiple luma TUs, the
                // chroma TU is parsed/reconstructed fewer times than
                // associated luama TUs. For example, in YUV420 format, one
                // 64x64 chroma TU is associated with four 64x64 luma TUs,
                // the parsing and reconstruction of the first 64x64 chroma
                // TU is done once for the first 64x64 luma TU and skipped
                // for the remaining three 64x64 TUs.
                const bool lossless = xd->lossless[mbmi->segment_id];
                if (skip_parsing_recon(row, col, ss_x, ss_y, lossless)) {
                  continue;
                }
                for (int blk_row = row >> ss_y; blk_row < plane_unit_height;
                     blk_row += bh_var_tx) {
                  for (int blk_col = col >> ss_x; blk_col < plane_unit_width;
                       blk_col += bw_var_tx) {
                    decode_reconstruct_tx(cm, td, r, mbmi, plane, plane_bsize,
                                          blk_row, blk_col, max_tx_size,
                                          &eobtotal);
                  }
                }
              }
            }
          }
#if CONFIG_TU64_TRAVERSED_ORDER
        }
      }
#endif  // CONFIG_TU64_TRAVERSED_ORDER
    } else if (is_cctx_enabled(cm, xd) && xd->is_chroma_ref &&
               xd->tree_type != LUMA_PART) {
      av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, bsize, 1,
                              xd->tree_type, &mbmi->chroma_ref_info,
                              plane_start, plane_end);
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
#if CONFIG_TU64_TRAVERSED_ORDER
      // Loop through each 128x128 block within the current coding block
      for (int row128 = 0; row128 < max_blocks_high; row128 += mu128_high) {
        for (int col128 = 0; col128 < max_blocks_wide; col128 += mu128_wide) {
          // Loop through each 64x64 block within the current 128x128 block
          for (int row = row128;
               row < AOMMIN(row128 + mu128_high, max_blocks_high);
               row += mu_blocks_high) {
            for (int col = col128;
                 col < AOMMIN(col128 + mu128_wide, max_blocks_wide);
                 col += mu_blocks_wide) {
#else

      for (int row = 0; row < max_blocks_high; row += mu_blocks_high) {
        for (int col = 0; col < max_blocks_wide; col += mu_blocks_wide) {
#endif  // CONFIG_TU64_TRAVERSED_ORDER
              int row_offset, col_offset;
              get_chroma_mi_offsets(xd, &row_offset, &col_offset);
              update_cctx_array(xd, 0, 0, row_offset, col_offset, max_tx_size,
                                CCTX_NONE);
            }
          }
#if CONFIG_TU64_TRAVERSED_ORDER
        }
      }
#endif  // CONFIG_TU64_TRAVERSED_ORDER
    } else {
      av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, bsize, 1,
                              xd->tree_type, &mbmi->chroma_ref_info,
                              plane_start, plane_end);
    }
  }

  td->copy_frame_mvs_block_visit(cm, dcb, bsize);

  av1_visit_palette(pbi, xd, r, set_color_index_map_offset);
  av1_mark_block_as_coded(xd, bsize, cm->sb_size);
}

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
  const int allow_horz = allow_tx_horz_split(bsize, max_tx_size);
  const int allow_vert = allow_tx_vert_split(bsize, max_tx_size);
  TX_PARTITION_TYPE partition = 0;
  const int is_fsc = (xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
                      plane_type == PLANE_TYPE_Y);
  const int bsize_group = size_to_tx_part_group_lookup[bsize];
  const int txsize_group_h_and_v = get_vert_and_horz_group(bsize);
  const int txsize_group_h_or_v = get_vert_or_horz_group(bsize);
  assert(!(txsize_group_h_and_v == BLOCK_INVALID &&
           txsize_group_h_or_v == BLOCK_INVALID));
  int do_partition = 0;
  if (allow_horz || allow_vert) {
    aom_cdf_prob *do_partition_cdf =
        ec_ctx->txfm_do_partition_cdf[is_fsc][is_inter][bsize_group];
    do_partition =
        aom_read_symbol(r, do_partition_cdf, 2, ACCT_INFO("do_partition"));
  }

  if (do_partition) {
    if (allow_horz && allow_vert) {
      // Read 4way tree type
      assert(txsize_group_h_or_v > 0);
      aom_cdf_prob *partition_type_cdf =
          ec_ctx->txfm_4way_partition_type_cdf[is_fsc][is_inter]
                                              [txsize_group_h_and_v];

      const TX_PARTITION_TYPE partition_type =
          aom_read_symbol(r, partition_type_cdf, TX_PARTITION_TYPE_NUM,
                          ACCT_INFO("partition_type"));
      partition = partition_type + 1;
    } else if (txsize_group_h_or_v) {
      aom_cdf_prob *partition_type_cdf =
          ec_ctx->txfm_2or3_way_partition_type_cdf[is_fsc][is_inter]
                                                  [txsize_group_h_or_v - 1];
      const TX_PARTITION_TYPE partition_type =
          xd->reduced_tx_part_set
              ? 0
              : aom_read_symbol(r, partition_type_cdf, 2,
                                ACCT_INFO("partition_type"));
      if (allow_horz) {
        switch (partition_type) {
          case 0: partition = TX_PARTITION_HORZ; break;
          case 1: partition = TX_PARTITION_HORZ4; break;
          default: assert(0); break;
        }
      } else {
        switch (partition_type) {
          case 0: partition = TX_PARTITION_VERT; break;
          case 1: partition = TX_PARTITION_VERT4; break;
          default: assert(0); break;
        }
      }
    } else {
      partition = allow_horz ? TX_PARTITION_HORZ : TX_PARTITION_VERT;
    }
  } else {
    partition = TX_PARTITION_NONE;
  }

  TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
  int num_txfm_blocks = get_tx_partition_sizes(
      partition, max_tx_size, &mbmi->txb_pos, sub_txs, xd->error_info);
  mbmi->tx_size = sub_txs[num_txfm_blocks - 1];
  int index = is_inter ? av1_get_txb_size_index(bsize, blk_row, blk_col) : 0;
  mbmi->tx_partition_type[index] = partition;
  return sub_txs[num_txfm_blocks - 1];
}

static TX_SIZE read_tx_size(MACROBLOCKD *xd, TX_MODE tx_mode, int is_inter,
                            int allow_select_inter, aom_reader *r) {
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART];
  if (xd->lossless[xd->mi[0]->segment_id]) {
    const bool is_fsc = xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART];
    if (bsize == BLOCK_4X4 || (!is_inter && !is_fsc) || !allow_select_inter)
      return TX_4X4;
    else {
      const int bsize_group = size_group_lookup[bsize];
      const int is_tx_size_large = aom_read_symbol(
          r, xd->tile_ctx->lossless_tx_size_cdf[bsize_group][is_inter], 2,
          ACCT_INFO("lossless_tx_size"));
      if (is_tx_size_large) {
        return lossless_max_txsize_lookup[bsize];
      }
      return (TX_SIZE)is_tx_size_large;
    }
  }

  if (block_signals_txsize(bsize)) {
    if ((!is_inter || allow_select_inter) && tx_mode == TX_MODE_SELECT) {
      MB_MODE_INFO *mbmi = xd->mi[0];
      const TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
      return read_tx_partition(xd, mbmi, max_tx_size, 0, 0, r);
    } else {
      return tx_size_from_tx_mode(bsize, tx_mode);
    }
  } else {
    assert(IMPLIES(tx_mode == ONLY_4X4, bsize == BLOCK_4X4));
    return max_txsize_rect_lookup[bsize];
  }
}
static BruActiveMode read_bru_mode(AV1_COMMON *cm, const MACROBLOCKD *xd,
                                   aom_reader *r) {
  if (!cm->bru.enabled) return 0;
  BruActiveMode sb_active_mode = BRU_ACTIVE_SB;
  if (is_sb_start_mi(cm, xd->mi_col, xd->mi_row)) {
    if (xd->tile.tile_active_mode == 0) {
      sb_active_mode = BRU_INACTIVE_SB;
    } else {
      FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
      // 0 inactive, 1 support, 2 active
      sb_active_mode = (BruActiveMode)aom_read_symbol(
          r, ec_ctx->bru_mode_cdf, 3, ACCT_INFO("sb_active_mode"));
    }
    xd->sbi->sb_active_mode = sb_active_mode;
  } else {
    const SB_INFO *sbi = av1_get_sb_info(cm, xd->mi_row, xd->mi_col);
    sb_active_mode = sbi->sb_active_mode;
  }
  return sb_active_mode;
}
static AOM_INLINE void parse_decode_block(AV1Decoder *const pbi,
                                          ThreadData *const td, int mi_row,
                                          int mi_col, aom_reader *r,
                                          PARTITION_TYPE partition,
                                          BLOCK_SIZE bsize,
                                          PARTITION_TREE *parent, int index) {
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  const int max_aspect_ratio =
      1 << (pbi->common.seq_params.max_pb_aspect_ratio_log2_m1 + 1);
  const int blk_w = block_size_wide[bsize];
  const int blk_h = block_size_high[bsize];
  if (blk_w > blk_h * max_aspect_ratio || blk_h > blk_w * max_aspect_ratio) {
    aom_internal_error(
        xd->error_info, AOM_CODEC_CORRUPT_FRAME,
        "Block size %dx%d violates aspect ratio constraint of %d", blk_w, blk_h,
        max_aspect_ratio);
  }
  decode_mbmi_block(pbi, dcb, mi_row, mi_col, r, partition, bsize, parent,
                    index);

  av1_visit_palette(pbi, xd, r, av1_decode_palette_tokens);

  AV1_COMMON *cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);
  MB_MODE_INFO *mbmi = xd->mi[0];
  int inter_block_tx = is_inter_block(mbmi, xd->tree_type) ||
                       is_intrabc_block(mbmi, xd->tree_type);
  xd->reduced_tx_part_set = cm->seq_params.reduced_tx_part_set;
  if (xd->tree_type != CHROMA_PART) {
    memset(mbmi->tx_partition_type, TX_PARTITION_NONE,
           sizeof(mbmi->tx_partition_type));
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
          read_tx_partition(xd, mbmi, max_tx_size, idy, idx, r);
    } else {
      mbmi->tx_size =
          read_tx_size(xd, cm->features.tx_mode, inter_block_tx,
                       !mbmi->skip_txfm[xd->tree_type == CHROMA_PART], r);
    }
  }

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
        xd->plane[j].seg_dequant_QTX[i][1] =
            av1_ac_quant_QTX(current_qindex, ac_delta_q,
                             j == 0 ? 0 : cm->seq_params.base_uv_ac_delta_q,
                             cm->seq_params.bit_depth);
      }
    }
  }
  assert(bsize == mbmi->sb_type[av1_get_sdp_idx(xd->tree_type)]);
  if (mbmi->skip_txfm[xd->tree_type == CHROMA_PART])
    av1_reset_entropy_context(xd, bsize, num_planes);
  // For regular decoder, always do recon
  // For optimized decoder, only do reocn when support SB
  if (!pbi->bru_opt_mode ||
      (pbi->bru_opt_mode && bru_is_sb_active(cm, mi_col, mi_row)))
    decode_token_recon_block(pbi, td, r, partition, bsize);

  // Note: the copying here must match corresponding encoder-side copying in
  // av1_update_state().
  // TODO(any): Refactor.
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
          xd->mi[y * idx + x]->cfl_alpha_signs = xd->mi[0]->cfl_alpha_signs;
          xd->mi[y * idx + x]->cfl_alpha_idx = xd->mi[0]->cfl_alpha_idx;
          xd->mi[y * idx + x]->partition = xd->mi[0]->partition;
          xd->mi[y * idx + x]->chroma_mi_row_start =
              xd->mi[0]->chroma_mi_row_start;
          xd->mi[y * idx + x]->chroma_mi_col_start =
              xd->mi[0]->chroma_mi_col_start;
          if (av1_allow_palette(PLANE_TYPE_UV,
                                cm->features.allow_screen_content_tools,
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

static AOM_INLINE void set_offsets_for_pred_and_recon(AV1Decoder *const pbi,
                                                      ThreadData *const td,
                                                      int mi_row, int mi_col,
                                                      BLOCK_SIZE bsize) {
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

  // It is assumed that CHROMA_REF_INFO is already set (during parsing stage).
  CHROMA_REF_INFO *chroma_ref_info = &xd->mi[0]->chroma_ref_info;
  set_plane_n4(xd, bw, bh, num_planes, chroma_ref_info);

  // Distance of Mb to the various image edges. These are specified to 8th pel
  // as they are always compared to values that are in 1/8th pel units
  set_mi_row_col(
#if CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
      cm,
#endif  // CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
      xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows, mi_params->mi_cols,
      chroma_ref_info);

  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                       num_planes, chroma_ref_info);
}

static AOM_INLINE void decode_block(AV1Decoder *const pbi, ThreadData *const td,
                                    int mi_row, int mi_col, aom_reader *r,
                                    PARTITION_TYPE partition, BLOCK_SIZE bsize,
                                    PARTITION_TREE *parent, int index) {
  (void)partition;
  (void)parent;
  (void)index;
  const int max_aspect_ratio =
      1 << (pbi->common.seq_params.max_pb_aspect_ratio_log2_m1 + 1);
  const int blk_w = block_size_wide[bsize];
  const int blk_h = block_size_high[bsize];
  if (blk_w > blk_h * max_aspect_ratio || blk_h > blk_w * max_aspect_ratio) {
    aom_internal_error(
        td->dcb.xd.error_info, AOM_CODEC_CORRUPT_FRAME,
        "Block size %dx%d violates aspect ratio constraint of %d", blk_w, blk_h,
        max_aspect_ratio);
  }
  set_offsets_for_pred_and_recon(pbi, td, mi_row, mi_col, bsize);
  decode_token_recon_block(pbi, td, r, partition, bsize);
}

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

static PARTITION_TYPE read_partition(const AV1_COMMON *const cm,
                                     MACROBLOCKD *xd, int mi_row, int mi_col,
                                     aom_reader *r, int has_rows, int has_cols,
                                     const PARTITION_TREE *ptree,
                                     const PARTITION_TREE *ptree_luma,
                                     BLOCK_SIZE bsize) {
#if !CONFIG_NEW_PART_CTX
  const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize, 1);
  const int rect_type_ctx =
      partition_plane_context(xd, mi_row, mi_col, bsize, 0);
#endif  // !CONFIG_NEW_PART_CTX
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  (void)has_rows;
  (void)has_cols;
  const int plane = xd->tree_type == CHROMA_PART;
  const int ssx = cm->seq_params.subsampling_x;
  const int ssy = cm->seq_params.subsampling_y;
  PARTITION_TYPE derived_partition = av1_get_normative_forced_partition_type(
      &cm->mi_params, xd->tree_type, ssx, ssy, mi_row, mi_col, bsize,
      ptree_luma);

  bool partition_allowed[ALL_PARTITION_TYPES];
  init_allowed_partitions_for_signaling(
      partition_allowed, cm, xd->tree_type,
      (ptree->parent ? ptree->parent->region_type : INTRA_REGION), mi_row,
      mi_col, ssx, ssy, bsize, &ptree->chroma_ref_info);
  if (derived_partition != PARTITION_INVALID &&
      partition_allowed[derived_partition]) {
    return derived_partition;
  }

  derived_partition = only_allowed_partition(partition_allowed);
  if (derived_partition != PARTITION_INVALID) {
    return derived_partition;
  }

  bool do_split;
  bool implied_do_split;
  // allow do whatever implied first, then if not possible
  // use BRU to set do split to false
  if (is_do_split_implied(partition_allowed, &implied_do_split)) {
    // BRU inactive won't go futher implied partition
#if CONFIG_CWG_F317
    if (!bru_is_sb_active(cm, mi_col, mi_row) ||
        cm->bridge_frame_info.is_bridge_frame) {
#else
    if (!bru_is_sb_active(cm, mi_col, mi_row)) {
#endif  // CONFIG_CWG_F317
      do_split = false;
    } else
      do_split = implied_do_split;
  } else {
    // if not derived partition, based on inactive/support set do_split to false
#if CONFIG_CWG_F317
    if (!bru_is_sb_active(cm, mi_col, mi_row) ||
        cm->bridge_frame_info.is_bridge_frame) {
#else
    if (!bru_is_sb_active(cm, mi_col, mi_row)) {
#endif  // CONFIG_CWG_F317
      do_split = false;
    } else {
#if CONFIG_NEW_PART_CTX
      const int ctx =
          partition_plane_context(xd, mi_row, mi_col, bsize, 0, SPLIT_CTX_MODE);
      do_split = aom_read_symbol(r, ec_ctx->do_split_cdf[plane][ctx], 2,
                                 ACCT_INFO("do_split"));
#else
      do_split = aom_read_symbol(r, ec_ctx->do_split_cdf[plane][ctx], 2,
                                 ACCT_INFO("do_split"));
#endif  // CONFIG_NEW_PART_CTX
    }
  }
  if (!do_split) {
    return PARTITION_NONE;
  }

  if (partition_allowed[PARTITION_SPLIT]) {
    const int square_split_ctx =
        square_split_context(xd, mi_row, mi_col, bsize);
    const bool do_square_split =
        aom_read_symbol(r, ec_ctx->do_square_split_cdf[plane][square_split_ctx],
                        2, ACCT_INFO("do_square_split"));
    if (do_square_split) {
      return PARTITION_SPLIT;
    }
  }

  RECT_PART_TYPE rect_type = rect_type_implied_by_bsize(bsize, xd->tree_type);
  if (rect_type == RECT_INVALID) {
    rect_type = only_allowed_rect_type(partition_allowed);
  }
  if (rect_type == RECT_INVALID) {
#if CONFIG_NEW_PART_CTX
    const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize, 0,
                                            RECT_TYPE_CTX_MODE);
    rect_type = aom_read_symbol(r, ec_ctx->rect_type_cdf[plane][ctx],
                                NUM_RECT_PARTS, ACCT_INFO("rect_type"));
#else
    rect_type = aom_read_symbol(r, ec_ctx->rect_type_cdf[plane][rect_type_ctx],
                                NUM_RECT_PARTS, ACCT_INFO("rect_type"));
#endif  // CONFIG_NEW_PART_CTX
  }

  bool do_ext_partition = false;
  bool do_uneven_4way_partition = false;
  UNEVEN_4WAY_PART_TYPE uneven_4way_partition_type = UNEVEN_4A;
  bool implied_do_ext;
  if (is_do_ext_partition_implied(partition_allowed, rect_type,
                                  &implied_do_ext)) {
    do_ext_partition = implied_do_ext;
  } else {
#if CONFIG_NEW_PART_CTX
    const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize,
                                            rect_type, EXT_PART_CTX_MODE);
    do_ext_partition =
        aom_read_symbol(r, ec_ctx->do_ext_partition_cdf[plane][0][ctx], 2,
                        ACCT_INFO("do_ext_partition"));
#else
    do_ext_partition =
        aom_read_symbol(r, ec_ctx->do_ext_partition_cdf[plane][rect_type][ctx],
                        2, ACCT_INFO("do_ext_partition"));
#endif  // CONFIG_NEW_PART_CTX
  }
  if (do_ext_partition) {
    bool implied_do_uneven_4way;
    if (is_do_uneven_4way_partition_implied(partition_allowed, rect_type,
                                            &implied_do_uneven_4way)) {
      do_uneven_4way_partition = implied_do_uneven_4way;
    } else {
#if CONFIG_NEW_PART_CTX
      const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize,
                                              rect_type, FOUR_WAY_CTX_MODE);
      do_uneven_4way_partition = aom_read_symbol(
          r, ec_ctx->do_uneven_4way_partition_cdf[plane][0][ctx], 2,
          ACCT_INFO("do_uneven_4way_partition"));
#else
      do_uneven_4way_partition = aom_read_symbol(
          r, ec_ctx->do_uneven_4way_partition_cdf[plane][rect_type][ctx], 2,
          ACCT_INFO("do_uneven_4way_partition"));
#endif  // CONFIG_NEW_PART_CTX
    }
    if (do_uneven_4way_partition) {
#if CONFIG_NEW_PART_CTX
      uneven_4way_partition_type =
          aom_read_bit(r, ACCT_INFO("uneven_4way_partition_type"));
#else
      uneven_4way_partition_type = aom_read_symbol(
          r, ec_ctx->uneven_4way_partition_type_cdf[plane][rect_type][ctx],
          NUM_UNEVEN_4WAY_PARTS, ACCT_INFO("uneven_4way_partition_type"));
#endif  // CONFIG_NEW_PART_CTX
    }
  }
  return rect_part_table[do_ext_partition][do_uneven_4way_partition]
                        [uneven_4way_partition_type][rect_type];
}

// Set the superblock level parameters
static void set_sb_mv_precision(SB_INFO *sbi, AV1Decoder *const pbi) {
  AV1_COMMON *const cm = &pbi->common;
  sbi->sb_mv_precision = cm->features.fr_mv_precision;
}

// TODO(slavarnway): eliminate bsize and subsize in future commits
static AOM_INLINE void decode_partition(
    AV1Decoder *const pbi, ThreadData *const td, int mi_row, int mi_col,
    aom_reader *reader, BLOCK_SIZE bsize, SB_INFO *sbi, PARTITION_TREE *ptree,
    PARTITION_TREE *ptree_luma, int parse_decode_flag) {
  assert(bsize < BLOCK_SIZES_ALL);
  AV1_COMMON *const cm = &pbi->common;
  DecoderCodingBlock *const dcb = &td->dcb;
  MACROBLOCKD *const xd = &dcb->xd;
  const int ss_x = xd->plane[1].subsampling_x;
  const int ss_y = xd->plane[1].subsampling_y;
  // Half block width/height.
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
  // One-eighth block width/height.
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
  PARTITION_TYPE partition;
  const int has_rows = (mi_row + hbs_h) < cm->mi_params.mi_rows;
  const int has_cols = (mi_col + hbs_w) < cm->mi_params.mi_cols;

  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols) {
    av1_mark_block_as_pseudo_coded(xd, mi_row, mi_col, bsize, cm->sb_size);
    return;
  }

  const int is_intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);
  const int total_loop_num =
      is_intra_sdp_enabled && bsize == BLOCK_64X64 ? 2 : 1;
  if (total_loop_num == 2 && xd->tree_type == SHARED_PART) {
    xd->tree_type = LUMA_PART;
    decode_partition(pbi, td, mi_row, mi_col, reader, bsize, sbi, ptree,
                     ptree_luma, parse_decode_flag);
    xd->tree_type = CHROMA_PART;

    decode_partition(pbi, td, mi_row, mi_col, reader, bsize, sbi, ptree_luma,
                     ptree, parse_decode_flag);
    xd->tree_type = SHARED_PART;
    return;
  }

  // parse_decode_flag takes the following values :
  // 01 - do parse only
  // 10 - do decode only
  // 11 - do parse and decode
  static const block_visitor_fn_t block_visit[4] = { NULL, parse_decode_block,
                                                     decode_block,
                                                     parse_decode_block };
#if CONFIG_LOCAL_INTRABC_ALIGN_RNG
  if (ptree->parent) {
    if (ptree->parent->bsize == cm->sb_size) {
      if (ptree->parent->partition == PARTITION_VERT)
        ptree->sb_root_partition_info = 1;
      else if (ptree->parent->partition == PARTITION_HORZ ||
               ptree->parent->partition == PARTITION_SPLIT)
        ptree->sb_root_partition_info = 2;
      else
        ptree->sb_root_partition_info = -1;
    } else {
      ptree->sb_root_partition_info = ptree->parent->sb_root_partition_info;
    }
  }
#endif  // CONFIG_LOCAL_INTRABC_ALIGN_RNG

  const int is_sb_root = bsize == cm->sb_size;
  if (is_sb_root) {
    xd->is_cfl_allowed_in_sdp =
        is_cfl_allowed_for_sdp(cm, xd, ptree_luma, PARTITION_NONE, bsize);
    ptree->is_cfl_allowed_for_this_chroma_partition = CFL_DISALLOWED_FOR_CHROMA;
    if (!frame_is_intra_only(cm)) {
      ptree->region_type = MIXED_INTER_INTRA_REGION;
      ptree->extended_sdp_allowed_flag = cm->seq_params.enable_extended_sdp;
    } else {
      ptree->region_type = INTRA_REGION;
      ptree->extended_sdp_allowed_flag = 0;
    }
  }

  if (parse_decode_flag & 1) {
    if (is_sb_root) {
#if CONFIG_CWG_F317
      if (cm->bru.enabled || cm->bridge_frame_info.is_bridge_frame) {
#else
      if (cm->bru.enabled) {
#endif  // CONFIG_CWG_F317
        const int mi_grid_idx = get_mi_grid_idx(&cm->mi_params, mi_row, mi_col);
        const int mi_alloc_idx =
            get_alloc_mi_idx(&cm->mi_params, mi_row, mi_col);
        cm->mi_params.mi_grid_base[mi_grid_idx] =
            &cm->mi_params.mi_alloc[mi_alloc_idx];
        // 'xd->mi' should point to an offset in 'mi_grid_base';
        xd->mi = cm->mi_params.mi_grid_base + mi_grid_idx;
        xd->mi[0]->sb_type[xd->tree_type == CHROMA_PART] = bsize;
        xd->mi_row = mi_row;
        xd->mi_col = mi_col;
#if CONFIG_CWG_F317
        if (!cm->bridge_frame_info.is_bridge_frame) {
#endif  // CONFIG_CWG_F317
          xd->sbi->sb_active_mode = read_bru_mode(cm, xd, reader);
          set_active_map(cm, mi_col, mi_row, xd->sbi->sb_active_mode);
#if CONFIG_CWG_F317
        }  // CONFIG_CWG_F317
#endif
      }
      set_sb_mv_precision(sbi, pbi);
    }
    const int plane_start = get_partition_plane_start(xd->tree_type);
    const int plane_end =
        get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
    for (int plane = plane_start; plane < plane_end; ++plane) {
      int rcol0, rcol1, rrow0, rrow1;
      if ((cm->rst_info[plane].frame_restoration_type != RESTORE_NONE) &&
          av1_loop_restoration_corners_in_sb(cm, plane, mi_row, mi_col, bsize,
                                             &rcol0, &rcol1, &rrow0, &rrow1)) {
#if !CONFIG_LR_FRAMEFILTERS_IN_HEADER
        if (is_frame_filters_enabled(plane) &&
            to_readwrite_framefilters(&cm->rst_info[plane], mi_row, mi_col)) {
          read_wienerns_framefilters(cm, xd, plane, reader);
        }
#endif  // !CONFIG_LR_FRAMEFILTERS_IN_HEADER
        const int rstride = cm->rst_info[plane].horz_units_per_frame;
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

    if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
      ptree_luma->bsize = bsize;
      ptree_luma->mi_row = mi_row;
      ptree_luma->mi_col = mi_col;
      ptree_luma->is_settled = 1;
      ptree_luma->is_cfl_allowed_for_this_chroma_partition =
          CFL_DISALLOWED_FOR_CHROMA;
    }

    PARTITION_TREE *parent = ptree->parent;
    set_chroma_ref_info(
        xd->tree_type, mi_row, mi_col, ptree->index, bsize,
        &ptree->chroma_ref_info, parent ? &parent->chroma_ref_info : NULL,
        parent ? parent->bsize : BLOCK_INVALID,
        parent ? parent->partition : PARTITION_NONE, ss_x, ss_y);

    partition = !is_partition_point(bsize)
                    ? PARTITION_NONE
                    : read_partition(cm, xd, mi_row, mi_col, reader, has_rows,
                                     has_cols, ptree, ptree_luma, bsize);
    ptree->is_cfl_allowed_for_this_chroma_partition |=
        is_cfl_allowed_for_sdp(cm, xd, ptree_luma, partition, bsize);
    CFL_ALLOWED_FOR_SDP_TYPE is_cfl_allowed_in_sdp =
        ptree->is_cfl_allowed_for_this_chroma_partition;
    ptree->partition = partition;

    if (!is_sb_root && parent) {
      if (parent->extended_sdp_allowed_flag)
        ptree->extended_sdp_allowed_flag =
            is_extended_sdp_allowed(cm->seq_params.enable_extended_sdp,
                                    parent->bsize, parent->partition);
      else
        ptree->extended_sdp_allowed_flag = 0;
      if (!frame_is_intra_only(cm) && ptree->partition &&
          parent->region_type != INTRA_REGION &&
          ptree->extended_sdp_allowed_flag &&
          bru_is_sb_active(cm, mi_col, mi_row) &&
#if CONFIG_CWG_F317
          !cm->bridge_frame_info.is_bridge_frame &&
#endif  // CONFIG_CWG_F317
          is_bsize_allowed_for_extended_sdp(bsize, ptree->partition)) {
        const int ctx = get_intra_region_context(bsize);
        ptree->region_type =
            aom_read_symbol(reader, xd->tile_ctx->region_type_cdf[ctx],
                            REGION_TYPES, ACCT_INFO("region_type"));
        if (ptree->region_type == INTRA_REGION) xd->tree_type = LUMA_PART;
      } else if (!frame_is_intra_only(cm)) {
        ptree->region_type = parent->region_type;
      } else {
        ptree->region_type = INTRA_REGION;
      }
    }

    switch (partition) {
      case PARTITION_HORZ_4A:
      case PARTITION_HORZ_4B:
      case PARTITION_VERT_4A:
      case PARTITION_VERT_4B:
      case PARTITION_SPLIT:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
        ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
        ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
        if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
          ptree_luma->sub_tree[0] = av1_alloc_ptree_node(ptree_luma, 0);
          ptree_luma->sub_tree[1] = av1_alloc_ptree_node(ptree_luma, 1);
          ptree_luma->sub_tree[2] = av1_alloc_ptree_node(ptree_luma, 2);
          ptree_luma->sub_tree[3] = av1_alloc_ptree_node(ptree_luma, 3);
        }
        break;
      case PARTITION_HORZ:
      case PARTITION_VERT:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);

        if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
          ptree_luma->sub_tree[0] = av1_alloc_ptree_node(ptree_luma, 0);
          ptree_luma->sub_tree[1] = av1_alloc_ptree_node(ptree_luma, 1);
        }
        break;
      case PARTITION_HORZ_3:
      case PARTITION_VERT_3:
        ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
        ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
        ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
        ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);

        if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
          ptree_luma->sub_tree[0] = av1_alloc_ptree_node(ptree_luma, 0);
          ptree_luma->sub_tree[1] = av1_alloc_ptree_node(ptree_luma, 1);
          ptree_luma->sub_tree[2] = av1_alloc_ptree_node(ptree_luma, 2);
          ptree_luma->sub_tree[3] = av1_alloc_ptree_node(ptree_luma, 3);
        }
        break;
      default: break;
    }
    switch (partition) {
      case PARTITION_NONE:
        xd->is_cfl_allowed_in_sdp = is_cfl_allowed_in_sdp;
        break;
      case PARTITION_HORZ_4A:
      case PARTITION_HORZ_4B:
      case PARTITION_VERT_4A:
      case PARTITION_VERT_4B:
      case PARTITION_HORZ_3:
      case PARTITION_VERT_3:
      case PARTITION_SPLIT:
        ptree->sub_tree[0]->is_cfl_allowed_for_this_chroma_partition =
            is_cfl_allowed_in_sdp;
        ptree->sub_tree[1]->is_cfl_allowed_for_this_chroma_partition =
            is_cfl_allowed_in_sdp;
        ptree->sub_tree[2]->is_cfl_allowed_for_this_chroma_partition =
            is_cfl_allowed_in_sdp;
        ptree->sub_tree[3]->is_cfl_allowed_for_this_chroma_partition =
            is_cfl_allowed_in_sdp;
        break;
      case PARTITION_HORZ:
      case PARTITION_VERT:
        ptree->sub_tree[0]->is_cfl_allowed_for_this_chroma_partition =
            is_cfl_allowed_in_sdp;
        ptree->sub_tree[1]->is_cfl_allowed_for_this_chroma_partition =
            is_cfl_allowed_in_sdp;
        break;
      default: break;
    }
  } else {
    partition = ptree->partition;
    const PARTITION_TREE *parent = ptree->parent;
    if (!is_sb_root && parent) {
      if (!frame_is_intra_only(cm) && !cm->seq_params.monochrome &&
          ptree->partition && parent->region_type != INTRA_REGION &&
          ptree->region_type == INTRA_REGION) {
        xd->tree_type = LUMA_PART;
      }
    }
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
  BLOCK_SIZE test_subsize = subsize;
  if (xd->tree_type == SHARED_PART) {
    const PARTITION_TREE *parent = ptree;
    CHROMA_REF_INFO chroma_ref_info;
    const int index =
        (partition == PARTITION_HORZ || partition == PARTITION_VERT) ? 1 : 0;
    set_chroma_ref_info(xd->tree_type, mi_row, mi_col, index, subsize,
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
  // Check that chroma ref block isn't completely outside the boundary.
  if (!is_chroma_ref_within_boundary(
          cm, xd->tree_type, ptree->chroma_ref_info.is_chroma_ref, mi_row,
          mi_col, bsize, partition, pd_u->subsampling_x, pd_u->subsampling_y)) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid partitioning %d at location [%d, %d]: chroma "
                       "info not coded.",
                       partition, mi_row << MI_SIZE_LOG2,
                       mi_col << MI_SIZE_LOG2);
  }

#define DEC_BLOCK_STX_ARG
#define DEC_BLOCK_EPT_ARG partition,
#define DEC_BLOCK(db_r, db_c, db_subsize, index)                               \
  block_visit[parse_decode_flag](pbi, td, DEC_BLOCK_STX_ARG(db_r), (db_c),     \
                                 reader, DEC_BLOCK_EPT_ARG(db_subsize), ptree, \
                                 index)
#define DEC_PARTITION(db_r, db_c, db_subsize, index)                 \
  decode_partition(pbi, td, DEC_BLOCK_STX_ARG(db_r), (db_c), reader, \
                   (db_subsize), sbi, ptree->sub_tree[(index)],      \
                   get_partition_subtree_const(ptree_luma, index),   \
                   parse_decode_flag)

  switch (partition) {
    case PARTITION_NONE: DEC_BLOCK(mi_row, mi_col, subsize, 0); break;
    case PARTITION_HORZ:
      DEC_PARTITION(mi_row, mi_col, subsize, 0);
      DEC_PARTITION(mi_row + hbs_h, mi_col, subsize, 1);
      break;
    case PARTITION_VERT:
      DEC_PARTITION(mi_row, mi_col, subsize, 0);
      DEC_PARTITION(mi_row, mi_col + hbs_w, subsize, 1);
      break;
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      int this_mi_row = mi_row;
      DEC_PARTITION(this_mi_row, mi_col, subsize, 0);
      this_mi_row += ebs_h;
      DEC_PARTITION(this_mi_row, mi_col, bsize_med, 1);
      this_mi_row += 2 * ebs_h;
      DEC_PARTITION(this_mi_row, mi_col, bsize_big, 2);
      this_mi_row += 4 * ebs_h;
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
      DEC_PARTITION(this_mi_row, mi_col, bsize_big, 1);
      this_mi_row += 4 * ebs_h;
      DEC_PARTITION(this_mi_row, mi_col, bsize_med, 2);
      this_mi_row += 2 * ebs_h;
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
      DEC_PARTITION(mi_row, this_mi_col, bsize_med, 1);
      this_mi_col += 2 * ebs_w;
      DEC_PARTITION(mi_row, this_mi_col, bsize_big, 2);
      this_mi_col += 4 * ebs_w;
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
      DEC_PARTITION(mi_row, this_mi_col, bsize_big, 1);
      this_mi_col += 4 * ebs_w;
      DEC_PARTITION(mi_row, this_mi_col, bsize_med, 2);
      this_mi_col += 2 * ebs_w;
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
    default: assert(0 && "Invalid partition type");
  }

  PARTITION_TREE *parent = ptree->parent;
  if (!is_sb_root && parent) {
    if (!frame_is_intra_only(cm) && !cm->seq_params.monochrome &&
        ptree->partition && parent->region_type != INTRA_REGION &&
        ptree->region_type == INTRA_REGION) {
      // decode chroma part in one intra region
      xd->tree_type = CHROMA_PART;
      DEC_BLOCK(mi_row, mi_col, bsize, 0);
      // reset back to shared part
      xd->tree_type = SHARED_PART;
    }
  }

#undef DEC_PARTITION
#undef DEC_BLOCK
#undef DEC_BLOCK_EPT_ARG
#undef DEC_BLOCK_STX_ARG

  if (parse_decode_flag & 1) {
    update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
    if (is_intra_sdp_enabled && xd->tree_type == SHARED_PART) {
      xd->tree_type = CHROMA_PART;
      update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize,
                                   partition);
      xd->tree_type = SHARED_PART;
    }
  }
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
  xd->tree_type = SHARED_PART;
  const int is_intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);

  if (parse_decode_flag & 1) {
    av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
    if (is_intra_sdp_enabled) av1_reset_ptree_in_sbi(xd->sbi, CHROMA_PART);
  }
  decode_partition(
      pbi, td, mi_row, mi_col, reader, bsize, xd->sbi,
      td->dcb.xd.sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
      (is_intra_sdp_enabled ? td->dcb.xd.sbi->ptree_root[1] : NULL),
      parse_decode_flag);

#if CONFIG_CWG_F317
  if ((cm->bru.enabled && cm->current_frame.frame_type != KEY_FRAME) ||
      cm->bridge_frame_info.is_bridge_frame) {
    if (pbi->bru_opt_mode && !cm->bridge_frame_info.is_bridge_frame) {
#else
  if (cm->bru.enabled && cm->current_frame.frame_type != KEY_FRAME) {
    if (pbi->bru_opt_mode) {
#endif  // CONFIG_CWG_F317
      if (bru_is_sb_available(cm, mi_col, mi_row)) {
#ifndef NDEBUG
        RefCntBuffer *ref_buf = get_ref_frame_buf(cm, cm->bru.update_ref_idx);
        assert(ref_buf != NULL);
#endif
        // for active sb, update to bru ref
        // for support sb, prepare copy to cur_frame (prepare for intra)
        if (bru_is_sb_active(cm, mi_col, mi_row))
          bru_update_sb(cm, mi_col, mi_row);
        else
          bru_copy_sb(cm, mi_col, mi_row);
      }
    } else {
#if CONFIG_CWG_F317
      if (!bru_is_sb_active(cm, mi_col, mi_row) ||
          cm->bridge_frame_info.is_bridge_frame) {
#else
      if (!bru_is_sb_active(cm, mi_col, mi_row)) {
#endif
        if (cm->seq_params.order_hint_info.enable_ref_frame_mvs) {
          const int sb_size = cm->seq_params.sb_size;
          // set cur_frame mvs to 0
          const int w = mi_size_wide[sb_size];
          const int h = mi_size_high[sb_size];
          const int x_inside_boundary =
              AOMMIN(w, cm->mi_params.mi_cols - mi_col) << MI_SIZE_LOG2;
          const int y_inside_boundary =
              AOMMIN(h, cm->mi_params.mi_rows - mi_row) << MI_SIZE_LOG2;
          bru_zero_sb_mvs(cm, -1, mi_row, mi_col,
                          x_inside_boundary >> MI_SIZE_LOG2,
                          y_inside_boundary >> MI_SIZE_LOG2);
        }
      }
    }
  }
#if CONFIG_INSPECTION
  if (pbi->inspect_sb_cb != NULL) {
    (*pbi->inspect_sb_cb)(pbi, pbi->inspect_ctx);
  }
#endif  // CONFIG_INSPECTION
}
static AOM_INLINE void setup_bru_active_info(AV1_COMMON *const cm,
                                             struct aom_read_bit_buffer *rb) {
  init_bru_params(cm);
  if (cm->current_frame.frame_type != INTER_FRAME) {
    return;
  }
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    return;
  }
#endif  // CONFIG_CWG_F317
  // need to reresh bru.active_mode_map every frame
  memset(cm->bru.active_mode_map, 2, sizeof(uint8_t) * cm->bru.total_units);
  if (cm->seq_params.enable_bru) {
    cm->bru.enabled = aom_rb_read_bit(rb);
    if (cm->bru.enabled) {
      memset(cm->bru.active_mode_map, 0, sizeof(uint8_t) * cm->bru.total_units);
      cm->bru.update_ref_idx = aom_rb_read_literal(
          rb, aom_ceil_log2(cm->ref_frames_info.num_total_refs));
      cm->bru.frame_inactive_flag = aom_rb_read_bit(rb);
      if (cm->bru.frame_inactive_flag) {
        cm->features.disable_cdf_update = 1;
      }
      if (!cm->show_frame) {
        aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                           "Invalid show_frame: BRU frame must be show_frame");
      }
    }
  }
}
static AOM_INLINE void setup_segmentation(AV1_COMMON *const cm,
                                          struct aom_read_bit_buffer *rb) {
  struct segmentation *const seg = &cm->seg;

  seg->update_map = 0;
  seg->update_data = 0;
  seg->temporal_update = 0;
#if CONFIG_EXT_SEG
  seg->enable_ext_seg = cm->seq_params.enable_ext_seg;
#endif  // CONFIG_EXT_SEG

  seg->enabled = aom_rb_read_bit(rb);
  if (!seg->enabled) {
    if (cm->cur_frame->seg_map) {
      memset(cm->cur_frame->seg_map, 0,
             (cm->cur_frame->mi_rows * cm->cur_frame->mi_cols));
    }

    memset(seg, 0, sizeof(*seg));
#if CONFIG_EXT_SEG
    seg->enable_ext_seg = cm->seq_params.enable_ext_seg;
#endif  // CONFIG_EXT_SEG
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
  if (cm->features.derived_primary_ref_frame == PRIMARY_REF_NONE) {
    // These frames can't use previous frames, so must signal map + features
    seg->update_map = 1;
    seg->temporal_update = 0;
    seg->update_data = 1;
#if CONFIG_EXT_SEG
    seg->enable_ext_seg = cm->seq_params.enable_ext_seg;
#endif  // CONFIG_EXT_SEG
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
#if CONFIG_EXT_SEG
    const int max_seg_num =
        cm->seg.enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
#else   // CONFIG_EXT_SEG
    const int max_seg_num = MAX_SEGMENTS;
#endif  // CONFIG_EXT_SEG
    for (int i = 0; i < max_seg_num; i++) {
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
#if CONFIG_EXT_SEG
  seg->enable_ext_seg = cm->seq_params.enable_ext_seg;
#endif  // CONFIG_EXT_SEG
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

static AOM_INLINE void decode_restoration_mode(AV1_COMMON *cm,
                                               struct aom_read_bit_buffer *rb) {
  assert(!cm->features.all_lossless);
  const int num_planes = av1_num_planes(cm);
  int luma_none = 1, chroma_none = 1;
  for (int p = 0; p < num_planes; ++p) {
    RestorationInfo *rsi = &cm->rst_info[p];
    rsi->frame_filters_on = 0;
    cm->cur_frame->rst_info[p].frame_filters_on = 0;
    rsi->temporal_pred_flag = 0;
    cm->cur_frame->rst_info[p].temporal_pred_flag = 0;
#if CONFIG_CWG_F317
    if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
    if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
      rsi->frame_restoration_type = RESTORE_NONE;
      continue;
    }
    uint8_t plane_lr_tools_disable_mask =
        cm->seq_params.lr_tools_disable_mask[p > 0];
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

    if (rsi->frame_restoration_type != RESTORE_NONE) {
      luma_none &= p > 0;
      chroma_none &= p == 0;
    }
    const int is_wiener_nonsep_possible =
        rsi->frame_restoration_type == RESTORE_WIENER_NONSEP ||
        rsi->frame_restoration_type == RESTORE_SWITCHABLE;
    if (is_wiener_nonsep_possible) {
      rsi->frame_filters_initialized = 0;
      if (is_frame_filters_enabled(p)) {
        const int read_frame_filters_on_off = 1;
        if (read_frame_filters_on_off) {
          rsi->frame_filters_on = aom_rb_read_bit(rb);
          rsi->rst_ref_pic_idx = 0;
          rsi->temporal_pred_flag = 0;
          if (rsi->frame_filters_on) {
            const int num_ref_frames = (frame_is_intra_only(cm)
#if CONFIG_F322_OBUER_ERM
                                        || frame_is_sframe(cm)
#else
                                        || cm->features.error_resilient_mode
#endif
                                            )
                                           ? 0
                                           : cm->ref_frames_info.num_total_refs;

            if (num_ref_frames > 0)
              rsi->temporal_pred_flag = aom_rb_read_bit(rb);
            if (rsi->temporal_pred_flag && num_ref_frames > 1) {
              rsi->rst_ref_pic_idx = aom_rb_read_literal(
                  rb,
                  aom_ceil_log2(num_ref_frames));  // read_lr_reference_idx
            }
          }

          if (rsi->temporal_pred_flag) {
            RestorationInfo tmp_rsi =
                get_ref_frame_buf(cm, rsi->rst_ref_pic_idx)->rst_info[p];
            if (!tmp_rsi.frame_filters_on) {
              const int alternate_plane = alternate_ref_plane(p);
              assert(alternate_plane != -1);
              tmp_rsi = get_ref_frame_buf(cm, rsi->rst_ref_pic_idx)
                            ->rst_info[alternate_plane];
            }
            if (!tmp_rsi.frame_filters_on) {
              aom_internal_error(
                  &cm->error, AOM_CODEC_ERROR,
                  "Invalid rst_ref_pic_idx: ref frame frame filter disabled");
            }
            av1_copy_rst_frame_filters(rsi, &tmp_rsi);
            rsi->frame_filters_initialized = 1;

            av1_copy_rst_frame_filters(&cm->cur_frame->rst_info[p], rsi);
          } else {
            if (rsi->frame_filters_on && max_num_classes(p) > 1) {
              rsi->num_filter_classes = decode_num_filter_classes(
                  aom_rb_read_literal(rb, NUM_FILTER_CLASSES_BITS));
            } else
              rsi->num_filter_classes = 1;
          }
        } else {
          rsi->frame_filters_on = 0;
          rsi->num_filter_classes = default_num_classes(p);
          assert(rsi->num_filter_classes == 1);
        }
      } else {
        rsi->frame_filters_on = 0;
        rsi->num_filter_classes = NUM_WIENERNS_CLASS_INIT_CHROMA;
      }
    }
    assert(IMPLIES(!rsi->frame_filters_on, !rsi->temporal_pred_flag));
  }
#if CONFIG_MINIMUM_LR_UNIT_SIZE_64x64
  int subsampling_xy =
      AOMMAX(cm->seq_params.subsampling_x, cm->seq_params.subsampling_y);

  cm->rst_info[0].restoration_unit_size = RESTORATION_UNITSIZE_MAX >> 3;
  cm->rst_info[1].restoration_unit_size =
      RESTORATION_UNITSIZE_MAX >> (3 + subsampling_xy);
  cm->rst_info[2].restoration_unit_size = cm->rst_info[1].restoration_unit_size;

#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
  if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
    if (num_planes > 1) {
      cm->rst_info[1].restoration_unit_size =
          RESTORATION_UNITSIZE_MAX >> (3 + subsampling_xy);
      cm->rst_info[2].restoration_unit_size =
          cm->rst_info[1].restoration_unit_size;
    }
    return;
  }

  if (!luma_none) {
    int size = RESTORATION_UNITSIZE_MAX;
    if (aom_rb_read_bit(rb))
      cm->rst_info[0].restoration_unit_size = size >> 1;
    else {
#if CONFIG_RU_SIZE_RESTRICTION
      if (cm->mib_size == 64) {  // sb_szie == 256
        cm->rst_info[0].restoration_unit_size = size;
      } else {
        if (aom_rb_read_bit(rb))
          cm->rst_info[0].restoration_unit_size = size;
        else {
          if (cm->mib_size == 32) {  // sb_szie == 128
            cm->rst_info[0].restoration_unit_size = size >> 2;
          } else {
            if (aom_rb_read_bit(rb))
              cm->rst_info[0].restoration_unit_size = size >> 2;
            else
              cm->rst_info[0].restoration_unit_size = size >> 3;
          }
        }
      }
#else
      if (aom_rb_read_bit(rb))
        cm->rst_info[0].restoration_unit_size = size;
      else {
        if (aom_rb_read_bit(rb))
          cm->rst_info[0].restoration_unit_size = size >> 2;
        else
          cm->rst_info[0].restoration_unit_size = size >> 3;
      }
#endif  // CONFIG_RU_SIZE_RESTRICTION
    }
  }
  if (num_planes > 1) {
    if (!chroma_none) {
      int size = RESTORATION_UNITSIZE_MAX >> subsampling_xy;
      if (aom_rb_read_bit(rb))
        cm->rst_info[1].restoration_unit_size = size >> 1;
      else {
#if CONFIG_RU_SIZE_RESTRICTION
        if (cm->mib_size == 64) {  // sb_szie == 256
          cm->rst_info[1].restoration_unit_size = size;
        } else {
          if (aom_rb_read_bit(rb))
            cm->rst_info[1].restoration_unit_size = size;
          else {
            if (cm->mib_size == 32) {  // sb_szie == 128
              cm->rst_info[1].restoration_unit_size = size >> 2;
            } else {
              if (aom_rb_read_bit(rb))
                cm->rst_info[1].restoration_unit_size = size >> 2;
              else
                cm->rst_info[1].restoration_unit_size = size >> 3;
            }
          }
        }
#else
        if (aom_rb_read_bit(rb))
          cm->rst_info[1].restoration_unit_size = size;
        else {
          if (aom_rb_read_bit(rb))
            cm->rst_info[1].restoration_unit_size = size >> 2;
          else
            cm->rst_info[1].restoration_unit_size = size >> 3;
        }
#endif  // CONFIG_RU_SIZE_RESTRICTION
      }
    }
    cm->rst_info[2].restoration_unit_size =
        cm->rst_info[1].restoration_unit_size;
  }

#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  // add the normative restriction of ru size when
  // disable_loopfilters_across_tiles == 1
  int max_plane_ru_size =
      AOMMAX(cm->rst_info[0].restoration_unit_size,
             cm->rst_info[1].restoration_unit_size << subsampling_xy);
  for (int tile_row = 0; tile_row < cm->tiles.rows - 1; tile_row++) {
    for (int tile_col = 0; tile_col < cm->tiles.cols - 1; tile_col++) {
      int tile_w = (cm->tiles.col_start_sb[tile_col + 1] -
                    cm->tiles.col_start_sb[tile_col])
                   << (cm->mib_size_log2 + MI_SIZE_LOG2);
      int tile_h = (cm->tiles.row_start_sb[tile_row + 1] -
                    cm->tiles.row_start_sb[tile_row])
                   << (cm->mib_size_log2 + MI_SIZE_LOG2);

      if (tile_w % max_plane_ru_size || tile_h % max_plane_ru_size) {
        aom_internal_error(
            &cm->error, AOM_CODEC_ERROR,
            "Invalid RU size, RU size shall be an integer divisor of any tile "
            "width or height when disable-loopfilters-across-tiles=1.");
        return;
      }
    }
  }
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
#else
  const int frame_width = cm->width;
  const int frame_height = cm->height;
  set_restoration_unit_size(
#if CONFIG_RU_SIZE_RESTRICTION
      cm,
#endif  // CONFIG_RU_SIZE_RESTRICTION
      frame_width, frame_height, cm->seq_params.subsampling_x,
      cm->seq_params.subsampling_y, cm->rst_info);
  int size = cm->rst_info[0].max_restoration_unit_size;

  cm->rst_info[0].restoration_unit_size =
      cm->rst_info[0].max_restoration_unit_size;
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
  if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
    if (num_planes > 1) {
      cm->rst_info[1].restoration_unit_size =
          cm->rst_info[1].max_restoration_unit_size;
      cm->rst_info[2].restoration_unit_size =
          cm->rst_info[1].restoration_unit_size;
    }
    return;
  }
  if (!luma_none) {
    if (aom_rb_read_bit(rb)) cm->rst_info[0].restoration_unit_size = size >> 1;
#if CONFIG_RU_SIZE_RESTRICTION
    else if (cm->mib_size != 64) {
      if (aom_rb_read_bit(rb))
        cm->rst_info[0].restoration_unit_size = size;
      else
        cm->rst_info[0].restoration_unit_size = size >> 2;
    } else {
      cm->rst_info[0].restoration_unit_size = size;
    }
#else
    else {
      if (aom_rb_read_bit(rb))
        cm->rst_info[0].restoration_unit_size = size;
      else
        cm->rst_info[0].restoration_unit_size = size >> 2;
    }
#endif  // CONFIG_RU_SIZE_RESTRICTION
  }
  if (num_planes > 1) {
    cm->rst_info[1].restoration_unit_size =
        cm->rst_info[1].max_restoration_unit_size;
    if (!chroma_none) {
      size = cm->rst_info[1].max_restoration_unit_size;
      if (aom_rb_read_bit(rb))
        cm->rst_info[1].restoration_unit_size = size >> 1;
#if CONFIG_RU_SIZE_RESTRICTION
      else if (cm->mib_size != 64) {
        if (aom_rb_read_bit(rb))
          cm->rst_info[1].restoration_unit_size = size;
        else
          cm->rst_info[1].restoration_unit_size = size >> 2;
      } else {
        cm->rst_info[1].restoration_unit_size = size;
      }
#else
      else {
        if (aom_rb_read_bit(rb))
          cm->rst_info[1].restoration_unit_size = size;
        else
          cm->rst_info[1].restoration_unit_size = size >> 2;
      }
#endif  // CONFIG_RU_SIZE_RESTRICTION
    }
    cm->rst_info[2].restoration_unit_size =
        cm->rst_info[1].restoration_unit_size;
  }
#endif  // CONFIG_MINIMUM_LR_UNIT_SIZE_64x64
#if CONFIG_LR_FRAMEFILTERS_IN_HEADER
  for (int p = 0; p < num_planes; ++p) {
    if (is_frame_filters_enabled(p) &&
        to_readwrite_framefilters(&cm->rst_info[p], 0, 0)) {
      read_wienerns_framefilters_hdr(cm, p, rb);
    }
  }
#endif  // CONFIG_LR_FRAMEFILTERS_IN_HEADER
}

// Decodes match indices.
#if CONFIG_LR_FRAMEFILTERS_IN_HEADER
static void read_match_indices_hdr(int plane, WienerNonsepInfo *wienerns_info,
                                   struct aom_read_bit_buffer *rb, int nopcw) {
  assert(NUM_MATCH_GROUPS == 3);
  int group_counts[NUM_MATCH_GROUPS];
  set_group_counts(plane, wienerns_info->num_classes,
                   wienerns_info->num_ref_filters, group_counts, nopcw);
  for (int c_id = 0; c_id < wienerns_info->num_classes; ++c_id) {
    // Read group-id.
    int only;
    const int pred_group =
        predict_group(c_id, wienerns_info->match_indices, group_counts, &only);
    int group = 0;
    int group_bit = only ? 0 : aom_rb_read_bit(rb);
    if (group_bit == 0) {
      // group-id matches prediction.
      group = pred_group;
    } else {
      int zero_group = -1;
      for (int i = 0; i < NUM_MATCH_GROUPS; ++i) {
        if (i == pred_group) continue;
        if (group_counts[i] == 0) {
          // There is a group-id with zero count.
          zero_group = i;
          break;
        }
      }
      if (zero_group != -1) {
        // group-id is the remaining non-zero group.
        group = 3 - (pred_group + zero_group);
      } else {
        const int convert_larger[] = { 2, 2, 1 };
        const int convert_smaller[] = { 1, 0, 0 };
        group_bit = aom_rb_read_bit(rb);
        // Infer group-id around pred_group.
        if (group_bit) {
          group = convert_larger[pred_group];
        } else {
          group = convert_smaller[pred_group];
        }
      }
    }
    // Decode match index with known group-id.
    const int ref = predict_within_group(
        group, c_id, wienerns_info->match_indices, group_counts);
    const int base = get_group_base(group, group_counts);
    const int n = group == 0 ? c_id + 1 : group_counts[group];
    int decoded_match = base;
    if (n > 1) {
      decoded_match +=
          (int)aom_rb_read_primitive_refsubexpfin(rb, n, 4, ref - base);
    }

    wienerns_info->match_indices[c_id] = decoded_match;
  }
}
#else
static void read_match_indices(int plane, WienerNonsepInfo *wienerns_info,
                               aom_reader *rb, int nopcw) {
  assert(NUM_MATCH_GROUPS == 3);
  int group_counts[NUM_MATCH_GROUPS];
  set_group_counts(plane, wienerns_info->num_classes,
                   wienerns_info->num_ref_filters, group_counts, nopcw);
  for (int c_id = 0; c_id < wienerns_info->num_classes; ++c_id) {
    // Read group-id.
    int only;
    const int pred_group =
        predict_group(c_id, wienerns_info->match_indices, group_counts, &only);
    int group = 0;
    int group_bit = only ? 0 : aom_read_bit(rb, ACCT_INFO("match"));
    if (group_bit == 0) {
      // group-id matches prediction.
      group = pred_group;
    } else {
      int zero_group = -1;
      for (int i = 0; i < NUM_MATCH_GROUPS; ++i) {
        if (i == pred_group) continue;
        if (group_counts[i] == 0) {
          // There is a group-id with zero count.
          zero_group = i;
          break;
        }
      }
      if (zero_group != -1) {
        // group-id is the remaining non-zero group.
        group = 3 - (pred_group + zero_group);
      } else {
        const int convert_larger[] = { 2, 2, 1 };
        const int convert_smaller[] = { 1, 0, 0 };
        group_bit = aom_read_bit(rb, ACCT_INFO("match"));
        // Infer group-id around pred_group.
        if (group_bit) {
          group = convert_larger[pred_group];
        } else {
          group = convert_smaller[pred_group];
        }
      }
    }
    // Decode match index with known group-id.
    const int ref = predict_within_group(
        group, c_id, wienerns_info->match_indices, group_counts);
    const int base = get_group_base(group, group_counts);
    const int n = group == 0 ? c_id + 1 : group_counts[group];
    int decoded_match = base;
    if (n > 1) {
      decoded_match += (int)aom_read_primitive_refsubexpfin(
          rb, n, 4, ref - base, ACCT_INFO("match"));
    }

    wienerns_info->match_indices[c_id] = decoded_match;
  }
}
#endif  // CONFIG_LR_FRAMEFILTERS_IN_HEADER

#if CONFIG_LR_FRAMEFILTERS_IN_HEADER
// Read frame level wiener filters from the uncompressed frame header
static void read_wienerns_framefilters_hdr(AV1_COMMON *cm, int plane,
                                           struct aom_read_bit_buffer *rb) {
  const int base_qindex = cm->quant_params.base_qindex;
  const int is_uv = plane != AOM_PLANE_Y;
  const int nopcw = disable_pcwiener_filters_in_framefilters(&cm->seq_params);
  RestorationInfo *rsi = &cm->rst_info[plane];
  assert(is_frame_filters_enabled(plane));
  assert(rsi->frame_filters_on && !rsi->frame_filters_initialized);
  if (cm->frame_filter_dictionary == NULL) {
    allocate_frame_filter_dictionary(cm);
    translate_pcwiener_filters_to_wienerns(cm);
  }
  *cm->num_ref_filters = set_frame_filter_dictionary(
      plane, cm, rsi->num_filter_classes, cm->frame_filter_dictionary,
      cm->frame_filter_dictionary_stride);
  int16_t *frame_filter_dictionary = cm->frame_filter_dictionary;
  const int dict_stride = cm->frame_filter_dictionary_stride;
  assert(frame_filter_dictionary != NULL);
  assert(dict_stride > 0);

  int skip_filter_read_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  const int num_classes = rsi->num_filter_classes;
  rsi->frame_filters.num_classes = num_classes;
  rsi->frame_filters.num_ref_filters = *cm->num_ref_filters;
  assert(num_classes <= WIENERNS_MAX_CLASSES);
  assert(!rsi->temporal_pred_flag);

  read_match_indices_hdr(plane, &rsi->frame_filters, rb, nopcw);

  for (int c_id = 0; c_id < num_classes; ++c_id) {
    const int exact_match = aom_rb_read_bit(rb);
    skip_filter_read_for_class[c_id] = exact_match;
  }
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(base_qindex, is_uv);
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;
  WienerNonsepInfoBank bank = { 0 };
  bank.filter[0].num_classes = num_classes;
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    fill_first_slot_of_bank_with_filter_match(
        plane, &bank, &rsi->frame_filters, rsi->frame_filters.match_indices,
        base_qindex, c_id, frame_filter_dictionary, dict_stride, nopcw);
    if (skip_filter_read_for_class[c_id]) {
      copy_nsfilter_taps_for_class(
          &rsi->frame_filters, av1_constref_from_wienerns_bank(&bank, 0, c_id),
          c_id);
      continue;
    }
    const WienerNonsepInfo *ref_wienerns_info =
        av1_constref_from_wienerns_bank(&bank, 0, c_id);
    assert(ref_wienerns_info->num_classes == num_classes);
    int16_t *wienerns_info_nsfilter = nsfilter_taps(&rsi->frame_filters, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);

    memset(wienerns_info_nsfilter, 0,
           nsfilter_params->ncoeffs * sizeof(wienerns_info_nsfilter[0]));

    const int beg_feat = 0;
    int end_feat = nsfilter_params->ncoeffs;
    int ncoeffs1, ncoeffs2;
    int ncoeffs =
        config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
    assert(nsfilter_params->ncoeffs == ncoeffs);
    (void)ncoeffs;
    int s = 0;
    for (int i = 0; i < nsfilter_params->nsubsets - 1; ++i) {
      const int filter_length_bit = aom_rb_read_bit(rb);
      s += filter_length_bit;
      if (!filter_length_bit) break;
    }
    assert((end_feat & 1) == 0);

    int sym = 1;
    if (!skip_sym_bit(nsfilter_params, s)) {
      assert(is_uv);
      sym = aom_rb_read_bit(rb);
    }

    for (int i = beg_feat; i < end_feat; ++i) {
      if (!nsfilter_params->subset_config[s][i]) continue;
      wienerns_info_nsfilter[i] =
          aom_rb_read_primitive_refsubexpfin(
              rb, 1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID],
              wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID] - 3,
              ref_wienerns_info_nsfilter[i] -
                  wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID]) +
          wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID];
      const int is_asym_coeff =
          (i < nsfilter_params->nsfilter_config.asymmetric ||
           (i >= ncoeffs1 &&
            i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      if (sym && is_asym_coeff) {
        // Fill in symmetrical tap without reading it
        wienerns_info_nsfilter[i + 1] = wienerns_info_nsfilter[i];
        i++;
      }
    }
  }
  rsi->frame_filters_initialized = 1;
  av1_copy_rst_frame_filters(&cm->cur_frame->rst_info[plane], rsi);
}
#else
static void read_wienerns_framefilters(AV1_COMMON *cm, MACROBLOCKD *xd,
                                       int plane, aom_reader *rb) {
  const int base_qindex = cm->quant_params.base_qindex;
  const int is_uv = plane != AOM_PLANE_Y;
  const int nopcw = disable_pcwiener_filters_in_framefilters(&cm->seq_params);
  RestorationInfo *rsi = &cm->rst_info[plane];
  assert(is_frame_filters_enabled(plane));
  assert(rsi->frame_filters_on && !rsi->frame_filters_initialized);
  if (cm->frame_filter_dictionary == NULL) {
    allocate_frame_filter_dictionary(cm);
    translate_pcwiener_filters_to_wienerns(cm);
  }
  *cm->num_ref_filters = set_frame_filter_dictionary(
      plane, cm, rsi->num_filter_classes, cm->frame_filter_dictionary,
      cm->frame_filter_dictionary_stride);
  int16_t *frame_filter_dictionary = cm->frame_filter_dictionary;
  const int dict_stride = cm->frame_filter_dictionary_stride;
  assert(frame_filter_dictionary != NULL);
  assert(dict_stride > 0);

  int skip_filter_read_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  const int num_classes = rsi->num_filter_classes;
  rsi->frame_filters.num_classes = num_classes;
  rsi->frame_filters.num_ref_filters = *cm->num_ref_filters;
  assert(num_classes <= WIENERNS_MAX_CLASSES);
  assert(!rsi->temporal_pred_flag);

  read_match_indices(plane, &rsi->frame_filters, rb, nopcw);

  for (int c_id = 0; c_id < num_classes; ++c_id) {
#if CONFIG_MERGE_PARA_CTX
    const int exact_match = aom_read_bit(rb, ACCT_INFO("exact_match"));
#else
    const int exact_match = aom_read_symbol(rb, xd->tile_ctx->merged_param_cdf,
                                            2, ACCT_INFO("exact_match"));
#endif  // CONFIG_MERGE_PARA_CTX
    skip_filter_read_for_class[c_id] = exact_match;
  }
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(base_qindex, is_uv);
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;
  WienerNonsepInfoBank bank = { 0 };
  bank.filter[0].num_classes = num_classes;
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    fill_first_slot_of_bank_with_filter_match(
        plane, &bank, &rsi->frame_filters, rsi->frame_filters.match_indices,
        base_qindex, c_id, frame_filter_dictionary, dict_stride, nopcw);
    if (skip_filter_read_for_class[c_id]) {
      copy_nsfilter_taps_for_class(
          &rsi->frame_filters, av1_constref_from_wienerns_bank(&bank, 0, c_id),
          c_id);
      continue;
    }
    const WienerNonsepInfo *ref_wienerns_info =
        av1_constref_from_wienerns_bank(&bank, 0, c_id);
    assert(ref_wienerns_info->num_classes == num_classes);
    int16_t *wienerns_info_nsfilter = nsfilter_taps(&rsi->frame_filters, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);

    memset(wienerns_info_nsfilter, 0,
           nsfilter_params->ncoeffs * sizeof(wienerns_info_nsfilter[0]));

    const int beg_feat = 0;
    int end_feat = nsfilter_params->ncoeffs;
    int ncoeffs1, ncoeffs2;
    int ncoeffs =
        config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
    assert(nsfilter_params->ncoeffs == ncoeffs);
    (void)ncoeffs;
    int s = 0;
    for (int i = 0; i < nsfilter_params->nsubsets - 1; ++i) {
      const int filter_length_bit =
          aom_read_symbol(rb, xd->tile_ctx->wienerns_length_cdf[is_uv], 2,
                          ACCT_INFO("wienerns_length"));
      s += filter_length_bit;
      if (!filter_length_bit) break;
    }
    assert((end_feat & 1) == 0);

    int sym = 1;
    if (!skip_sym_bit(nsfilter_params, s)) {
      assert(is_uv);
      sym = aom_read_symbol(rb, xd->tile_ctx->wienerns_uv_sym_cdf, 2,
                            ACCT_INFO("wienerns_uv_sym"));
    }

    for (int i = beg_feat; i < end_feat; ++i) {
      if (!nsfilter_params->subset_config[s][i]) continue;
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
      const int is_asym_coeff =
          (i < nsfilter_params->nsfilter_config.asymmetric ||
           (i >= ncoeffs1 &&
            i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      if (sym && is_asym_coeff) {
        // Fill in symmetrical tap without reading it
        wienerns_info_nsfilter[i + 1] = wienerns_info_nsfilter[i];
        i++;
      }
    }
  }
  rsi->frame_filters_initialized = 1;
  av1_copy_rst_frame_filters(&cm->cur_frame->rst_info[plane], rsi);
}
#endif  // CONFIG_LR_FRAMEFILTERS_IN_HEADER

static void read_wienerns_filter(MACROBLOCKD *xd, int is_uv,
                                 const RestorationInfo *rsi,
                                 WienerNonsepInfo *wienerns_info,
                                 WienerNonsepInfoBank *bank, aom_reader *rb) {
  int skip_filter_read_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  int ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  const int num_classes = wienerns_info->num_classes;
  assert(num_classes <= WIENERNS_MAX_CLASSES);
  if (rsi->frame_filters_on) return;
  for (int c_id = 0; c_id < num_classes; ++c_id) {
#if CONFIG_MERGE_PARA_CTX
    const int exact_match = aom_read_bit(rb, ACCT_INFO("exact_match"));
#else
    const int exact_match = aom_read_symbol(rb, xd->tile_ctx->merged_param_cdf,
                                            2, ACCT_INFO("exact_match"));
#endif  // CONFIG_MERGE_PARA_CTX
    int ref;
    for (ref = 0; ref < bank->bank_size_for_class[c_id] - 1; ++ref) {
      if (aom_read_literal(rb, 1, ACCT_INFO("bank"))) break;
    }
    wienerns_info->bank_ref_for_class[c_id] = ref;
    skip_filter_read_for_class[c_id] = exact_match;
    ref_for_class[c_id] = ref;
  }
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(xd->current_base_qindex, is_uv);
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    if (skip_filter_read_for_class[c_id]) {
      copy_nsfilter_taps_for_class(
          wienerns_info,
          av1_constref_from_wienerns_bank(bank, ref_for_class[c_id], c_id),
          c_id);
      if (bank->bank_size_for_class[c_id] == 0)
        av1_add_to_wienerns_bank(bank, wienerns_info, c_id);
      continue;
    }
    const int ref = ref_for_class[c_id];

    const WienerNonsepInfo *ref_wienerns_info =
        av1_constref_from_wienerns_bank(bank, ref, c_id);
    assert(ref_wienerns_info->num_classes == num_classes);
    int16_t *wienerns_info_nsfilter = nsfilter_taps(wienerns_info, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);

    memset(wienerns_info_nsfilter, 0,
           nsfilter_params->ncoeffs * sizeof(wienerns_info_nsfilter[0]));

    const int beg_feat = 0;
    int end_feat = nsfilter_params->ncoeffs;
    int ncoeffs1, ncoeffs2;
    int ncoeffs =
        config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
    assert(nsfilter_params->ncoeffs == ncoeffs);
    (void)ncoeffs;
    int s = 0;
    for (int i = 0; i < nsfilter_params->nsubsets - 1; ++i) {
      const int filter_length_bit =
          aom_read_symbol(rb, xd->tile_ctx->wienerns_length_cdf[is_uv], 2,
                          ACCT_INFO("wienerns_length"));
      s += filter_length_bit;
      if (!filter_length_bit) break;
    }
    assert((end_feat & 1) == 0);

    int sym = 1;
    if (!skip_sym_bit(nsfilter_params, s)) {
      assert(is_uv);
      sym = aom_read_symbol(rb, xd->tile_ctx->wienerns_uv_sym_cdf, 2,
                            ACCT_INFO("wienerns_uv_sym"));
    }

    for (int i = beg_feat; i < end_feat; ++i) {
      if (!nsfilter_params->subset_config[s][i]) continue;
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
      const int is_asym_coeff =
          (i < nsfilter_params->nsfilter_config.asymmetric ||
           (i >= ncoeffs1 &&
            i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      if (sym && is_asym_coeff) {
        // Fill in symmetrical tap without reading it
        wienerns_info_nsfilter[i + 1] = wienerns_info_nsfilter[i];
        i++;
      }
    }
    av1_add_to_wienerns_bank(bank, wienerns_info, c_id);
  }
}

static AOM_INLINE void loop_restoration_read_sb_coeffs(AV1_COMMON *cm,
                                                       MACROBLOCKD *xd,
                                                       aom_reader *const r,
                                                       int plane,
                                                       int runit_idx) {
  RestorationInfo *rsi = &cm->rst_info[plane];
  RestorationUnitInfo *rui = &rsi->unit_info[runit_idx];
  assert(rsi->frame_restoration_type != RESTORE_NONE);

  assert(!cm->features.all_lossless);

  rui->wienerns_info.num_classes = rsi->num_filter_classes;

  if (rsi->frame_restoration_type == RESTORE_SWITCHABLE) {
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
    switch (rui->restoration_type) {
      case RESTORE_WIENER_NONSEP:
        read_wienerns_filter(xd, plane != AOM_PLANE_Y, rsi, &rui->wienerns_info,
                             &xd->wienerns_info[plane], r);
        break;
      case RESTORE_PC_WIENER:
        // No side-information for now.
        break;
      default: assert(rui->restoration_type == RESTORE_NONE); break;
    }
  } else if (rsi->frame_restoration_type == RESTORE_WIENER_NONSEP) {
    if (aom_read_symbol(r, xd->tile_ctx->wienerns_restore_cdf, 2,
                        ACCT_INFO("wienerns_restore_cdf"))) {
      rui->restoration_type = RESTORE_WIENER_NONSEP;
      read_wienerns_filter(xd, plane != AOM_PLANE_Y, rsi, &rui->wienerns_info,
                           &xd->wienerns_info[plane], r);
    } else {
      rui->restoration_type = RESTORE_NONE;
    }
  } else if (rsi->frame_restoration_type == RESTORE_PC_WIENER) {
    if (aom_read_symbol(r, xd->tile_ctx->pc_wiener_restore_cdf, 2,
                        ACCT_INFO("pc_wiener_restore_cdf"))) {
      rui->restoration_type = RESTORE_PC_WIENER;
      // No side-information for now.
    } else {
      rui->restoration_type = RESTORE_NONE;
    }
  }

  assert(((cm->features.lr_tools_disable_mask[plane] >> rui->restoration_type) &
          1) == 0);
}

static AOM_INLINE void setup_loopfilter(AV1_COMMON *cm,
                                        struct aom_read_bit_buffer *rb) {
  const int num_planes = av1_num_planes(cm);
  struct loopfilter *lf = &cm->lf;
  if (cm->features.coded_lossless) {
    // write default deltas to frame buffer
    av1_set_default_ref_deltas(cm->cur_frame->ref_deltas);
    av1_set_default_mode_deltas(cm->cur_frame->mode_deltas);
    return;
  }
  assert(!cm->features.coded_lossless);
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame)
    return;
#else
  if (cm->bru.frame_inactive_flag) return;
#endif  // CONFIG_CWG_F317
  if (cm->prev_frame) {
    // write deltas to frame buffer
    memcpy(lf->ref_deltas, cm->prev_frame->ref_deltas, SINGLE_REF_FRAMES);
    memcpy(lf->mode_deltas, cm->prev_frame->mode_deltas, MAX_MODE_LF_DELTAS);
  } else {
    av1_set_default_ref_deltas(lf->ref_deltas);
    av1_set_default_mode_deltas(lf->mode_deltas);
  }
#if CONFIG_MULTI_FRAME_HEADER
  assert(cm->mfh_valid[cm->cur_mfh_id]);
  if (cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_update_flag)
    lf->filter_level[0] =
        cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_level[0];
  else
#endif  // CONFIG_MULTI_FRAME_HEADER
    lf->filter_level[0] = aom_rb_read_bit(rb);
#if DF_DUAL
#if CONFIG_MULTI_FRAME_HEADER
  if (cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_update_flag)
    lf->filter_level[1] =
        cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_level[1];
  else
#endif  // CONFIG_MULTI_FRAME_HEADER
    lf->filter_level[1] = aom_rb_read_bit(rb);
#else
  lf->filter_level[1] = lf->filter_level[0];
#endif  // DF_DUAL
  if (num_planes > 1) {
    if (lf->filter_level[0] || lf->filter_level[1]) {
#if CONFIG_MULTI_FRAME_HEADER
      if (cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_update_flag) {
        lf->filter_level_u =
            cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_level[2];
        lf->filter_level_v =
            cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_level[3];
      } else {
#endif  // CONFIG_MULTI_FRAME_HEADER
        lf->filter_level_u = aom_rb_read_bit(rb);
        lf->filter_level_v = aom_rb_read_bit(rb);
#if CONFIG_MULTI_FRAME_HEADER
      }
#endif  // CONFIG_MULTI_FRAME_HEADER
    } else {
      lf->filter_level_u = lf->filter_level_v = 0;
    }
  }
#if CONFIG_ASYM_DF
  else {
    lf->filter_level_u = lf->filter_level_v = 0;
  }
#endif  // CONFIG_ASYM_DF

  const uint8_t df_par_bits = cm->seq_params.df_par_bits_minus2 + 2;
  const uint8_t df_par_offset = 1 << (df_par_bits - 1);

#if DF_DUAL
  if (lf->filter_level[0]) {
    int luma_delta_q = aom_rb_read_bit(rb);
    if (luma_delta_q) {
      lf->delta_q_luma[0] =
          aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
    } else {
      lf->delta_q_luma[0] = 0;
    }
#if DF_TWO_PARAM
    int luma_delta_side = aom_rb_read_bit(rb);
    if (luma_delta_side) {
      lf->delta_side_luma[0] =
          aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
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
          aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
    } else {
      lf->delta_q_luma[1] = lf->delta_q_luma[0];
    }
#if DF_TWO_PARAM
    int luma_delta_side = aom_rb_read_bit(rb);
    if (luma_delta_side) {
      lf->delta_side_luma[1] =
          aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
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
      lf->delta_q_luma = aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
    } else {
      lf->delta_q_luma = 0;
    }
#if DF_TWO_PARAM
    int luma_delta_side = aom_rb_read_bit(rb);
    if (luma_delta_side) {
      lf->delta_side_luma =
          aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
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
      lf->delta_q_u = aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
    } else {
      lf->delta_q_u = 0;
    }
#if DF_TWO_PARAM
    int u_delta_side = aom_rb_read_bit(rb);
    if (u_delta_side) {
      lf->delta_side_u = aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
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
      lf->delta_q_v = aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
    } else {
      lf->delta_q_v = 0;
    }
#if DF_TWO_PARAM
    int v_delta_side = aom_rb_read_bit(rb);
    if (v_delta_side) {
      lf->delta_side_v = aom_rb_read_literal(rb, df_par_bits) - df_par_offset;
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

static AOM_INLINE void setup_gdf(AV1_COMMON *cm,
                                 struct aom_read_bit_buffer *rb) {
  cm->gdf_info.gdf_mode = 0;
  if (!is_allow_gdf(cm)) return;
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
  if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
    return;
  }
  init_gdf(cm);
#if CONFIG_CWG_F362
  if (cm->seq_params.single_picture_hdr_flag) {
    cm->gdf_info.gdf_mode = 1;
  } else {
    cm->gdf_info.gdf_mode = aom_rb_read_bit(rb);
  }
#else
  cm->gdf_info.gdf_mode = aom_rb_read_bit(rb);
#endif  // CONFIG_CWG_F362
  if (cm->gdf_info.gdf_mode > 0) {
    alloc_gdf_buffers(&cm->gdf_info);
    if (cm->gdf_info.gdf_block_num > 1) {
      cm->gdf_info.gdf_mode += aom_rb_read_bit(rb);
    }
    cm->gdf_info.gdf_pic_qp_idx = aom_rb_read_literal(rb, GDF_RDO_QP_NUM_LOG2);
    cm->gdf_info.gdf_pic_scale_idx =
        aom_rb_read_literal(rb, GDF_RDO_SCALE_NUM_LOG2);
  }
}

static AOM_INLINE void setup_cdef(AV1_COMMON *cm,
                                  struct aom_read_bit_buffer *rb) {
  const int num_planes = av1_num_planes(cm);
  CdefInfo *const cdef_info = &cm->cdef_info;
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    cdef_info->cdef_frame_enable = 0;
    return;
  }
#endif  // CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag) return;
#if CONFIG_CWG_F362
  if (cm->seq_params.single_picture_hdr_flag) {
    cdef_info->cdef_frame_enable = 1;
  } else {
    cdef_info->cdef_frame_enable = aom_rb_read_bit(rb);
  }
#else
  cdef_info->cdef_frame_enable = aom_rb_read_bit(rb);
#endif  // CONFIG_CWG_F362
  if (!cdef_info->cdef_frame_enable) {
    cdef_info->cdef_on_skip_txfm_frame_enable = 0;
    return;
  }
  cdef_info->cdef_damping = aom_rb_read_literal(rb, 2) + 3;
  cdef_info->nb_cdef_strengths = aom_rb_read_literal(rb, 3) + 1;
  if (cm->seq_params.enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_ADAPTIVE) {
    cdef_info->cdef_on_skip_txfm_frame_enable = aom_rb_read_bit(rb);
  } else if (cm->seq_params.enable_cdef_on_skip_txfm ==
             CDEF_ON_SKIP_TXFM_ALWAYS_ON) {
    cdef_info->cdef_on_skip_txfm_frame_enable = 1;
  } else {
    cdef_info->cdef_on_skip_txfm_frame_enable = 0;
  }
  for (int i = 0; i < cdef_info->nb_cdef_strengths; i++) {
    int less_4 = aom_rb_read_bit(rb);
    if (less_4) {
      cdef_info->cdef_strengths[i] = aom_rb_read_literal(rb, 2);
    } else {
      cdef_info->cdef_strengths[i] =
          aom_rb_read_literal(rb, CDEF_STRENGTH_BITS);
    }

    if (num_planes > 1) {
      less_4 = aom_rb_read_bit(rb);
      if (less_4) {
        cdef_info->cdef_uv_strengths[i] = aom_rb_read_literal(rb, 2);
      } else {
        cdef_info->cdef_uv_strengths[i] =
            aom_rb_read_literal(rb, CDEF_STRENGTH_BITS);
      }
    } else {
      cdef_info->cdef_uv_strengths[i] = 0;
    }
  }
}

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
static AOM_INLINE void setup_ccso(AV1_COMMON *cm,
                                  struct aom_read_bit_buffer *rb) {
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
  if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
    cm->cur_frame->ccso_info.ccso_enable[0] = 0;
    cm->cur_frame->ccso_info.ccso_enable[1] = 0;
    cm->cur_frame->ccso_info.ccso_enable[2] = 0;
    return;
  }
  const int ccso_offset[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
  const int ccso_scale[4] = { 1, 2, 3, 4 };
  const int num_ref_frames = (frame_is_intra_only(cm) ||
#if CONFIG_F322_OBUER_ERM
                              frame_is_sframe(cm)
#else
                              cm->features.error_resilient_mode
#endif  // CONFIG_F322_OBUER_ERM
                                  )
                                 ? 0
                                 : cm->ref_frames_info.num_total_refs;
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    cm->ccso_info.ccso_frame_flag = 0;
  } else {
#endif  // CONFIG_CWG_F317
#if CONFIG_CWG_F362
    if (cm->seq_params.single_picture_hdr_flag) {
      cm->ccso_info.ccso_frame_flag = 1;
    } else {
      cm->ccso_info.ccso_frame_flag = aom_rb_read_bit(rb);
    }
#else
  cm->ccso_info.ccso_frame_flag = aom_rb_read_bit(rb);
#endif  // CONFIG_CWG_F362
#if CONFIG_CWG_F317
  }
#endif  // CONFIG_CWG_F317
  if (cm->ccso_info.ccso_frame_flag) {
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    const int ccso_blk_size = get_ccso_unit_size_log2_adaptive_tile(
        cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
    cm->ccso_info.ccso_blk_size = ccso_blk_size;
    cm->cur_frame->ccso_info.ccso_blk_size = ccso_blk_size;
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    for (int plane = 0; plane < av1_num_planes(cm); plane++) {
      CcsoInfo *ref_frame_ccso_info = NULL;
      cm->cur_frame->ccso_info.subsampling_y[plane] =
          plane ? cm->seq_params.subsampling_y : 0;
      cm->cur_frame->ccso_info.subsampling_x[plane] =
          plane ? cm->seq_params.subsampling_x : 0;
      cm->ccso_info.ccso_enable[plane] = aom_rb_read_bit(rb);
      if (cm->ccso_info.ccso_enable[plane]) {
        cm->cur_frame->ccso_info.ccso_enable[plane] = 1;
        if (!frame_is_intra_only(cm) &&
#if CONFIG_F322_OBUER_ERM
            !frame_is_sframe(cm)
#else
            !cm->features.error_resilient_mode
#endif
        ) {
          cm->ccso_info.reuse_ccso[plane] = aom_rb_read_bit(rb);
          cm->ccso_info.sb_reuse_ccso[plane] = aom_rb_read_bit(rb);
        } else {
          cm->ccso_info.reuse_ccso[plane] = 0;
          cm->ccso_info.sb_reuse_ccso[plane] = 0;
        }

        if (cm->ccso_info.reuse_ccso[plane] ||
            cm->ccso_info.sb_reuse_ccso[plane]) {
          if (num_ref_frames > 1) {
            cm->ccso_info.ccso_ref_idx[plane] =
                aom_rb_read_literal(rb, aom_ceil_log2(num_ref_frames));
          } else {
            cm->ccso_info.ccso_ref_idx[plane] = 0;
          }
          if (cm->ccso_info.ccso_ref_idx[plane] >=
              cm->ref_frames_info.num_total_refs) {
            aom_internal_error(
                &cm->error, AOM_CODEC_ERROR,
                "Invalid ccso_ref_idx: ccso_ref_idx >= num_total_refs");
          }
          ref_frame_ccso_info =
              &get_ref_frame_buf(cm, cm->ccso_info.ccso_ref_idx[plane])
                   ->ccso_info;
          if (!ref_frame_ccso_info->ccso_enable[plane]) {
            aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                               "Invalid ccso_ref_idx: ref frame ccso disabled");
          }
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
          if (cm->ccso_info.sb_reuse_ccso[plane] &&
              (cm->ccso_info.ccso_blk_size !=
                   ref_frame_ccso_info->ccso_blk_size ||
               cm->ccso_info.ccso_blk_size != CCSO_BLK_SIZE)) {
            aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                               "Invalid ccso_reuse: ccso_blk_size mismatch");
          }
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
        }

        if (cm->ccso_info.sb_reuse_ccso[plane] &&
            ((cm->mi_params.mi_rows !=
              get_ref_frame_buf(cm, cm->ccso_info.ccso_ref_idx[plane])
                  ->mi_rows) ||
             (cm->mi_params.mi_cols !=
              get_ref_frame_buf(cm, cm->ccso_info.ccso_ref_idx[plane])
                  ->mi_cols) ||
             (plane && ((cm->seq_params.subsampling_y !=
                         ref_frame_ccso_info->subsampling_y[plane]) ||
                        (cm->seq_params.subsampling_x !=
                         ref_frame_ccso_info->subsampling_x[plane]))))) {
          aom_internal_error(&cm->error, AOM_CODEC_ERROR, "Invalid ccso_reuse");
        }

        if (!cm->ccso_info.reuse_ccso[plane]) {
          cm->ccso_info.ccso_bo_only[plane] = aom_rb_read_bit(rb);
          cm->ccso_info.scale_idx[plane] = aom_rb_read_literal(rb, 2);
          if (cm->ccso_info.ccso_bo_only[plane]) {
            cm->ccso_info.quant_idx[plane] = 0;
            cm->ccso_info.ext_filter_support[plane] = 0;
            cm->ccso_info.edge_clf[plane] = 0;
            cm->ccso_info.max_band_log2[plane] = aom_rb_read_literal(rb, 3);
            if (cm->ccso_info.max_band_log2[plane] >
                compute_log2(CCSO_BAND_NUM)) {
              aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                                 "Invalid CCSO Band number");
            }
          } else {
            cm->ccso_info.quant_idx[plane] = aom_rb_read_literal(rb, 2);
            cm->ccso_info.ext_filter_support[plane] =
                aom_rb_read_literal(rb, 3);
            if (cm->ccso_info.ext_filter_support[plane] == 7) {
              aom_internal_error(
                  &cm->error, AOM_CODEC_CORRUPT_FRAME,
                  "Invalid ccso_ext_filter: ccso_ext_filter == 7");
            }
#if CONFIG_CCSO_CLEANUP
            if (quant_sz[cm->ccso_info.scale_idx[plane]]
                        [cm->ccso_info.quant_idx[plane]]) {
              cm->ccso_info.edge_clf[plane] = aom_rb_read_bit(rb);
            } else {
              cm->ccso_info.edge_clf[plane] = 0;
            }
#else
            cm->ccso_info.edge_clf[plane] = aom_rb_read_bit(rb);
#endif  // CONFIG_CCSO_CLEANUP
            cm->ccso_info.max_band_log2[plane] = aom_rb_read_literal(rb, 2);
          }
          const int max_band = 1 << cm->ccso_info.max_band_log2[plane];
          const int edge_clf = cm->ccso_info.edge_clf[plane];
          const int max_edge_interval = edge_clf_to_edge_interval[edge_clf];
          const int num_edge_offset_intervals =
              cm->ccso_info.ccso_bo_only[plane] ? 1 : max_edge_interval;
          for (int d0 = 0; d0 < num_edge_offset_intervals; d0++) {
            for (int d1 = 0; d1 < num_edge_offset_intervals; d1++) {
              for (int band_num = 0; band_num < max_band; band_num++) {
                const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
                const int offset_idx = read_ccso_offset_idx(rb);
                cm->ccso_info.filter_offset[plane][lut_idx_ext] =
                    ccso_offset[offset_idx] *
                    ccso_scale[cm->ccso_info.scale_idx[plane]];
              }
            }
          }
          av1_copy_ccso_filters(&cm->cur_frame->ccso_info, &cm->ccso_info,
                                plane, 1, 0, 0);
          cm->cur_frame->ccso_info.ccso_enable[plane] = 1;

        } else {  // frame level ccso reuse is true
          av1_copy_ccso_filters(&cm->cur_frame->ccso_info, ref_frame_ccso_info,
                                plane, 1, 0, 0);
          av1_copy_ccso_filters(&cm->ccso_info, ref_frame_ccso_info, plane, 1,
                                0, 0);
        }
      } else {  // disable ccso
        cm->cur_frame->ccso_info.ccso_enable[plane] = 0;
      }
    }
  } else {
    cm->cur_frame->ccso_info.ccso_enable[0] = 0;
    cm->cur_frame->ccso_info.ccso_enable[1] = 0;
    cm->cur_frame->ccso_info.ccso_enable[2] = 0;
    cm->ccso_info.ccso_enable[0] = 0;
    cm->ccso_info.ccso_enable[1] = 0;
    cm->ccso_info.ccso_enable[2] = 0;
  }
}

static INLINE int read_delta_q(struct aom_read_bit_buffer *rb) {
  return aom_rb_read_bit(rb) ? aom_rb_read_inv_signed_literal(rb, 6) : 0;
}

static AOM_INLINE void setup_quantization(CommonQuantParams *quant_params,
                                          int num_planes,
                                          const SequenceHeader *seq_params,
                                          struct aom_read_bit_buffer *rb) {
  aom_bit_depth_t bit_depth = seq_params->bit_depth;
  bool separate_uv_delta_q = seq_params->separate_uv_delta_q;
  quant_params->base_qindex = aom_rb_read_literal(
      rb, bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT : QINDEX_BITS);
  if (seq_params->y_dc_delta_q_enabled)
    quant_params->y_dc_delta_q = read_delta_q(rb);
  else
    quant_params->y_dc_delta_q = 0;
  if (num_planes > 1 && (seq_params->uv_dc_delta_q_enabled ||
                         seq_params->uv_ac_delta_q_enabled)) {
    int diff_uv_delta = 0;
    if (separate_uv_delta_q) diff_uv_delta = aom_rb_read_bit(rb);
    if (!seq_params->equal_ac_dc_q) {
      if (seq_params->uv_dc_delta_q_enabled)
        quant_params->u_dc_delta_q = read_delta_q(rb);
      else
        quant_params->u_dc_delta_q = 0;
    }
    if (seq_params->uv_ac_delta_q_enabled)
      quant_params->u_ac_delta_q = read_delta_q(rb);
    else
      quant_params->u_ac_delta_q = 0;
    if (seq_params->equal_ac_dc_q)
      quant_params->u_dc_delta_q = quant_params->u_ac_delta_q;
    if (diff_uv_delta) {
      if (!seq_params->equal_ac_dc_q) {
        if (seq_params->uv_dc_delta_q_enabled)
          quant_params->v_dc_delta_q = read_delta_q(rb);
        else
          quant_params->v_dc_delta_q = 0;
      }
      if (seq_params->uv_ac_delta_q_enabled)
        quant_params->v_ac_delta_q = read_delta_q(rb);
      else
        quant_params->v_ac_delta_q = 0;
      if (seq_params->equal_ac_dc_q)
        quant_params->v_dc_delta_q = quant_params->v_ac_delta_q;
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
}

static AOM_INLINE void setup_qm_params(SequenceHeader *seq_params,
                                       CommonQuantParams *quant_params,
                                       bool segmentation_enabled,
                                       int num_planes,
                                       struct aom_read_bit_buffer *rb) {
  quant_params->using_qmatrix = aom_rb_read_bit(rb);
  if (quant_params->using_qmatrix) {
    if (!quant_params->qmatrix_allocated) {
      seq_params->quantizer_matrix_8x8 = av1_alloc_qm(8, 8);
      seq_params->quantizer_matrix_8x4 = av1_alloc_qm(8, 4);
      seq_params->quantizer_matrix_4x8 = av1_alloc_qm(4, 8);
      quant_params->qmatrix_allocated = true;
    }
    if (!quant_params->qmatrix_initialized) {
      if (!seq_params->user_defined_qmatrix) {
        av1_init_qmatrix(seq_params->quantizer_matrix_8x8,
                         seq_params->quantizer_matrix_8x4,
                         seq_params->quantizer_matrix_4x8, num_planes);
      }
      qm_val_t ***fund_mat[3] = { seq_params->quantizer_matrix_8x8,
                                  seq_params->quantizer_matrix_8x4,
                                  seq_params->quantizer_matrix_4x8 };
      av1_qm_init_dequant_only(quant_params, num_planes, fund_mat);
      quant_params->qmatrix_initialized = true;
    }
  }
#if CONFIG_QM_DEBUG
  printf("[DEC-FRM] using_qmatrix: %d\n", quant_params->using_qmatrix);
#endif
  if (quant_params->using_qmatrix) {
    if (segmentation_enabled) {
      quant_params->pic_qm_num = aom_rb_read_literal(rb, 2) + 1;
    } else {
      quant_params->pic_qm_num = 1;
    }
#if CONFIG_QM_DEBUG
    printf("[DEC-FRM] pic_qm_num: %d\n", quant_params->pic_qm_num);
#endif
    quant_params->qm_index_bits = aom_ceil_log2(quant_params->pic_qm_num);
    for (uint8_t i = 0; i < quant_params->pic_qm_num; i++) {
      quant_params->qm_y[i] = aom_rb_read_literal(rb, QM_LEVEL_BITS);
      if (num_planes > 1) {
        const bool qm_uv_same_as_y = aom_rb_read_bit(rb);
#if CONFIG_QM_DEBUG
        printf("[DEC-FRM] qm_uv_same_as_y: %d\n", qm_uv_same_as_y);
#endif
        if (qm_uv_same_as_y) {
          quant_params->qm_u[i] = quant_params->qm_y[i];
          quant_params->qm_v[i] = quant_params->qm_y[i];
        } else {
          quant_params->qm_u[i] = aom_rb_read_literal(rb, QM_LEVEL_BITS);
          if (!seq_params->separate_uv_delta_q) {
            quant_params->qm_v[i] = quant_params->qm_u[i];
          } else {
            quant_params->qm_v[i] = aom_rb_read_literal(rb, QM_LEVEL_BITS);
          }
        }
      }
#if CONFIG_QM_DEBUG
      if (num_planes > 1) {
        printf("[DEC-FRM] qm_y/u/v[%d]: (%d,%d,%d)\n", i, quant_params->qm_y[i],
               quant_params->qm_u[i], quant_params->qm_v[i]);
      } else {
        printf("[DEC-FRM] qm_y[%d]: (%d)\n", i, quant_params->qm_y[i]);
      }
#endif
    }
  } else {
    for (uint8_t i = 0; i < 4; i++) {
      quant_params->qm_y[i] = 0;
      quant_params->qm_u[i] = 0;
      quant_params->qm_v[i] = 0;
    }
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
    quant_params->y_dequant_QTX[i][1] =
        av1_ac_quant_QTX(qindex, 0, 0, bit_depth);
    quant_params->u_dequant_QTX[i][0] =
        av1_dc_quant_QTX(qindex, quant_params->u_dc_delta_q,
                         cm->seq_params.base_uv_dc_delta_q, bit_depth);
    quant_params->u_dequant_QTX[i][1] =
        av1_ac_quant_QTX(qindex, quant_params->u_ac_delta_q,
                         cm->seq_params.base_uv_ac_delta_q, bit_depth);
    quant_params->v_dequant_QTX[i][0] =
        av1_dc_quant_QTX(qindex, quant_params->v_dc_delta_q,
                         cm->seq_params.base_uv_dc_delta_q, bit_depth);
    quant_params->v_dequant_QTX[i][1] =
        av1_ac_quant_QTX(qindex, quant_params->v_ac_delta_q,
                         cm->seq_params.base_uv_ac_delta_q, bit_depth);
    const int use_qmatrix = av1_use_qmatrix(quant_params, xd, i);
    // NB: depends on base index so there is only 1 set per frame
    // No quant weighting when lossless or signalled not using QM
    const int qm_index = quant_params->qm_index[i];
    const int qmlevel_y =
        use_qmatrix ? quant_params->qm_y[qm_index] : NUM_QM_LEVELS - 1;
    for (int j = 0; j < TX_SIZES_ALL; ++j) {
      quant_params->y_iqmatrix[i][j] =
          av1_iqmatrix(quant_params, qmlevel_y, AOM_PLANE_Y, j);
    }
    const int num_planes = av1_num_planes(cm);
    if (num_planes > 1) {
      const int qmlevel_u =
          use_qmatrix ? quant_params->qm_u[qm_index] : NUM_QM_LEVELS - 1;
      for (int j = 0; j < TX_SIZES_ALL; ++j) {
        quant_params->u_iqmatrix[i][j] =
            av1_iqmatrix(quant_params, qmlevel_u, AOM_PLANE_U, j);
      }
      const int qmlevel_v =
          use_qmatrix ? quant_params->qm_v[qm_index] : NUM_QM_LEVELS - 1;
      for (int j = 0; j < TX_SIZES_ALL; ++j) {
        quant_params->v_iqmatrix[i][j] =
            av1_iqmatrix(quant_params, qmlevel_v, AOM_PLANE_V, j);
      }
#if CONFIG_QM_DEBUG
      printf("[DEC-FRM] qmlevel_y/u/v[%d]: (%d,%d,%d)\n", i, qmlevel_y,
             qmlevel_u, qmlevel_v);
#endif
    } else {
#if CONFIG_QM_DEBUG
      printf("[DEC-FRM] qmlevel_y[%d]: (%d)\n", i, qmlevel_y);
#endif
    }
  }
}

static InterpFilter read_frame_interp_filter(struct aom_read_bit_buffer *rb) {
  return aom_rb_read_bit(rb) ? SWITCHABLE
                             : aom_rb_read_literal(rb, LOG_SWITCHABLE_FILTERS);
}

static AOM_INLINE void setup_render_size(AV1_COMMON *cm,
                                         struct aom_read_bit_buffer *rb) {
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    return;
  }
#endif  // CONFIG_CWG_F317
#if !CONFIG_CWG_F248_RENDER_SIZE
#if CONFIG_MULTI_FRAME_HEADER
  assert(cm->mfh_valid[cm->cur_mfh_id]);
#if CONFIG_CWG_E242_PARSING_INDEP
  if (cm->mfh_params[cm->cur_mfh_id].mfh_render_size_present_flag) {
    cm->render_width = cm->mfh_params[cm->cur_mfh_id].mfh_render_width;
    cm->render_height = cm->mfh_params[cm->cur_mfh_id].mfh_render_height;
  } else {
    cm->render_width = cm->width;
    cm->render_height = cm->height;
  }
#else
  cm->render_width = cm->mfh_params[cm->cur_mfh_id].mfh_render_width;
  cm->render_height = cm->mfh_params[cm->cur_mfh_id].mfh_render_height;
#endif  // CONFIG_CWG_E242_PARSING_INDEP
#else   // CONFIG_MULTI_FRAME_HEADER
  cm->render_width = cm->width;
  cm->render_height = cm->height;
#endif  // CONFIG_MULTI_FRAME_HEADER
#endif  // !CONFIG_CWG_F248_RENDER_SIZE

#if CONFIG_CWG_F248_RENDER_SIZE
  (void)rb;
#if CONFIG_MULTILAYER_HLS && CONFIG_MULTILAYER_HLS_ENABLE_SIGNALING
  // Note: if Local LCR information is used, then the layer_id =
  // lcr_params.xlayer_id If Global LCR is used, then for each extended layer
  // i.e, xlayer_info(1,n) is specified, where n is xlayer_id[i] of the i-th
  // extended layer. Set default to use xlayer_id 31 when Global LCR is being
  // used
  const bool is_global_lcr = !cm->lcr_params.is_local_lcr;
  const int layer_id =
      is_global_lcr ? GLOBAL_LCR_XLAYER_ID : cm->lcr_params.xlayer_id;
  const int xlayer_id = cm->lcr_params.lcr_xLayer_id[layerId];
  if (cm->lcr_params.lcr_rep_info_present_flag[is_global_lcr][xlayer_id]) {
    cm->render_width = cm->lcr_params.rep_params.lcr_max_pic_width;
    cm->render_height = cm->lcr_params.rep_params.lcr_max_pic_height;
  } else {
#endif  // CONFIG_MULTILAYER_HLS && CONFIG_MULTILAYER_HLS_ENABLE_SIGNALING
    cm->render_width = cm->width;
    cm->render_height = cm->height;
#if CONFIG_MULTILAYER_HLS && CONFIG_MULTILAYER_HLS_ENABLE_SIGNALING
  }
#endif  // CONFIG_MULTILAYER_HLS && CONFIG_MULTILAYER_HLS_ENABLE_SIGNALING
#else
  if (aom_rb_read_bit(rb))
    av1_read_frame_size(rb, 16, 16, &cm->render_width, &cm->render_height);
#endif  // CONFIG_CWG_F248_RENDER_SIZE
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

  // We also need to handle the case when `sb_rows` / `sb_cols` has
  // increased due to a change in superblock size. Note that total number of
  // superblocks can *increase* even if frame size has decreased, if
  // superblock size is reduced.
  // We call this function unconditionally, because it already has the logic
  // to reallocte only when necessary.
  if (av1_alloc_superblock_info_buffers(cm)) {
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate superblock info buffers");
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
          cm->features.byte_alignment, NULL, NULL, NULL, false)) {
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

  tip_frame_buf = &cm->tip_ref.tmp_tip_frame->buf;
  if (aom_realloc_frame_buffer(
          tip_frame_buf, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, AOM_DEC_BORDER_IN_PIXELS,
          cm->features.byte_alignment, NULL, NULL, NULL, false)) {
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
}

static AOM_INLINE void setup_buffer_pool(AV1_COMMON *cm) {
  BufferPool *const pool = cm->buffer_pool;
  const SequenceHeader *const seq_params = &cm->seq_params;

  lock_buffer_pool(pool);
  if (aom_realloc_frame_buffer(
          &cm->cur_frame->buf, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, AOM_DEC_BORDER_IN_PIXELS,
          cm->features.byte_alignment, &cm->cur_frame->raw_frame_buffer,
          pool->get_fb_cb, pool->cb_priv, false)) {
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
    setup_tip_frame_size(cm);
  }
}

static AOM_INLINE void setup_frame_size(AV1_COMMON *cm,
                                        int frame_size_override_flag,
                                        struct aom_read_bit_buffer *rb) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  int width, height;

#if CONFIG_CWG_F317
  int num_bits_width = seq_params->num_bits_width;
  int num_bits_height = seq_params->num_bits_height;
  if (cm->bridge_frame_info.is_bridge_frame) {
    cm->bridge_frame_info.bridge_frame_max_width =
        aom_rb_read_literal(rb, num_bits_width) + 1;
    cm->bridge_frame_info.bridge_frame_max_height =
        aom_rb_read_literal(rb, num_bits_height) + 1;
    const RefCntBuffer *ref_buf = get_ref_frame_buf(
        cm, cm->bridge_frame_info.bridge_frame_ref_idx_remapped);
    width =
        AOMMIN(cm->bridge_frame_info.bridge_frame_max_width, ref_buf->width);
    height =
        AOMMIN(cm->bridge_frame_info.bridge_frame_max_height, ref_buf->height);
  } else {
#endif  // CONFIG_CWG_F317
    if (frame_size_override_flag) {
#if !CONFIG_CWG_F317
      int num_bits_width = seq_params->num_bits_width;
      int num_bits_height = seq_params->num_bits_height;
#endif  // CONFIG_CWG_F317

      av1_read_frame_size(rb, num_bits_width, num_bits_height, &width, &height);
      if (width > seq_params->max_frame_width ||
          height > seq_params->max_frame_height) {
        aom_internal_error(
            &cm->error, AOM_CODEC_CORRUPT_FRAME,
            "Frame dimensions are larger than the maximum values");
      }
    } else {
#if CONFIG_MULTI_FRAME_HEADER
      assert(cm->mfh_valid[cm->cur_mfh_id]);
#if CONFIG_CWG_E242_PARSING_INDEP
      if (cm->mfh_params[cm->cur_mfh_id].mfh_frame_size_present_flag) {
        width = cm->mfh_params[cm->cur_mfh_id].mfh_frame_width;
        height = cm->mfh_params[cm->cur_mfh_id].mfh_frame_height;
      } else {
        width = seq_params->max_frame_width;
        height = seq_params->max_frame_height;
      }
#else
      width = cm->mfh_params[cm->cur_mfh_id].mfh_frame_width;
      height = cm->mfh_params[cm->cur_mfh_id].mfh_frame_height;
#endif  // CONFIG_CWG_E242_PARSING_INDEP
#else   // CONFIG_MULTI_FRAME_HEADER
    width = seq_params->max_frame_width;
    height = seq_params->max_frame_height;
#endif  // CONFIG_MULTI_FRAME_HEADER
    }
#if CONFIG_CWG_F317
  }
#endif  // CONFIG_CWG_F317

  resize_context_buffers(cm, width, height);
  setup_render_size(cm, rb);
  setup_buffer_pool(cm);
  realloc_bru_info(cm);
}

static AOM_INLINE void setup_seq_sb_size(SequenceHeader *seq_params,
                                         struct aom_read_bit_buffer *rb) {
  static const BLOCK_SIZE sb_sizes[] = { BLOCK_256X256, BLOCK_128X128,
                                         BLOCK_64X64 };
  int index = 0;
  bool bit = aom_rb_read_bit(rb);
  if (!bit) {
    index++;
    bit = aom_rb_read_bit(rb);
    if (!bit) {
      index++;
    }
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
    AV1_COMMON *cm,
#if CONFIG_ACROSS_SCALE_REF_OPT
    const int explicit_ref_frame_map,
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
    struct aom_read_bit_buffer *rb) {
  int width, height;
  int found = 0;
#if CONFIG_ACROSS_SCALE_REF_OPT
  // In implicit reference framework, the reference frame mapping up to this
  // point is a preliminary, resolution independent version, in which case
  // num_total_refs_res_indep and remapped_ref_idx_res_indep are used instead.
  const int num_refs = explicit_ref_frame_map
                           ? cm->ref_frames_info.num_total_refs
                           : cm->ref_frames_info.num_total_refs_res_indep;
#else
  const int num_refs = cm->ref_frames_info.num_total_refs;
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
  for (int i = 0; i < num_refs; ++i) {
    if (aom_rb_read_bit(rb)) {
#if CONFIG_ACROSS_SCALE_REF_OPT
      const RefCntBuffer *const ref_buf =
          explicit_ref_frame_map ? get_ref_frame_buf(cm, i)
                                 : get_ref_frame_buf_res_indep(cm, i);
#else
      const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, i);
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
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
    resize_context_buffers(cm, width, height);
    setup_render_size(cm, rb);
  }

  if (width <= 0 || height <= 0)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid frame size");

#if !CONFIG_ACROSS_SCALE_REF_OPT
  // Check to make sure at least one of frames that this frame references has
  // valid dimensions.
  int has_valid_ref_frame = 0;
  for (int i = 0; i < num_refs; ++i) {
    const RefCntBuffer *const ref_frame = get_ref_frame_buf(cm, i);
    has_valid_ref_frame |=
        valid_ref_frame_size(ref_frame->buf.y_crop_width,
                             ref_frame->buf.y_crop_height, width, height);
  }
  if (!has_valid_ref_frame)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Referenced frame has invalid size");
#endif  // !CONFIG_ACROSS_SCALE_REF_OPT
  for (int i = 0; i < num_refs; ++i) {
#if CONFIG_ACROSS_SCALE_REF_OPT
    const RefCntBuffer *const ref_frame =
        explicit_ref_frame_map ? get_ref_frame_buf(cm, i)
                               : get_ref_frame_buf_res_indep(cm, i);
#else
    const RefCntBuffer *const ref_frame = get_ref_frame_buf(cm, i);
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
    if (!valid_ref_frame_img_fmt(
            ref_frame->buf.bit_depth, ref_frame->buf.subsampling_x,
            ref_frame->buf.subsampling_y, seq_params->bit_depth,
            seq_params->subsampling_x, seq_params->subsampling_y))
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Referenced frame has incompatible color format");
  }
  setup_buffer_pool(cm);
  realloc_bru_info(cm);
}

#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
// Reconstructs the tile information
static void reconstruct_tile_info_max_tile(
    AV1_COMMON *const cm, const TileInfoSyntax *const tile_params) {
  CommonTileParams *const tiles = &cm->tiles;
  const CommonTileParams *const tile_info = &tile_params->tile_info;
  int width_sb = tile_info->sb_cols;
  int height_sb = tile_info->sb_rows;

  tiles->uniform_spacing = tile_info->uniform_spacing;

  // Read tile columns
  if (tiles->uniform_spacing) {
    tiles->log2_cols = tile_info->log2_cols;
  } else {
    int i;
    int start_sb;
    for (i = 0, start_sb = 0; width_sb > 0 && i < MAX_TILE_COLS; i++) {
      const int size_sb =
          tile_info->col_start_sb[i + 1] - tile_info->col_start_sb[i];
      tiles->col_start_sb[i] = start_sb;
      start_sb += size_sb;
      width_sb -= size_sb;
    }
    tiles->cols = i;
    tiles->col_start_sb[i] = start_sb + width_sb;
    assert(width_sb == 0);
  }
  av1_calculate_tile_cols(tiles);
  // Read tile rows
  if (tiles->uniform_spacing) {
    // tiles->log2_rows = tile_params->log2_rows + 1;
    tiles->log2_rows = tile_info->log2_rows;
  } else {
    int i;
    int start_sb;
    for (i = 0, start_sb = 0; height_sb > 0 && i < MAX_TILE_ROWS; i++) {
      const int size_sb =
          tile_info->row_start_sb[i + 1] - tile_info->row_start_sb[i];
      tiles->row_start_sb[i] = start_sb;
      start_sb += size_sb;
      height_sb -= size_sb;
    }
    tiles->rows = i;
    tiles->row_start_sb[i] = start_sb + height_sb;
    assert(height_sb == 0);
  }
  av1_calculate_tile_rows(tiles);
}
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO

static AOM_INLINE void read_tile_info_max_tile(
    AV1_COMMON *const cm, struct aom_read_bit_buffer *const rb) {
  CommonTileParams *const tiles = &cm->tiles;
  int width_sb = tiles->sb_cols;
  int height_sb = tiles->sb_rows;

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
  av1_calculate_tile_cols(tiles);

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
  av1_calculate_tile_rows(tiles);
}

void av1_set_single_tile_decoding_mode(AV1_COMMON *const cm) {
  cm->tiles.single_tile_decoding = 0;
  if (cm->tiles.large_scale) {
    struct loopfilter *lf = &cm->lf;
    RestorationInfo *const rst_info = cm->rst_info;
    const CdefInfo *const cdef_info = &cm->cdef_info;
    const GdfInfo *const gdf_info = &cm->gdf_info;

    // Figure out single_tile_decoding by loopfilter_level.
    const int no_loopfilter = !(lf->filter_level[0] || lf->filter_level[1]);
    const int no_cdef = cdef_info->nb_cdef_strengths == 1 &&
                        cdef_info->cdef_strengths[0] == 0 &&
                        cdef_info->cdef_uv_strengths[0] == 0;
    const int no_restoration =
        rst_info[0].frame_restoration_type == RESTORE_NONE &&
        rst_info[1].frame_restoration_type == RESTORE_NONE &&
        rst_info[2].frame_restoration_type == RESTORE_NONE;
    const int no_gdf = gdf_info->gdf_mode == 0;
    assert(IMPLIES(cm->features.coded_lossless,
                   no_loopfilter && no_cdef && no_gdf));
    assert(IMPLIES(cm->features.all_lossless, no_restoration));
    cm->tiles.single_tile_decoding =
        no_loopfilter && no_cdef && no_restoration && no_gdf;
    ;
  }
}

static AOM_INLINE void read_tile_info(AV1Decoder *const pbi,
                                      struct aom_read_bit_buffer *const rb) {
  AV1_COMMON *const cm = &pbi->common;
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    return;
  }
#endif  // CONFIG_CWG_F317
  av1_get_tile_limits(&cm->tiles, cm->mi_params.mi_rows, cm->mi_params.mi_cols,
                      cm->mib_size_log2, cm->seq_params.mib_size_log2);
#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
  const TileInfoSyntax *const tile_params = find_effective_tile_params(cm);
  int reuse = 0;
#if CONFIG_CWG_F349_SIGNAL_TILE_INFO
  if (tile_params &&
      is_frame_tile_config_reuse_eligible(tile_params, &cm->tiles)) {
    if (tile_params->allow_tile_info_change)
      reuse = aom_rb_read_bit(rb);
    else
      reuse = 1;
  }
#else
  cm->current_frame.tile_info_present_in_frame_header = aom_rb_read_bit(rb);
  reuse = !cm->current_frame.tile_info_present_in_frame_header;
  if (reuse && !tile_params) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "No tile information present");
  }
#endif  // CONFIG_CWG_F349_SIGNAL_TILE_INFO
  if (reuse) {
    reconstruct_tile_info_max_tile(cm, tile_params);
  } else {
    read_tile_info_max_tile(cm, rb);
  }
#else
  read_tile_info_max_tile(cm, rb);
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO
  if (cm->bru.enabled) {
    const int num_tiles = cm->tiles.rows * cm->tiles.cols;
    memset(cm->tiles.tile_active_bitmap, 0, (num_tiles + 7) / 8);
    if (num_tiles == 1) {
      cm->tiles.tile_active_bitmap[0] = 1;
    }
  }
  pbi->context_update_tile_id = 0;
  if (cm->tiles.rows * cm->tiles.cols > 1) {
    if (!cm->seq_params.enable_avg_cdf || !cm->seq_params.avg_cdf_type) {
      // tile to use for cdf update
      pbi->context_update_tile_id =
          aom_rb_read_literal(rb, cm->tiles.log2_rows + cm->tiles.log2_cols);
      if (pbi->context_update_tile_id >= cm->tiles.rows * cm->tiles.cols) {
        aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                           "Invalid context_update_tile_id");
      }
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
  int tile_cols = cm->tiles.cols;
  int tile_rows = cm->tiles.rows;
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
  if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
    tile_cols = 1;
    tile_rows = 1;
    end_tile = 0;
  }
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
      if (cm->bru.enabled) {
        const int tile_idx = r * tile_cols + c;
        TileDataDec *const this_tile = pbi->tile_data + tile_idx;
        const int tile_active_map_byte = tile_idx >> 3;
        const int tile_active_map_bit = tile_idx & 7;
        this_tile->tile_info.tile_active_mode =
            (cm->tiles.tile_active_bitmap[tile_active_map_byte] >>
             tile_active_map_bit) &
            1;
      }
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

  av1_zero(td->dcb.xd.ref_mv_bank);
  av1_zero(td->dcb.xd.warp_param_bank);

  for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
       mi_col += cm->mib_size, sb_col_in_tile++) {
    av1_reset_is_mi_coded_map(&td->dcb.xd, cm->mib_size);
    td->dcb.xd.sbi = av1_get_sb_info(cm, mi_row, mi_col);
    set_cb_buffer(pbi, &td->dcb, pbi->cb_buffer_base, num_planes, mi_row,
                  mi_col);

    sync_read(&tile_data->dec_row_mt_sync, sb_row_in_tile, sb_col_in_tile);

    DecoderCodingBlock *const dcb = &td->dcb;
    MACROBLOCKD *const xd = &dcb->xd;

    av1_reset_refmv_bank(cm, xd, &tile_info, mi_row, mi_col);

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
  td->copy_frame_mvs_block_visit = predict_inter_block_void;
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
    td->copy_frame_mvs_block_visit = copy_frame_mvs_inter_block;
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
  int num_filter_classes[MAX_MB_PLANE];
  for (int p = 0; p < num_planes; ++p)
    num_filter_classes[p] = cm->rst_info[p].num_filter_classes;
  av1_reset_loop_restoration(xd, 0, num_planes, num_filter_classes);
#if CONFIG_CWG_F317
  if (cm->bru.enabled || cm->bridge_frame_info.is_bridge_frame) {
    if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame)
      xd->tile.tile_active_mode = 0;
#else
  if (cm->bru.enabled) {
    if (cm->bru.frame_inactive_flag) xd->tile.tile_active_mode = 0;
#endif  // CONFIG_CWG_F317
  }

  for (int mi_row = tile_info.mi_row_start; mi_row < tile_info.mi_row_end;
       mi_row += cm->mib_size) {
    av1_zero_left_context(xd);
    av1_zero(xd->ref_mv_bank);

    av1_zero(xd->warp_param_bank);
#if !WARP_CU_BANK
    xd->warp_param_bank_pt = &td->warp_param_bank;
#endif  //! WARP_CU_BANK

    for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
         mi_col += cm->mib_size) {
      av1_reset_is_mi_coded_map(xd, cm->mib_size);
      BruActiveMode sb_active_mode = BRU_ACTIVE_SB;
      av1_set_sb_info(cm, xd, mi_row, mi_col, sb_active_mode);
      set_cb_buffer(pbi, dcb, &td->cb_buffer_base, num_planes, 0, 0);
      // td->ref_mv_bank is initialized as xd->ref_mv_bank, and used
      // for MV referencing during decoding the tile.
      // xd->ref_mv_bank is updated as decoding goes.
      av1_reset_refmv_bank(cm, xd, &tile_info, mi_row, mi_col);

      decode_partition_sb(pbi, td, mi_row, mi_col, td->bit_reader, cm->sb_size,
                          0x3);

      if (aom_reader_has_overflowed(td->bit_reader)) {
        aom_merge_corrupted_flag(&dcb->corrupted, 1);
        return;
      }
    }
  }
#if CONFIG_CWG_F317
  int corrupted =
      (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) ? 0
#else
  int corrupted =
      cm->bru.frame_inactive_flag ? 0
#endif  // CONFIG_CWG_F317
      : (check_trailing_bits_after_symbol_coder(td->bit_reader)) ? 1
                                                                 : 0;
  aom_merge_corrupted_flag(&dcb->corrupted, corrupted);
}

#if CONFIG_THROUGHPUT_ANALYSIS
static void aom_accounting_cal_total(AV1Decoder *pbi) {
  if (pbi->decoding_first_frame) {
    pbi->common.sym_stats.frame_dec_order = 0;
    pbi->common.sym_stats.tot_ctx_syms = 0;
    pbi->common.sym_stats.total_total_hits = 0;
    pbi->common.sym_stats.total_context_switch = 0;
    pbi->common.sym_stats.tot_bypass_syms = 0;
    pbi->common.sym_stats.tot_bits = 0;
    pbi->common.sym_stats.peak_ctx_syms = 0;
    pbi->common.sym_stats.peak_bypass_syms = 0;
    pbi->common.sym_stats.peak_bits = 0;
  }
  Accounting accounting = pbi->accounting;
  int64_t frm_ctx_syms = accounting.syms.num_ctx_coded;
  int64_t frm_bypass_syms = accounting.syms.num_bypass_coded;
  int64_t frm_context_switch = accounting.syms.context_switch;
  int64_t frm_total_hits = accounting.syms.total_hits;
  int64_t frm_bits = 0;
  for (int i = 0; i < accounting.syms.num_syms; i++) {
    AccountingSymbol sym = accounting.syms.syms[i];
    frm_bits += sym.bits;
  }
  int64_t peak_ctx_syms = pbi->common.sym_stats.peak_ctx_syms;
  int64_t peak_bypass_syms = pbi->common.sym_stats.peak_bypass_syms;
  pbi->common.sym_stats.tot_ctx_syms += frm_ctx_syms;
  pbi->common.sym_stats.total_context_switch += frm_context_switch;
  pbi->common.sym_stats.total_total_hits += frm_total_hits;
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
  total_context_switch = pbi->common.sym_stats.total_context_switch;
  total_total_hits = pbi->common.sym_stats.total_total_hits;
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
  if (pbi->tile_data == NULL || n_tiles != pbi->allocated_tiles) {
    decoder_alloc_tile_data(pbi, n_tiles);
  }
#if EXT_TILE_DEBUG
  if (tiles->large_scale && !pbi->ext_tile_debug)
    raw_data_end = get_ls_single_tile_buffer(pbi, data, tile_buffers);
  else if (tiles->large_scale && pbi->ext_tile_debug)
    raw_data_end = get_ls_tile_buffers(pbi, data, data_end, tile_buffers);
  else
#endif  // EXT_TILE_DEBUG
    get_tile_buffers(pbi, data, data_end, tile_buffers, start_tile, end_tile);
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

  // Temporary buffers used during the DMVR and OPFL processing.
  td->dcb.xd.opfl_vxy_bufs = td->opfl_vxy_bufs;
  td->dcb.xd.opfl_gxy_bufs = td->opfl_gxy_bufs;
  td->dcb.xd.opfl_dst_bufs = td->opfl_dst_bufs;

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

      td->dcb.xd.tile.tile_active_mode = 1;
      if (cm->bru.enabled && (cm->tiles.cols * cm->tiles.rows > 1)) {
        td->dcb.xd.tile.tile_active_mode =
            tile_data->tile_info.tile_active_mode;
      }
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
  int num_filter_classes[MAX_MB_PLANE];
  for (int p = 0; p < num_planes; ++p)
    num_filter_classes[p] = cm->rst_info[p].num_filter_classes;
  av1_reset_loop_restoration(xd, 0, num_planes, num_filter_classes);

  for (int mi_row = tile_info.mi_row_start; mi_row < tile_info.mi_row_end;
       mi_row += cm->mib_size) {
    av1_zero_left_context(xd);
    av1_zero(xd->ref_mv_bank);

    av1_zero(xd->warp_param_bank);
#if !WARP_CU_BANK
    xd->warp_param_bank_pt = &td->warp_param_bank;
#endif  //! WARP_CU_BANK

    for (int mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
         mi_col += cm->mib_size) {
      av1_reset_is_mi_coded_map(xd, cm->mib_size);
      BruActiveMode sb_active_mode = BRU_ACTIVE_SB;
      av1_set_sb_info(cm, xd, mi_row, mi_col, sb_active_mode);
      set_cb_buffer(pbi, dcb, pbi->cb_buffer_base, num_planes, mi_row, mi_col);
      av1_reset_refmv_bank(cm, xd, &tile_info, mi_row, mi_col);

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
}

// Free-up the temporary buffers created for DMVR and OPFL processing.
void av1_free_opfl_tmp_bufs(ThreadData *thread_data) {
  aom_free(thread_data->opfl_vxy_bufs);
  thread_data->opfl_vxy_bufs = NULL;

  aom_free(thread_data->opfl_gxy_bufs);
  thread_data->opfl_gxy_bufs = NULL;

  aom_free(thread_data->opfl_dst_bufs);
  thread_data->opfl_dst_bufs = NULL;
}

// Allocate memory for temporary buffers used during the DMVR and OPFL
// processing.
static AOM_INLINE void allocate_opfl_tmp_bufs(AV1_COMMON *const cm,
                                              ThreadData *thread_data) {
  CHECK_MEM_ERROR(
      cm, thread_data->opfl_vxy_bufs,
      aom_memalign(32, N_OF_OFFSETS * 4 * sizeof(*thread_data->opfl_vxy_bufs)));

  CHECK_MEM_ERROR(cm, thread_data->opfl_gxy_bufs,
                  aom_memalign(32, MAX_SB_SQUARE * 4 *
                                       sizeof(*thread_data->opfl_gxy_bufs)));

  CHECK_MEM_ERROR(cm, thread_data->opfl_dst_bufs,
                  aom_memalign(32, MAX_SB_SQUARE * 2 *
                                       sizeof(*thread_data->opfl_dst_bufs)));
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
    // Temporary buffers used during the DMVR and OPFL processing.
    thread_data->td->dcb.xd.opfl_vxy_bufs = thread_data->td->opfl_vxy_bufs;
    thread_data->td->dcb.xd.opfl_gxy_bufs = thread_data->td->opfl_gxy_bufs;
    thread_data->td->dcb.xd.opfl_dst_bufs = thread_data->td->opfl_dst_bufs;

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
      av1_free_opfl_tmp_bufs(thread_data->td);

      allocate_mc_tmp_buf(cm, thread_data->td, buf_size);
      allocate_opfl_tmp_bufs(cm, thread_data->td);
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
  if (pbi->tile_data == NULL || n_tiles != pbi->allocated_tiles) {
    decoder_alloc_tile_data(pbi, n_tiles);
  }
  // get tile size in tile group
#if EXT_TILE_DEBUG
  if (tiles->large_scale) assert(pbi->ext_tile_debug == 1);
  if (tiles->large_scale)
    raw_data_end = get_ls_tile_buffers(pbi, data, data_end, tile_buffers);
  else
#endif  // EXT_TILE_DEBUG
    get_tile_buffers(pbi, data, data_end, tile_buffers, start_tile, end_tile);

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

  if (pbi->tile_data == NULL || n_tiles != pbi->allocated_tiles) {
    if (pbi->tile_data != NULL) {
      for (int i = 0; i < pbi->allocated_tiles; i++) {
        TileDataDec *const tile_data = pbi->tile_data + i;
        av1_dec_row_mt_dealloc(&tile_data->dec_row_mt_sync);
      }
    }
    decoder_alloc_tile_data(pbi, n_tiles);
  }

  // get tile size in tile group
#if EXT_TILE_DEBUG
  if (tiles->large_scale) assert(pbi->ext_tile_debug == 1);
  if (tiles->large_scale)
    raw_data_end = get_ls_tile_buffers(pbi, data, data_end, tile_buffers);
  else
#endif  // EXT_TILE_DEBUG
    get_tile_buffers(pbi, data, data_end, tile_buffers, start_tile, end_tile);

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

#if CONFIG_CWG_E242_BITDEPTH
// Gets the bitdepth_lut_idx field in color_config() and returns bit_depth from
// the bitdepth list.
int av1_get_bitdepth_from_index(int bitdepth_lut_idx) {
  static aom_bit_depth_t bitdepth_list[] = { AOM_BITS_10, AOM_BITS_8,
                                             AOM_BITS_12 };
  if (bitdepth_lut_idx >= AOM_NUM_SUPPORTED_BITDEPTH) return -1;
  return bitdepth_list[bitdepth_lut_idx];
}
// Reads the bitdepth in color_config() and sets seq_params->bit_depth
#else
// Reads the high_bitdepth and twelve_bit fields in color_config() and sets
// seq_params->bit_depth based on the values of those fields and
// seq_params->profile.
#endif  // CONFIG_CWG_E242_BITDEPTH
// Reports errors by calling rb->error_handler() or
// aom_internal_error().
static AOM_INLINE void read_bitdepth(
    struct aom_read_bit_buffer *rb, SequenceHeader *seq_params,
    struct aom_internal_error_info *error_info) {
#if CONFIG_CWG_E242_BITDEPTH
  const int bitdepth_lut_idx = aom_rb_read_uvlc(rb);
  const int bitdepth = av1_get_bitdepth_from_index(bitdepth_lut_idx);
  if (bitdepth >= 0) seq_params->bit_depth = bitdepth;
#else
  const int high_bitdepth = aom_rb_read_bit(rb);
  if (seq_params->profile == PROFILE_2 && high_bitdepth) {
    const int twelve_bit = aom_rb_read_bit(rb);
    seq_params->bit_depth = twelve_bit ? AOM_BITS_12 : AOM_BITS_10;
  } else if (seq_params->profile <= PROFILE_2) {
    seq_params->bit_depth = high_bitdepth ? AOM_BITS_10 : AOM_BITS_8;
  }
#endif  // CONFIG_CWG_E242_BITDEPTH
  else {
    aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                       "Unsupported profile/bit-depth combination");
  }
}

void av1_read_film_grain_params(AV1_COMMON *cm,
                                struct aom_read_bit_buffer *rb) {
  aom_film_grain_t *pars = &cm->film_grain_params;
  const SequenceHeader *const seq_params = &cm->seq_params;

#if CONFIG_CWG_F362
  if (cm->seq_params.single_picture_hdr_flag) {
    pars->apply_grain = 1;
  } else {
    pars->apply_grain = aom_rb_read_bit(rb);
  }
#else
  pars->apply_grain = aom_rb_read_bit(rb);
#endif  // CONFIG_CWG_F362
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
    int film_grain_params_ref_idx =
        aom_rb_read_literal(rb, cm->seq_params.ref_frames_log2);
    if (film_grain_params_ref_idx >= cm->seq_params.ref_frames) {
      aom_internal_error(
          &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
          "Film grain reference idx must be less than %d but is set to %d",
          cm->seq_params.ref_frames, film_grain_params_ref_idx);
    }
    // Section 6.8.20: It is a requirement of bitstream conformance that
    // film_grain_params_ref_idx is equal to ref_frame_idx[ j ] for some value
    // of j in the range 0 to INTER_REFS_PER_FRAME - 1.
    int found = 0;
    for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i) {
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
#if CONFIG_CWG_F298_REC11
#define fgm_value_increment(i, j) (fgm_scaling_points[i][j][0])
#define fgm_value_scale(i, j) (fgm_scaling_points[i][j][1])
  int(*fgm_scaling_points[])[2] = { pars->fgm_scaling_points_0,
                                    pars->fgm_scaling_points_1,
                                    pars->fgm_scaling_points_2 };

  int fgmNumChannels = seq_params->monochrome ? 1 : 3;

  if (fgmNumChannels > 1) {
    pars->fgm_scale_from_channel0_flag = aom_rb_read_bit(rb);
  } else {
    pars->fgm_scale_from_channel0_flag = 0;
  }

  int fgmNumScalingChannels =
      pars->fgm_scale_from_channel0_flag ? 1 : fgmNumChannels;

  for (int i = 0; i < fgmNumScalingChannels; i++) {
    pars->fgm_points[i] = aom_rb_read_literal(rb, 4);  // max 14
    if (pars->fgm_points[i] > 14)
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Number of points for film grain scaling function "
                         "exceeds the maximum value.");
    for (int j = 0; j < pars->fgm_points[i]; j++) {
      fgm_value_increment(i, j) = aom_rb_read_literal(rb, 8);
      fgm_scaling_points[i][j][0] =
          j ? fgm_value_increment(i, j) + fgm_scaling_points[i][j - 1][0]
            : fgm_value_increment(i, j);
      if (j && fgm_scaling_points[i][j - 1][0] >= fgm_scaling_points[i][j][0])
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "First coordinate of the scaling function points "
                           "shall be increasing.");
      if (j && fgm_scaling_points[i][j][0] > 255)
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "First coordinate of the scaling function point "
                           "exceeds the value 255.");
      fgm_value_scale(i, j) = aom_rb_read_literal(rb, 8);
    }
  }

  // Initialize unsignaled values to zero
  for (int i = fgmNumScalingChannels; i < 3; i++) pars->fgm_points[i] = 0;

  if ((seq_params->subsampling_x == 1) && (seq_params->subsampling_y == 1) &&
      (((pars->fgm_points[1] == 0) && (pars->fgm_points[2] != 0)) ||
       ((pars->fgm_points[1] != 0) && (pars->fgm_points[2] == 0))))
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "In YCbCr 4:2:0, film grain shall be applied "
                       "to both chroma components or neither.");
#else
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
#endif

#if !CONFIG_CWG_F298_REC11
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
#endif

  pars->scaling_shift = aom_rb_read_literal(rb, 2) + 8;  // 8 + value

  // AR coefficients
  // Only sent if the corresponsing scaling function has
  // more than 0 points

  pars->ar_coeff_lag = aom_rb_read_literal(rb, 2);

  int num_pos_luma = 2 * pars->ar_coeff_lag * (pars->ar_coeff_lag + 1);
  int num_pos_chroma = num_pos_luma;
#if CONFIG_CWG_F298_REC11
  if (pars->fgm_points[0] > 0) ++num_pos_chroma;
  if (pars->fgm_points[0])
#else
  if (pars->num_y_points > 0) ++num_pos_chroma;

  if (pars->num_y_points)
#endif
    for (int i = 0; i < num_pos_luma; i++)
      pars->ar_coeffs_y[i] = aom_rb_read_literal(rb, 8) - 128;

#if CONFIG_CWG_F298_REC11
  if (pars->fgm_points[1] || pars->fgm_scale_from_channel0_flag)
#else
  if (pars->num_cb_points || pars->chroma_scaling_from_luma)
#endif
    for (int i = 0; i < num_pos_chroma; i++)
      pars->ar_coeffs_cb[i] = aom_rb_read_literal(rb, 8) - 128;

#if CONFIG_CWG_F298_REC11
  if (pars->fgm_points[2] || pars->fgm_scale_from_channel0_flag)
#else
  if (pars->num_cr_points || pars->chroma_scaling_from_luma)
#endif
    for (int i = 0; i < num_pos_chroma; i++)
      pars->ar_coeffs_cr[i] = aom_rb_read_literal(rb, 8) - 128;

  pars->ar_coeff_shift = aom_rb_read_literal(rb, 2) + 6;  // 6 + value

  pars->grain_scale_shift = aom_rb_read_literal(rb, 2);

#if CONFIG_CWG_F298_REC11
  if (pars->fgm_points[1]) {
#else
  if (pars->num_cb_points) {
#endif
    pars->cb_mult = aom_rb_read_literal(rb, 8);
    pars->cb_luma_mult = aom_rb_read_literal(rb, 8);
    pars->cb_offset = aom_rb_read_literal(rb, 9);
  }

#if CONFIG_CWG_F298_REC11
  if (pars->fgm_points[2]) {
#else
  if (pars->num_cr_points) {
#endif
    pars->cr_mult = aom_rb_read_literal(rb, 8);
    pars->cr_luma_mult = aom_rb_read_literal(rb, 8);
    pars->cr_offset = aom_rb_read_literal(rb, 9);
  }

  pars->overlap_flag = aom_rb_read_bit(rb);

  pars->clip_to_restricted_range = aom_rb_read_bit(rb);

#if CONFIG_FGS_BLOCK_SIZE
  pars->block_size = aom_rb_read_bit(rb);
#endif
}

static AOM_INLINE void read_film_grain(AV1_COMMON *cm,
                                       struct aom_read_bit_buffer *rb) {
  if (cm->seq_params.film_grain_params_present
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
      && (cm->seq_params.enable_frame_output_order || cm->show_frame ||
          cm->showable_frame)
#endif
  ) {
    av1_read_film_grain_params(cm, rb);
  } else {
    memset(&cm->film_grain_params, 0, sizeof(cm->film_grain_params));
  }
  cm->film_grain_params.bit_depth = cm->seq_params.bit_depth;
  memcpy(&cm->cur_frame->film_grain_params, &cm->film_grain_params,
         sizeof(aom_film_grain_t));
}

#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
// Given chroma_format_idc, set the subsampling_x/y values in `seq_params`.
// Calls `aom_internal_error` in case of invalid chroma_format_idc.
static void set_seq_chroma_format(uint32_t seq_chroma_format_idc,
                                  SequenceHeader *seq_params,
                                  struct aom_internal_error_info *error_info) {
  aom_codec_err_t err = av1_get_chroma_subsampling(seq_chroma_format_idc,
                                                   &seq_params->subsampling_x,
                                                   &seq_params->subsampling_y);
  if (err != AOM_CODEC_OK) {
    aom_internal_error(
        error_info, AOM_CODEC_UNSUP_BITSTREAM,
        "Only 4:0:0, 4:4:4, 4:2:2 and 4:2:0 are currently supported. "
        "Invalid chroma_format_idc value: %d.\n",
        seq_chroma_format_idc);
  }
}
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

void av1_read_color_config(struct aom_read_bit_buffer *rb,
                           SequenceHeader *seq_params,
                           struct aom_internal_error_info *error_info) {
#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  const uint32_t seq_chroma_format_idc = aom_rb_read_uvlc(rb);
  set_seq_chroma_format(seq_chroma_format_idc, seq_params, error_info);
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

  read_bitdepth(rb, seq_params, error_info);

#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  const int is_monochrome = (seq_chroma_format_idc == CHROMA_FORMAT_400);
#else
  // monochrome bit (not needed for PROFILE_1)
  const int is_monochrome =
      seq_params->profile != PROFILE_1 ? aom_rb_read_bit(rb) : 0;
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC
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
    seq_params->chroma_sample_position = AOM_CSP_UNSPECIFIED;
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
#if !CONFIG_CWG_E242_CHROMA_FORMAT_IDC
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
#endif  // !CONFIG_CWG_E242_CHROMA_FORMAT_IDC
      if (seq_params->matrix_coefficients == AOM_CICP_MC_IDENTITY &&
          (seq_params->subsampling_x || seq_params->subsampling_y)) {
        aom_internal_error(
            error_info, AOM_CODEC_UNSUP_BITSTREAM,
            "Identity CICP Matrix incompatible with non 4:4:4 color sampling");
      }
      if (seq_params->subsampling_x && !seq_params->subsampling_y) {
        // YUV 4:2:2
        seq_params->chroma_sample_position = AOM_CSP_UNSPECIFIED;
        const int csp_present_flag = aom_rb_read_bit(rb);
        if (csp_present_flag) {
          seq_params->chroma_sample_position = aom_rb_read_bit(rb);
        }
      } else if (seq_params->subsampling_x && seq_params->subsampling_y) {
        // YUV 4:2:0
        seq_params->chroma_sample_position = AOM_CSP_UNSPECIFIED;
        const int csp_present_flag = aom_rb_read_bit(rb);
        if (csp_present_flag) {
          const int chroma_sample_position = aom_rb_read_literal(rb, 3);
          if (chroma_sample_position > AOM_CSP_BOTTOM) {
            aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                               "Invalid chroma_sample_position");
          }
          seq_params->chroma_sample_position = chroma_sample_position;
        }
      }
    }
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
#if !CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING
  decoder_model_info->buffer_removal_time_length =
      aom_rb_read_literal(rb, 5) + 1;
#endif  // !CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING
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

#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
void read_tile_syntax_info(TileInfoSyntax *tile_params,
                           struct aom_read_bit_buffer *rb) {
#if CONFIG_CWG_F349_SIGNAL_TILE_INFO
  tile_params->allow_tile_info_change = aom_rb_read_bit(rb);
#endif  // CONFIG_CWG_F349_SIGNAL_TILE_INFO
  CommonTileParams *tile_info = &tile_params->tile_info;
  tile_info->uniform_spacing = aom_rb_read_bit(rb);

  // Read tile columns
  if (tile_info->uniform_spacing) {
    tile_info->log2_cols = tile_info->min_log2_cols;
    while (tile_info->log2_cols < tile_info->max_log2_cols) {
      if (!aom_rb_read_bit(rb)) {
        break;
      }
      tile_info->log2_cols++;
    }
  } else {
    int i;
    int start_sb;
    int width_sb = tile_info->sb_cols;
    for (i = 0, start_sb = 0; width_sb > 0 && i < MAX_TILE_COLS; i++) {
      const int size_sb =
          1 + rb_read_uniform(rb, AOMMIN(width_sb, tile_info->max_width_sb));
      tile_info->col_start_sb[i] = start_sb;
      start_sb += size_sb;
      width_sb -= size_sb;
    }
    tile_info->cols = i;
    tile_info->col_start_sb[i] = start_sb + width_sb;
    assert(width_sb == 0);
  }
  tile_info->min_log2_rows =
      AOMMAX(tile_info->min_log2 - tile_info->log2_cols, 0);
  av1_calculate_tile_cols(tile_info);

  // Read tile rows
  if (tile_info->uniform_spacing) {
    tile_info->log2_rows = tile_info->min_log2_rows;
    while (tile_info->log2_rows < tile_info->max_log2_rows) {
      if (!aom_rb_read_bit(rb)) {
        break;
      }
      tile_info->log2_rows++;
    }
  } else {
    int i;
    int start_sb;
    int height_sb = tile_info->sb_rows;
    for (i = 0, start_sb = 0; height_sb > 0 && i < MAX_TILE_ROWS; i++) {
      const int size_sb =
          1 + rb_read_uniform(rb, AOMMIN(height_sb, tile_info->max_height_sb));
      tile_info->row_start_sb[i] = start_sb;
      start_sb += size_sb;
      height_sb -= size_sb;
    }
    tile_info->rows = i;
    tile_info->row_start_sb[i] = start_sb + height_sb;
    assert(height_sb == 0);
  }
  av1_calculate_tile_rows(tile_info);
}

void read_sequence_tile_info(struct SequenceHeader *seq_params,
                             struct aom_read_bit_buffer *rb) {
  av1_get_seqmfh_tile_limits(
      &seq_params->tile_params, seq_params->max_frame_height,
      seq_params->max_frame_width, seq_params->mib_size_log2,
      seq_params->mib_size_log2);
  read_tile_syntax_info(&seq_params->tile_params, rb);
}
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO

void av1_read_sequence_header(
#if !CWG_F215_CONFIG_REMOVE_FRAME_ID
    AV1_COMMON *cm,
#endif  // !CWG_F215_CONFIG_REMOVE_FRAME_ID
    struct aom_read_bit_buffer *rb, SequenceHeader *seq_params) {
#if !CWG_F215_CONFIG_REMOVE_FRAME_ID
  if (seq_params->single_picture_hdr_flag) {
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
#endif  // !CWG_F215_CONFIG_REMOVE_FRAME_ID

  setup_seq_sb_size(seq_params, rb);
#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
  seq_params->seq_tile_info_present_flag = 0;
#if CONFIG_CWG_F349_SIGNAL_TILE_INFO
  seq_params->tile_params.allow_tile_info_change = 0;
#endif  // CONFIG_CWG_F349_SIGNAL_TILE_INFO
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO
  seq_params->enable_intra_dip = aom_rb_read_bit(rb);
  seq_params->enable_intra_edge_filter = aom_rb_read_bit(rb);
  if (seq_params->single_picture_hdr_flag) {
    seq_params->seq_enabled_motion_modes = (1 << SIMPLE_TRANSLATION);
    seq_params->enable_masked_compound = 0;
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
    seq_params->order_hint_info.enable_order_hint = 0;
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
    seq_params->order_hint_info.enable_ref_frame_mvs = 0;
    seq_params->force_screen_content_tools = 2;  // SELECT_SCREEN_CONTENT_TOOLS
    seq_params->force_integer_mv = 2;            // SELECT_INTEGER_MV
    seq_params->enable_tcq = 1;
    seq_params->order_hint_info.order_hint_bits_minus_1 = -1;
    seq_params->enable_opfl_refine = AOM_OPFL_REFINE_NONE;
    seq_params->enable_six_param_warp_delta = 0;
#if CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
    seq_params->seq_frame_motion_modes_present_flag = 0;
#endif  // CONFIG_MOTION_MODE_FRAME_HEADERS_OPT

  } else {
    int seq_enabled_motion_modes = (1 << SIMPLE_TRANSLATION);
#if CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
    uint8_t motion_mode_enabled = 0;
    uint8_t warp_delta_enabled = 0;
#endif  // CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
    for (int motion_mode = INTERINTRA; motion_mode < MOTION_MODES;
         motion_mode++) {
      int enabled = aom_rb_read_bit(rb);
#if CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
      motion_mode_enabled |= enabled;
      if (motion_mode == WARP_DELTA && enabled) {
        warp_delta_enabled = 1;
      }
#endif  // CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
      if (enabled) {
        seq_enabled_motion_modes |= (1 << motion_mode);
      }
    }

#if CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
    seq_params->seq_frame_motion_modes_present_flag =
        motion_mode_enabled ? aom_rb_read_bit(rb) : 0;
    seq_params->enable_six_param_warp_delta =
        warp_delta_enabled ? aom_rb_read_bit(rb) : 0;
#else
    seq_params->enable_six_param_warp_delta = aom_rb_read_bit(rb);
#endif  // CONFIG_MOTION_MODE_FRAME_HEADERS_OPT

    seq_params->seq_enabled_motion_modes = seq_enabled_motion_modes;
    seq_params->enable_masked_compound = aom_rb_read_bit(rb);
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
    seq_params->order_hint_info.enable_order_hint = aom_rb_read_bit(rb);
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
    seq_params->order_hint_info.enable_ref_frame_mvs =
#if CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
        aom_rb_read_bit(rb);
#else
        seq_params->order_hint_info.enable_order_hint ? aom_rb_read_bit(rb) : 0;
#endif  // CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
    seq_params->order_hint_info.reduced_ref_frame_mvs_mode =
        seq_params->order_hint_info.enable_ref_frame_mvs ? aom_rb_read_bit(rb)
                                                         : 0;

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
#if CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
        aom_rb_read_literal(rb, 3);
#else
        seq_params->order_hint_info.enable_order_hint
            ? aom_rb_read_literal(rb, 3)
            : -1;
#endif  // CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
  }

#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  seq_params->disable_loopfilters_across_tiles = aom_rb_read_bit(rb);
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  seq_params->enable_cdef = aom_rb_read_bit(rb);
  seq_params->enable_gdf = aom_rb_read_bit(rb);
  seq_params->enable_restoration = aom_rb_read_bit(rb);
  seq_params->lr_tools_disable_mask[0] = 0;
  seq_params->lr_tools_disable_mask[1] = 0;
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

  const int is_monochrome = seq_params->monochrome;
  if (is_monochrome) {
    seq_params->separate_uv_delta_q = 0;
  } else {
    seq_params->separate_uv_delta_q = aom_rb_read_bit(rb);
  }

  seq_params->equal_ac_dc_q = aom_rb_read_bit(rb);
  if (!seq_params->equal_ac_dc_q) {
    seq_params->base_y_dc_delta_q =
        DELTA_DCQUANT_MIN + aom_rb_read_literal(rb, DELTA_DCQUANT_BITS);
    seq_params->y_dc_delta_q_enabled = aom_rb_read_bit(rb);
  } else {
    seq_params->base_y_dc_delta_q = 0;
    seq_params->y_dc_delta_q_enabled = 0;
  }
  if (!is_monochrome) {
    if (!seq_params->equal_ac_dc_q) {
      seq_params->base_uv_dc_delta_q =
          DELTA_DCQUANT_MIN + aom_rb_read_literal(rb, DELTA_DCQUANT_BITS);
      seq_params->uv_dc_delta_q_enabled = aom_rb_read_bit(rb);
    } else {
      seq_params->uv_dc_delta_q_enabled = 0;
    }
    seq_params->base_uv_ac_delta_q =
        DELTA_DCQUANT_MIN + aom_rb_read_literal(rb, DELTA_DCQUANT_BITS);
    seq_params->uv_ac_delta_q_enabled = aom_rb_read_bit(rb);
    if (seq_params->equal_ac_dc_q)
      seq_params->base_uv_dc_delta_q = seq_params->base_uv_ac_delta_q;
  } else {
    seq_params->base_uv_dc_delta_q = 0;
    seq_params->base_uv_ac_delta_q = 0;
    seq_params->uv_dc_delta_q_enabled = 0;
    seq_params->uv_ac_delta_q_enabled = 0;
  }
#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
  seq_params->seq_tile_info_present_flag = aom_rb_read_bit(rb);
  if (seq_params->seq_tile_info_present_flag) {
    read_sequence_tile_info(seq_params, rb);
  }
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO
}

// Decodes the user-defined quantization matrices for the given level and stores
// them in seq_params.
static AOM_INLINE void decode_qm_data(
    SequenceHeader *const seq_params, struct aom_read_bit_buffer *rb, int level,
    int num_planes, struct aom_internal_error_info *error_info) {
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };
  qm_val_t ***fund_mat[3] = { seq_params->quantizer_matrix_8x8,
                              seq_params->quantizer_matrix_8x4,
                              seq_params->quantizer_matrix_4x8 };

  for (int t = 0; t < 3; t++) {
    const TX_SIZE tsize = fund_tsize[t];
    const int width = tx_size_wide[tsize];
    const int height = tx_size_high[tsize];
    const SCAN_ORDER *s = get_scan(tsize, DCT_DCT);

    for (int c = 0; c < num_planes; c++) {
      if (c > 0) {
        const bool qm_copy_from_previous_plane = aom_rb_read_bit(rb);

        if (qm_copy_from_previous_plane) {
          const qm_val_t *src_mat = fund_mat[t][level][c - 1];
          qm_val_t *dst_mat = fund_mat[t][level][c];
          memcpy(dst_mat, src_mat, width * height * sizeof(qm_val_t));
          continue;
        }
      }
      bool qm_8x8_is_symmetric = false;
      if (tsize == TX_8X8) {
        qm_8x8_is_symmetric = aom_rb_read_bit(rb);
      } else if (tsize == TX_4X8) {
        const bool qm_4x8_is_transpose_of_8x4 = aom_rb_read_bit(rb);

        if (qm_4x8_is_transpose_of_8x4) {
          assert(fund_tsize[t - 1] == TX_8X4);
          const qm_val_t *src_mat = fund_mat[t - 1][level][c];
          qm_val_t *dst_mat = fund_mat[t][level][c];

          for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
              dst_mat[j] = src_mat[j * height];
            }
            src_mat += 1;
            dst_mat += width;
          }
          continue;
        }
      }

      qm_val_t *mat = fund_mat[t][level][c];
      bool coef_repeat_until_end = false;
      qm_val_t prev = 32;
      for (int i = 0; i < tx_size_2d[tsize]; i++) {
        const int pos = s->scan[i];
        if (tsize == TX_8X8 && qm_8x8_is_symmetric) {
          const int row = pos / width;
          const int col = pos % width;
          if (col > row) {
            prev = mat[col * width + row];
            mat[pos] = prev;
            continue;
          }
        }

        if (!coef_repeat_until_end) {
          const int32_t delta = aom_rb_read_svlc(rb);
          // The valid range of quantization matrix coefficients is 1..255.
          // Therefore the valid range of delta values is -254..254. Because of
          // the & 255 operation, the valid range of delta values can be reduced
          // to -128..127 to shorten the svlc() code.
          if (delta < -128 || delta > 127) {
            aom_internal_error(error_info, AOM_CODEC_CORRUPT_FRAME,
                               "Invalid matrix_coef_delta: %d", delta);
          }
          const qm_val_t coef = (prev + delta) & 255;
          if (coef == 0) {
            coef_repeat_until_end = true;
          } else {
            prev = coef;
          }
        }
        mat[pos] = prev;
      }
    }
  }
}

// Decodes all user-defined quantization matrices and stores them in seq_params.
static AOM_INLINE void decode_user_defined_qm(
    SequenceHeader *const seq_params, struct aom_read_bit_buffer *rb,
    int num_planes, struct aom_internal_error_info *error_info) {
  for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
    seq_params->qm_data_present[i] = aom_rb_read_bit(rb);
#if CONFIG_QM_DEBUG
    printf("[DEC-SEQ] qm_data_present[%d]: %d\n", i,
           seq_params->qm_data_present[i]);
#endif
    if (seq_params->qm_data_present[i]) {
      decode_qm_data(seq_params, rb, i, num_planes, error_info);
    }
  }
}

void av1_read_sequence_header_beyond_av1(
    struct aom_read_bit_buffer *rb, SequenceHeader *seq_params,
    CommonQuantParams *quant_params,
    struct aom_internal_error_info *error_info) {
  // printf("print sps\n");
  seq_params->enable_refmvbank = aom_rb_read_bit(rb);
  if (aom_rb_read_bit(rb)) {
    seq_params->enable_drl_reorder = DRL_REORDER_DISABLED;
  } else {
    seq_params->enable_drl_reorder =
        aom_rb_read_bit(rb) ? DRL_REORDER_CONSTRAINT : DRL_REORDER_ALWAYS;
  }

  if (aom_rb_read_bit(rb)) {
    seq_params->enable_cdef_on_skip_txfm = CDEF_ON_SKIP_TXFM_ALWAYS_ON;
  } else {
    seq_params->enable_cdef_on_skip_txfm = aom_rb_read_bit(rb)
                                               ? CDEF_ON_SKIP_TXFM_DISABLED
                                               : CDEF_ON_SKIP_TXFM_ADAPTIVE;
  }
  seq_params->enable_avg_cdf = aom_rb_read_bit(rb);
  if (seq_params->enable_avg_cdf) {
    seq_params->avg_cdf_type = aom_rb_read_bit(rb);
  }

  seq_params->explicit_ref_frame_map = aom_rb_read_bit(rb);

#if !CONFIG_F253_REMOVE_OUTPUTFLAG
  // 0 : use show_existing_frame, 1: use implicit derivation
  seq_params->enable_frame_output_order = aom_rb_read_bit(rb);
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG

#if CONFIG_CWG_F168_DPB_HLS
  if (aom_rb_read_bit(rb)) {
    seq_params->ref_frames =
        aom_rb_read_literal(rb, 4) + 1;  // explicitly signaled DPB size
  } else {
    seq_params->ref_frames = 8;  // default DPB size: 8
  }
  seq_params->ref_frames_log2 = aom_ceil_log2(seq_params->ref_frames);
#else
  // A bit is sent here to indicate if the max number of references is 7. If
  // this bit is 0, then two more bits are sent to indicate the exact number
  // of references allowed (range: 3 to 6).
  if (aom_rb_read_bit(rb)) {
    seq_params->max_reference_frames = 3 + aom_rb_read_literal(rb, 2);
  } else {
    seq_params->max_reference_frames = 7;
  }
  const bool use_extra_dpb = aom_rb_read_bit(rb);

  if (use_extra_dpb) {
    seq_params->num_extra_dpb = 1 + aom_rb_read_literal(rb, 3);
  } else {
    seq_params->num_extra_dpb = 0;
  }

  seq_params->ref_frames = seq_params->num_extra_dpb
                               ? REGULAR_REF_FRAMES + seq_params->num_extra_dpb
                               : REGULAR_REF_FRAMES;

  seq_params->ref_frames_log2 =
      seq_params->num_extra_dpb ? REF_FRAMES_LOG2 + 1 : REF_FRAMES_LOG2;
#endif  // CONFIG_CWG_F168_DPB_HLS

  seq_params->def_max_drl_bits =
      aom_rb_read_primitive_quniform(rb,
                                     MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1) +
      MIN_MAX_DRL_BITS;
  seq_params->allow_frame_max_drl_bits = aom_rb_read_bit(rb);
  seq_params->def_max_bvp_drl_bits =
      aom_rb_read_primitive_quniform(
          rb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1) +
      MIN_MAX_IBC_DRL_BITS;
  seq_params->allow_frame_max_bvp_drl_bits = aom_rb_read_bit(rb);

  seq_params->num_same_ref_compound = aom_rb_read_literal(rb, 2);
  seq_params->enable_sdp = seq_params->monochrome ? 0 : aom_rb_read_bit(rb);
  seq_params->enable_extended_sdp =
      seq_params->enable_sdp ? aom_rb_read_bit(rb) : 0;
  seq_params->enable_ist = aom_rb_read_bit(rb);
  seq_params->enable_inter_ist = aom_rb_read_bit(rb);
  seq_params->enable_chroma_dctonly =
      seq_params->monochrome ? 0 : aom_rb_read_bit(rb);
  seq_params->enable_inter_ddt = aom_rb_read_bit(rb);
  seq_params->reduced_tx_part_set = aom_rb_read_bit(rb);
  seq_params->enable_cctx = seq_params->monochrome ? 0 : aom_rb_read_bit(rb);
  seq_params->enable_mrls = aom_rb_read_bit(rb);
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
  seq_params->enable_cfl_intra = aom_rb_read_bit(rb);
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
  seq_params->enable_mhccp = aom_rb_read_bit(rb);
  uint8_t enable_tip = aom_rb_read_bit(rb);
  if (enable_tip) {
    seq_params->enable_tip = 1 + aom_rb_read_bit(rb);
  } else {
    seq_params->enable_tip = 0;
  }
  if (seq_params->enable_tip) {
    seq_params->enable_tip_hole_fill = aom_rb_read_bit(rb);
  } else {
    seq_params->enable_tip_hole_fill = 0;
  }
  seq_params->enable_mv_traj = aom_rb_read_bit(rb);
  seq_params->enable_bawp = aom_rb_read_bit(rb);
  seq_params->enable_cwp = aom_rb_read_bit(rb);
  seq_params->enable_imp_msk_bld = aom_rb_read_bit(rb);
  seq_params->enable_fsc = aom_rb_read_bit(rb);
#if CONFIG_FSC_RES_HLS
  if (!seq_params->enable_fsc) {
    seq_params->enable_idtx_intra = aom_rb_read_bit(rb);
  } else {
    seq_params->enable_idtx_intra = 1;
  }
#endif  // CONFIG_FSC_RES_HLS
  seq_params->enable_ccso = aom_rb_read_bit(rb);
  seq_params->enable_lf_sub_pu = aom_rb_read_bit(rb);
  if (seq_params->enable_tip == 1 && seq_params->enable_lf_sub_pu) {
    seq_params->enable_tip_explicit_qp = aom_rb_read_bit(rb);
  } else {
    seq_params->enable_tip_explicit_qp = 0;
  }
  seq_params->enable_orip = aom_rb_read_bit(rb);
#if CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
  seq_params->enable_opfl_refine = aom_rb_read_literal(rb, 2);
#else
  seq_params->enable_opfl_refine = seq_params->order_hint_info.enable_order_hint
                                       ? aom_rb_read_literal(rb, 2)
                                       : AOM_OPFL_REFINE_NONE;
#endif  // CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
  seq_params->enable_ibp = aom_rb_read_bit(rb);
  seq_params->enable_adaptive_mvd = aom_rb_read_bit(rb);

  seq_params->enable_refinemv = aom_rb_read_bit(rb);

  seq_params->enable_tip_refinemv =
      (seq_params->enable_tip &&
       (seq_params->enable_opfl_refine || seq_params->enable_refinemv))
          ? aom_rb_read_bit(rb)
          : 0;
  seq_params->enable_bru = aom_rb_read_bit(rb);
#if CONFIG_DERIVED_MVD_SIGN
  seq_params->enable_mvd_sign_derive = aom_rb_read_bit(rb);
#endif  // CONFIG_DERIVED_MVD_SIGN
  seq_params->enable_flex_mvres = aom_rb_read_bit(rb);
  seq_params->cfl_ds_filter_index =
      seq_params->monochrome ? 0 : aom_rb_read_literal(rb, 2);

  seq_params->enable_tcq = 0;
  int enable_tcq = aom_rb_read_bit(rb);
  if (enable_tcq) {
    enable_tcq += aom_rb_read_bit(rb);
    seq_params->enable_tcq = enable_tcq;
  }
  if (seq_params->enable_tcq == TCQ_DISABLE ||
      seq_params->enable_tcq >= TCQ_8ST_FR) {
    seq_params->enable_parity_hiding = aom_rb_read_bit(rb);
  } else {
    seq_params->enable_parity_hiding = 0;
  }
  seq_params->enable_ext_partitions = aom_rb_read_bit(rb);
  if (seq_params->enable_ext_partitions)
    seq_params->enable_uneven_4way_partitions = aom_rb_read_bit(rb);
  else
    seq_params->enable_uneven_4way_partitions = 0;
  seq_params->max_pb_aspect_ratio_log2_m1 = 2;
  if (aom_rb_read_bit(rb)) {
    seq_params->max_pb_aspect_ratio_log2_m1 = aom_rb_read_bit(rb);
  }
  if (seq_params->single_picture_hdr_flag) {
    seq_params->enable_global_motion = 0;
  } else {
    seq_params->enable_global_motion = aom_rb_read_bit(rb);
  }
  seq_params->df_par_bits_minus2 = aom_rb_read_literal(rb, 2);
  seq_params->enable_short_refresh_frame_flags = aom_rb_read_bit(rb);
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  seq_params->number_of_bits_for_lt_frame_id = aom_rb_read_literal(rb, 3);
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_EXT_SEG
  seq_params->enable_ext_seg = aom_rb_read_bit(rb);
#endif  // CONFIG_EXT_SEG
  seq_params->user_defined_qmatrix = aom_rb_read_bit(rb);
#if CONFIG_QM_DEBUG
  printf("[DEC-SEQ] user_defined_qmatrix=%d\n",
         seq_params->user_defined_qmatrix);
#endif
  if (seq_params->user_defined_qmatrix) {
    int num_planes = seq_params->monochrome ? 1 : MAX_MB_PLANE;
    if (!quant_params->qmatrix_allocated) {
      seq_params->quantizer_matrix_8x8 = av1_alloc_qm(8, 8);
      seq_params->quantizer_matrix_8x4 = av1_alloc_qm(8, 4);
      seq_params->quantizer_matrix_4x8 = av1_alloc_qm(4, 8);
      quant_params->qmatrix_allocated = true;
    }
    av1_init_qmatrix(seq_params->quantizer_matrix_8x8,
                     seq_params->quantizer_matrix_8x4,
                     seq_params->quantizer_matrix_4x8, num_planes);
    decode_user_defined_qm(seq_params, rb, num_planes, error_info);
  } else {
    for (uint16_t i = 0; i < NUM_CUSTOM_QMS; i++) {
      seq_params->qm_data_present[i] = false;
    }
  }
}

#if CONFIG_MULTI_FRAME_HEADER
void av1_read_multi_frame_header(AV1_COMMON *cm,
                                 struct aom_read_bit_buffer *rb) {
  // TO DO: Adding a read seq_header_id_in_multi_frame_header here
#if CONFIG_CWG_E242_MFH_ID_UVLC
  int cur_mfh_id = aom_rb_read_uvlc(rb) + 1;
#else
  int cur_mfh_id = aom_rb_read_literal(rb, 4) + 1;
#endif  // CONFIG_CWG_E242_MFH_ID_UVLC
  if (cur_mfh_id >= MAX_MFH_NUM) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "multi-frame header id is greater than or equal to the "
                       "maximum multi-frame header number");
  }

  MultiFrameHeader *mfh_param = &cm->mfh_params[cur_mfh_id];

#if CONFIG_CWG_E242_PARSING_INDEP
  mfh_param->mfh_frame_size_present_flag = aom_rb_read_bit(rb);
  if (mfh_param->mfh_frame_size_present_flag) {
    mfh_param->mfh_frame_width_bits_minus1 = aom_rb_read_literal(rb, 4);
    int num_bits_width = mfh_param->mfh_frame_width_bits_minus1 + 1;
    mfh_param->mfh_frame_height_bits_minus1 = aom_rb_read_literal(rb, 4);
    int num_bits_height = mfh_param->mfh_frame_height_bits_minus1 + 1;
    av1_read_frame_size(rb, num_bits_width, num_bits_height,
                        &mfh_param->mfh_frame_width,
                        &mfh_param->mfh_frame_height);
  }
#else
  bool frame_size_update_flag = aom_rb_read_bit(rb);

  int width = cm->seq_params.max_frame_width;
  int height = cm->seq_params.max_frame_height;
  if (frame_size_update_flag) {
    int num_bits_width = cm->seq_params.num_bits_width;
    int num_bits_height = cm->seq_params.num_bits_height;
    av1_read_frame_size(rb, num_bits_width, num_bits_height, &width, &height);
    if (width > cm->seq_params.max_frame_width ||
        height > cm->seq_params.max_frame_height) {
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Frame dimensions are larger than the maximum values");
    }
  }
  mfh_param->mfh_frame_width = width;
  mfh_param->mfh_frame_height = height;
#endif  // CONFIG_CWG_E242_PARSING_INDEP

#if CONFIG_CWG_E242_PARSING_INDEP
#if !CONFIG_CWG_F248_RENDER_SIZE
  mfh_param->mfh_render_size_present_flag = aom_rb_read_bit(rb);
  if (mfh_param->mfh_render_size_present_flag) {
    av1_read_frame_size(rb, 16, 16, &mfh_param->mfh_render_width,
                        &mfh_param->mfh_render_height);
  }
#endif  // !CONFIG_CWG_F248_RENDER_SIZE
#else
#if !CONFIG_CWG_F248_RENDER_SIZE
  if (aom_rb_read_bit(rb)) {
    av1_read_frame_size(rb, 16, 16, &mfh_param->mfh_render_width,
                        &mfh_param->mfh_render_height);
  } else {
    mfh_param->mfh_render_width = mfh_param->mfh_frame_width;
    mfh_param->mfh_render_height = mfh_param->mfh_frame_height;
  }
#endif  // CONFIG_CWG_E242_PARSING_INDEP
#endif  // !CONFIG_CWG_F248_RENDER_SIZE

  mfh_param->mfh_loop_filter_update_flag = aom_rb_read_bit(rb);
  if (mfh_param->mfh_loop_filter_update_flag) {
    for (int i = 0; i < 4; i++) {
      mfh_param->mfh_loop_filter_level[i] = aom_rb_read_bit(rb);
    }
  } else {
    for (int i = 0; i < 4; i++) {
      mfh_param->mfh_loop_filter_level[i] = 0;
    }
  }
  cm->mfh_valid[cur_mfh_id] = true;
}
#endif  // CONFIG_MULTI_FRAME_HEADER

static int read_global_motion_params(WarpedMotionParams *params,
                                     const WarpedMotionParams *ref_params,
                                     struct aom_read_bit_buffer *rb,
#if CONFIG_ACROSS_SCALE_WARP
                                     const struct scale_factors *sf,
#endif  // CONFIG_ACROSS_SCALE_WARP
                                     MvSubpelPrecision precision) {
  const int precision_loss = get_gm_precision_loss(precision);
  (void)precision_loss;
  TransformationType type = aom_rb_read_bit(rb);
  if (type != IDENTITY) {
    if (aom_rb_read_bit(rb)) {
      type = ROTZOOM;
    } else {
      type = AFFINE;
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
    const int trans_dec_factor = GM_TRANS_DECODE_FACTOR;
    const int trans_prec_diff = GM_TRANS_PREC_DIFF;
    const int trans_max = GM_TRANS_MAX;

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
    av1_reduce_warp_model(params);
    int good_shear_params = av1_get_shear_params(params
#if CONFIG_ACROSS_SCALE_WARP
                                                 ,
                                                 sf
#endif  // CONFIG_ACROSS_SCALE_WARP
    );
    if (!good_shear_params) return 0;
  }

  return 1;
}

static AOM_INLINE void read_global_motion(AV1_COMMON *cm,
                                          struct aom_read_bit_buffer *rb) {
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
      const int our_ref_order_hint = buf->display_order_hint;
      const int their_ref_order_hint = buf->ref_display_order_hint[their_ref];
      cm->base_global_motion_model = buf->global_motion[their_ref];
      cm->base_global_motion_distance =
          get_relative_dist(&seq_params->order_hint_info, our_ref_order_hint,
                            their_ref_order_hint);
    }
  }

  for (int frame = 0; frame < cm->ref_frames_info.num_total_refs; ++frame) {
    int temporal_distance;
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
    if (seq_params->order_hint_info.enable_order_hint) {
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
      const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, frame);
      const int ref_order_hint = ref_buf->display_order_hint;
      const int cur_order_hint = cm->cur_frame->display_order_hint;
      temporal_distance = get_relative_dist(&seq_params->order_hint_info,
                                            cur_order_hint, ref_order_hint);
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
    } else {
      temporal_distance = 1;
    }
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT

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
    int good_params =
        read_global_motion_params(&cm->global_motion[frame], ref_params, rb,
#if CONFIG_ACROSS_SCALE_WARP
                                  get_ref_scale_factors_const(cm, frame),
#endif  // CONFIG_ACROSS_SCALE_WARP
                                  cm->features.fr_mv_precision);
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
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    decrease_ref_count(cm->ref_frame_map[i], pool);
    cm->ref_frame_map[i] = NULL;
  }
}

// If the refresh_frame_flags bitmask is set, update reference frame id values
// and mark frames as valid for reference.
static AOM_INLINE void update_ref_frame_id(AV1Decoder *const pbi) {
  AV1_COMMON *const cm = &pbi->common;
  int refresh_frame_flags = cm->current_frame.refresh_frame_flags;
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
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
  cm->current_frame.refresh_frame_flags = (1 << cm->seq_params.ref_frames) - 1;

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
  assert(cm->cur_frame->ref_count == 1);
  for (i = 0; i < FRAME_BUFFERS; ++i) {
    // Reset all unreferenced frame buffers. We can also reset cm->cur_frame
    // because we are the sole owner of cm->cur_frame.
    if (frame_bufs[i].ref_count > 0 && &frame_bufs[i] != cm->cur_frame) {
      continue;
    }
    frame_bufs[i].order_hint = 0;
    frame_bufs[i].display_order_hint = 0;
    av1_zero(frame_bufs[i].ref_display_order_hint);
    av1_zero(frame_bufs[i].ref_order_hints);
  }
  av1_zero_unused_internal_frame_buffers(&cm->buffer_pool->int_frame_buffers);
  unlock_buffer_pool(cm->buffer_pool);
}

static INLINE int get_disp_order_hint(AV1_COMMON *const cm) {
  CurrentFrame *const current_frame = &cm->current_frame;
  if (current_frame->frame_type == KEY_FRAME && cm->show_existing_frame)
    return 0;

  // For key frames, the implicit derivation of display_order_hit is not
  // applied.
  if (current_frame->frame_type == KEY_FRAME) return current_frame->order_hint;
  // Derive the exact display order hint from the signaled order_hint.
  // This requires scaling up order_hints corresponding to frame
  // numbers that exceed the number of bits available to send the order_hints.

  // Find the reference frame with the largest order_hint
  int max_disp_order_hint = 0;
#if CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS
  int max_layer_id = 0;
  int ref_order_hint = 0;
#endif  // CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS
  for (int map_idx = 0; map_idx < cm->seq_params.ref_frames; map_idx++) {
    // Get reference frame buffer
    const RefCntBuffer *const buf = cm->ref_frame_map[map_idx];
    if (buf == NULL
#if CONFIG_MULTILAYER_CORE && CONFIG_MULTILAYER_CORE_HLS
        || !is_tlayer_scalable_and_dependent(&cm->seq_params, cm->tlayer_id,
                                             buf->temporal_layer_id) ||
        !is_mlayer_scalable_and_dependent(&cm->seq_params, cm->mlayer_id,
                                          buf->layer_id)
#else
        || buf->temporal_layer_id > (unsigned int)cm->tlayer_id
#if CONFIG_MULTILAYER_CORE
        || buf->layer_id > current_frame->layer_id
#endif  // CONFIG_MULTILAYER_CORE
#endif  // CONFIG_MULTILAYER_CORE && CONFIG_MULTILAYER_CORE_HLS
    )
      continue;
#if CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS
    // the equality in the following comparisons is needed to properly update
    // max_layer_id and ref_order_hint
    if ((int)buf->display_order_hint >= max_disp_order_hint) {
      max_disp_order_hint = buf->display_order_hint;
      if (buf->layer_id >= max_layer_id) {
        max_layer_id = buf->layer_id;
        ref_order_hint = (int)buf->order_hint;
      }
    }
#else
    if ((int)buf->display_order_hint > max_disp_order_hint)
      max_disp_order_hint = buf->display_order_hint;
#endif  // CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS
  }

  int cur_disp_order_hint = current_frame->order_hint;
#if CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS
  if (max_layer_id < current_frame->layer_id &&
      cur_disp_order_hint == ref_order_hint) {
    return max_disp_order_hint;
  }
#endif  // CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS

  int display_order_hint_factor =
      1 << (cm->seq_params.order_hint_info.order_hint_bits_minus_1 + 1);

  while (abs(max_disp_order_hint - cur_disp_order_hint) >=
         (display_order_hint_factor >> 1)) {
    if (cur_disp_order_hint > max_disp_order_hint) return cur_disp_order_hint;
    cur_disp_order_hint += display_order_hint_factor;
  }
  // We restrict the derived display order hint to a range, to avoid 32 bit
  // integer overflow and some corner cases when display order hint operations
  // are performed in DISPLAY_ORDER_HINT_BITS bit range
  if (cur_disp_order_hint >= (1 << (DISPLAY_ORDER_HINT_BITS - 1)))
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Derived display order hint is invalid");
  return cur_disp_order_hint;
}

#if !CONFIG_F322_OBUER_ERM
static INLINE int get_ref_frame_disp_order_hint(AV1_COMMON *const cm,
                                                const RefCntBuffer *const buf) {
  // Find the reference frame with the largest order_hint
  int max_disp_order_hint = 0;
#if CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS
  int max_layer_id = 0;
  int ref_order_hint = 0;
#endif  // CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS
  for (int map_idx = 0; map_idx < INTER_REFS_PER_FRAME; map_idx++) {
#if CONFIG_MULTILAYER_CORE && CONFIG_MULTILAYER_CORE_HLS
    if (!is_tlayer_scalable_and_dependent(&cm->seq_params, cm->tlayer_id,
                                          buf->temporal_layer_id) ||
        !is_mlayer_scalable_and_dependent(
            &cm->seq_params, cm->current_frame.layer_id, buf->layer_id))
      continue;
#else
    if (buf->temporal_layer_id > (unsigned int)cm->tlayer_id) continue;
#if CONFIG_MULTILAYER_CORE
    if (buf->layer_id > cm->current_frame.layer_id) continue;
    // the equality in the following comparisons is needed to properly update
    // max_layer_id and ref_order_hint
    if ((int)buf->ref_display_order_hint[map_idx] >= max_disp_order_hint) {
      max_disp_order_hint = buf->ref_display_order_hint[map_idx];
      if (buf->ref_layer_ids[map_idx] >= max_layer_id) {
        max_layer_id = buf->ref_layer_ids[map_idx];
        ref_order_hint = (int)buf->ref_order_hints[map_idx];
      }
    }
#endif  // CONFIG_MULTILAYER_CORE
#endif  // CONFIG_MULTILAYER_CORE && CONFIG_MULTILAYER_CORE_HLS
    if ((int)buf->ref_display_order_hint[map_idx] > max_disp_order_hint)
      max_disp_order_hint = buf->ref_display_order_hint[map_idx];
  }

  const int display_order_hint_factor =
      1 << (cm->seq_params.order_hint_info.order_hint_bits_minus_1 + 1);
  int disp_order_hint = buf->order_hint;
#if CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS
  if (max_layer_id < buf->layer_id && disp_order_hint == ref_order_hint) {
    return max_disp_order_hint;
  }
#endif  // CONFIG_MULTILAYER_CORE && !CONFIG_MULTILAYER_CORE_HLS

  while (abs(max_disp_order_hint - disp_order_hint) >=
         (display_order_hint_factor >> 1)) {
    if (disp_order_hint > max_disp_order_hint) return disp_order_hint;

    disp_order_hint += display_order_hint_factor;
  }
  // We restrict the derived display order hint to a range, to avoid 32 bit
  // integer overflow and some corner cases when display order hint operations
  // are performed in DISPLAY_ORDER_HINT_BITS bit range
  if (disp_order_hint >= (1 << (DISPLAY_ORDER_HINT_BITS - 1)))
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Derived display order hint is invalid");
  return disp_order_hint;
}
#endif  // CONFIG_F322_OBUER_ERM

static INLINE void read_screen_content_params(AV1_COMMON *const cm,
                                              struct aom_read_bit_buffer *rb) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  FeatureFlags *const features = &cm->features;

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
}

static void set_primary_ref_frame_and_ctx(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;

  if (!seq_params->single_picture_hdr_flag) {
    int tmp_ref_frame[2] = { 0 };
    choose_primary_secondary_ref_frame(cm, tmp_ref_frame);
    features->derived_primary_ref_frame = tmp_ref_frame[0];
    features->derived_secondary_ref_frame = tmp_ref_frame[1];

    if (!pbi->signal_primary_ref_frame) {
      features->primary_ref_frame = features->derived_primary_ref_frame;
    }
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

  if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
    // use the default frame context values
    av1_setup_past_independence(cm);
  } else {
    *cm->fc = get_primary_ref_frame_buf(cm, cm->features.primary_ref_frame)
                  ->frame_context;
  }

  if (!cm->fc->initialized) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Uninitialized entropy context.");
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
}

static void read_frame_max_drl_bits(AV1_COMMON *const cm,
                                    struct aom_read_bit_buffer *rb) {
  FeatureFlags *const features = &cm->features;
  const SequenceHeader *const seq_params = &cm->seq_params;
  features->max_drl_bits = seq_params->def_max_drl_bits;
  if (seq_params->allow_frame_max_drl_bits) {
    features->max_drl_bits =
        aom_rb_read_primitive_ref_quniform(
            rb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
            seq_params->def_max_drl_bits - MIN_MAX_DRL_BITS) +
        MIN_MAX_DRL_BITS;
  }
}

static void read_frame_max_bvp_drl_bits(AV1_COMMON *const cm,
                                        struct aom_read_bit_buffer *rb) {
  FeatureFlags *const features = &cm->features;
  const SequenceHeader *const seq_params = &cm->seq_params;
  features->max_bvp_drl_bits = seq_params->def_max_bvp_drl_bits;
  if (seq_params->allow_frame_max_bvp_drl_bits) {
    features->max_bvp_drl_bits =
        aom_rb_read_primitive_ref_quniform(
            rb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1,
            seq_params->def_max_bvp_drl_bits - MIN_MAX_IBC_DRL_BITS) +
        MIN_MAX_IBC_DRL_BITS;
  }
}

#if CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_SEF
static int read_show_existing_frame(AV1Decoder *pbi,
                                    struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  CurrentFrame *const current_frame = &cm->current_frame;
  BufferPool *const pool = cm->buffer_pool;
  cm->show_existing_frame = 1;
  init_bru_params(cm);
  if (pbi->sequence_header_changed) {
    aom_internal_error(
        &cm->error, AOM_CODEC_CORRUPT_FRAME,
        "New sequence header starts with a show_existing_frame.");
  }
  // Show an existing frame directly.
  const int existing_frame_idx =
      aom_rb_read_literal(rb, seq_params->ref_frames_log2);
  if (existing_frame_idx >= seq_params->ref_frames) {
    aom_internal_error(
        &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
        "Existing frame idx must be less than %d but is set to %d",
        seq_params->ref_frames, existing_frame_idx);
  }
  RefCntBuffer *const frame_to_show = cm->ref_frame_map[existing_frame_idx];
  if (frame_to_show == NULL) {
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Buffer does not contain a decoded frame");
  }
  if (seq_params->decoder_model_info_present_flag &&
      seq_params->timing_info.equal_picture_interval == 0) {
    read_temporal_point_info(cm, rb);
  }
#if !CWG_F215_CONFIG_REMOVE_FRAME_ID
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
#endif  // !CWG_F215_CONFIG_REMOVE_FRAME_ID
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
  if (raw_frame_hash.is_present) cm->cur_frame->raw_frame_hash = raw_frame_hash;
  if (grain_frame_hash.is_present)
    cm->cur_frame->grain_frame_hash = grain_frame_hash;
  unlock_buffer_pool(pool);

  cm->lf.filter_level[0] = 0;
  cm->lf.filter_level[1] = 0;
  cm->show_frame = 1;
  // It is a requirement of bitstream conformance that when
  // show_existing_frame is used to show a previous frame with
  // RefFrameType[ frame_to_show_map_idx ] equal to KEY_FRAME, that the
  // frame is output via the show_existing_frame mechanism at most once.
  if ((
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
          seq_params->enable_frame_output_order &&
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
          frame_to_show->frame_type == KEY_FRAME &&
          !frame_to_show->showable_frame && frame_to_show->frame_output_done)
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
      ||
      (!seq_params->enable_frame_output_order && !frame_to_show->showable_frame)
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
  ) {
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Buffer does not contain a showable frame");
  }
  if (pbi->reset_decoder_state) frame_to_show->showable_frame = 0;

  cm->film_grain_params = frame_to_show->film_grain_params;

  if (pbi->reset_decoder_state) {
    show_existing_frame_reset(pbi, existing_frame_idx);
  } else {
    current_frame->refresh_frame_flags = 0;
  }

  return 0;
}
#endif  // CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_SEF

#if CONFIG_FIX_OPFL_AUTO
static void read_frame_opfl_refine_type(AV1_COMMON *const cm,
                                        struct aom_read_bit_buffer *rb) {
  if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    cm->features.opfl_refine_type =
        cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_NONE ? REFINE_NONE
                                                                  : REFINE_ALL;
    return;
  }

  if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_AUTO) {
    if (aom_rb_read_bit(rb)) {
      cm->features.opfl_refine_type = REFINE_SWITCHABLE;
    } else {
      cm->features.opfl_refine_type =
          aom_rb_read_bit(rb) ? REFINE_ALL : REFINE_NONE;
    }
  } else {
    cm->features.opfl_refine_type = cm->seq_params.enable_opfl_refine;
  }
}
#endif  // CONFIG_FIX_OPFL_AUTO

#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
int ras_frame_refresh_frame_flags_derivation(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  int refresh_frame_flags = (1 << cm->seq_params.ref_frames) - 1;
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    for (int j = 0; j < cm->num_ref_key_frames; j++) {
      if (cm->ref_long_term_ids[j] == pbi->long_term_ids_in_buffer[i]) {
        refresh_frame_flags &= ~(1 << i);
      }
    }
  }
  return refresh_frame_flags;
}

void mark_reference_frames_with_long_term_ids(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    pbi->valid_for_referencing[i] = 0;
    for (int j = 0; j < cm->num_ref_key_frames; j++) {
      if (cm->ref_long_term_ids[j] == pbi->long_term_ids_in_buffer[i])
        pbi->valid_for_referencing[i] = 1;
    }
  }
}
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME

// On success, returns 0. On failure, calls aom_internal_error and does not
// return.
static int read_uncompressed_header(AV1Decoder *pbi,
#if CONFIG_F106_OBU_TILEGROUP && \
    (CONFIG_F106_OBU_SWITCH || CONFIG_F106_OBU_SEF || CONFIG_F106_OBU_TIP)
                                    OBU_TYPE obu_type,
#endif  // CONFIG_F106_OBU_TILEGROUP && (CONFIG_F106_OBU_SWITCH ||
        // CONFIG_F106_OBU_SEF || CONFIG_F106_OBU_TIP)
                                    struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;
  MACROBLOCKD *const xd = &pbi->dcb.xd;
#if !CONFIG_F322_OBUER_ERM
  BufferPool *const pool = cm->buffer_pool;
  RefCntBuffer *const frame_bufs = pool->frame_bufs;
#endif  // CONFIG_F322_OBUER_ERM
  aom_s_frame_info *sframe_info = &pbi->sframe_info;
  sframe_info->is_s_frame = 0;
  sframe_info->is_s_frame_at_altref = 0;
  init_bru_params(cm);
#if CONFIG_PARAKIT_COLLECT_DATA
  for (int i = 0; i < MAX_NUM_CTX_GROUPS; i++) {
    cm->prob_models[i].frameNumber = current_frame->frame_number;
    cm->prob_models[i].frameType = current_frame->frame_type;
    for (int j = 0; j < MAX_DIMS_CONTEXT3; j++)
      for (int k = 0; k < MAX_DIMS_CONTEXT2; k++)
        for (int l = 0; l < MAX_DIMS_CONTEXT1; l++)
          for (int h = 0; h < MAX_DIMS_CONTEXT0; h++)
            beginningFrameFlag[i][j][k][l][h] = 1;
  }
#endif

  if (!pbi->sequence_header_ready) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "No sequence header");
  }

#if CONFIG_CWG_F317
  cm->bridge_frame_info.bridge_frame_ref_idx = INVALID_IDX;
  if (cm->bridge_frame_info.is_bridge_frame) {
    cm->bridge_frame_info.bridge_frame_ref_idx =
        aom_rb_read_literal(rb, seq_params->ref_frames_log2);
  } else {
#endif  // CONFIG_CWG_F317
#if CONFIG_MULTI_FRAME_HEADER
#if CONFIG_CWG_E242_MFH_ID_UVLC
    cm->cur_mfh_id = aom_rb_read_uvlc(rb);
#else
    cm->cur_mfh_id = aom_rb_read_literal(rb, 4);
#endif  // CONFIG_CWG_E242_MFH_ID_UVLC
    if (cm->cur_mfh_id == 0) {
      int seq_header_id_in_frame_header = aom_rb_read_uvlc(rb);
      (void)seq_header_id_in_frame_header;
      cm->mfh_params[cm->cur_mfh_id].mfh_frame_width =
          seq_params->max_frame_width;
      cm->mfh_params[cm->cur_mfh_id].mfh_frame_height =
          seq_params->max_frame_height;
#if !CONFIG_CWG_F248_RENDER_SIZE
      cm->mfh_params[cm->cur_mfh_id].mfh_render_width =
          seq_params->max_frame_width;
      cm->mfh_params[cm->cur_mfh_id].mfh_render_height =
          seq_params->max_frame_height;
#endif  // !CONFIG_CWG_F248_RENDER_SIZE
      cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_update_flag = 0;
      for (int i = 0; i < 4; i++) {
        cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_level[i] = 0;
      }
      cm->mfh_valid[cm->cur_mfh_id] = true;
    } else {
      if (!cm->mfh_valid[cm->cur_mfh_id]) {
        aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                           "No multi-frame header with mfh_id %d",
                           cm->cur_mfh_id);
      }
    }
#endif  // CONFIG_MULTI_FRAME_HEADER
#if CONFIG_CWG_F317
  }
#endif  // CONFIG_CWG_F317

  if (seq_params->single_picture_hdr_flag) {
    cm->show_existing_frame = 0;
    cm->show_frame = 1;
    cm->cur_frame->showable_frame = 0;
    current_frame->frame_type = KEY_FRAME;
    if (pbi->sequence_header_changed) {
      // This is the start of a new coded video sequence.
      pbi->sequence_header_changed = 0;
      pbi->decoding_first_frame = 1;
      reset_frame_buffers(cm);
    }

#if !CONFIG_F322_OBUER_ERM
    features->error_resilient_mode = 1;
#endif  // !CONFIG_F322_OBUER_ERM

#if !CONFIG_F253_REMOVE_OUTPUTFLAG
    if (cm->seq_params.enable_frame_output_order)
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
      cm->cur_frame->frame_output_done = 0;
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
    else
      cm->cur_frame->frame_output_done = 1;
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG

  } else {
#if CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_SEF
    pbi->reset_decoder_state = 0;
    if (obu_type == OBU_SEF) {
      return read_show_existing_frame(pbi, rb);
    } else
      cm->show_existing_frame = 0;
#else  // CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_SEF
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      cm->show_existing_frame = 0;
    } else {
#endif  // CONFIG_CWG_F317
      cm->show_existing_frame = aom_rb_read_bit(rb);
#if CONFIG_CWG_F317
    }
#endif  // CONFIG_CWG_F317
    pbi->reset_decoder_state = 0;

    if (cm->show_existing_frame) {
      if (pbi->sequence_header_changed) {
        aom_internal_error(
            &cm->error, AOM_CODEC_CORRUPT_FRAME,
            "New sequence header starts with a show_existing_frame.");
      }
      // Show an existing frame directly.
      const int existing_frame_idx =
          aom_rb_read_literal(rb, seq_params->ref_frames_log2);
      if (existing_frame_idx >= seq_params->ref_frames) {
        aom_internal_error(
            &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
            "Existing frame idx must be less than %d but is set to %d",
            seq_params->ref_frames, existing_frame_idx);
      }
      RefCntBuffer *const frame_to_show = cm->ref_frame_map[existing_frame_idx];
      if (frame_to_show == NULL) {
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Buffer does not contain a decoded frame");
      }
      if (seq_params->decoder_model_info_present_flag &&
          seq_params->timing_info.equal_picture_interval == 0) {
        read_temporal_point_info(cm, rb);
      }
#if !CWG_F215_CONFIG_REMOVE_FRAME_ID
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
#endif  // !CWG_F215_CONFIG_REMOVE_FRAME_ID
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
      // It is a requirement of bitstream conformance that when
      // show_existing_frame is used to show a previous frame with
      // RefFrameType[ frame_to_show_map_idx ] equal to KEY_FRAME, that the
      // frame is output via the show_existing_frame mechanism at most once.
      if ((
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
              seq_params->enable_frame_output_order &&
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
              frame_to_show->frame_type == KEY_FRAME &&
              !frame_to_show->showable_frame &&
              frame_to_show->frame_output_done)) {
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
          || (!seq_params->enable_frame_output_order &&
              !frame_to_show->showable_frame)) {
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
            aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                               "Buffer does not contain a showable frame");
          }
          if (pbi->reset_decoder_state) frame_to_show->showable_frame = 0;

          cm->film_grain_params = frame_to_show->film_grain_params;

          if (pbi->reset_decoder_state) {
            show_existing_frame_reset(pbi, existing_frame_idx);
          } else {
            current_frame->refresh_frame_flags = 0;
          }

          return 0;
      }
#endif  // CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_SEF
#if CONFIG_F106_OBU_TILEGROUP
#if CONFIG_F106_OBU_SWITCH
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    if (obu_type == OBU_RAS_FRAME || obu_type == OBU_SWITCH) {
#else   // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    if (obu_type == OBU_SWITCH) {
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
      current_frame->frame_type = S_FRAME;
    } else
#endif  // CONFIG_F106_OBU_SWITCH
#if CONFIG_F106_OBU_TIP
        if (obu_type == OBU_TIP) {
      current_frame->frame_type = INTER_FRAME;
    } else
#endif  // CONFIG_F106_OBU_TIP
#endif  // CONFIG_F106_OBU_TILEGROUP
#if CONFIG_CWG_F317
        if (cm->bridge_frame_info.is_bridge_frame) {
      current_frame->frame_type = INTER_FRAME;
    } else {
#endif  // CONFIG_CWG_F317
      if (aom_rb_read_bit(rb)) {
        current_frame->frame_type = INTER_FRAME;
      } else {
        if (aom_rb_read_bit(rb)) {
          current_frame->frame_type = KEY_FRAME;
        } else {
#if CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_SWITCH
          current_frame->frame_type = INTRA_ONLY_FRAME;
#else
          current_frame->frame_type =
              aom_rb_read_bit(rb) ? INTRA_ONLY_FRAME : S_FRAME;
#endif  // CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_SWITCH
        }
      }
#if CONFIG_CWG_F317
    }
#endif  // CONFIG_CWG_F317
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    current_frame->long_term_id = -1;
    if (current_frame->frame_type == KEY_FRAME) {
      current_frame->long_term_id =
          aom_rb_read_literal(rb, seq_params->number_of_bits_for_lt_frame_id);
    } else if (obu_type == OBU_RAS_FRAME) {
      cm->num_ref_key_frames = aom_rb_read_literal(rb, 3);
      for (int i = 0; i < cm->num_ref_key_frames; i++) {
        cm->ref_long_term_ids[i] =
            aom_rb_read_literal(rb, seq_params->number_of_bits_for_lt_frame_id);
      }
    }
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
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

#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      cm->show_frame = 0;
    } else {
#endif  // CONFIG_CWG_F317
      cm->show_frame = aom_rb_read_bit(rb);
#if CONFIG_CWG_F317
    }
#endif  // CONFIG_CWG_F317
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
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      cm->showable_frame = 0;
    } else
#endif  // CONFIG_CWG_F317
      cm->showable_frame = current_frame->frame_type != KEY_FRAME;
    if (cm->show_frame) {
#if !CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME
      if (seq_params->decoder_model_info_present_flag &&
          seq_params->timing_info.equal_picture_interval == 0)
        read_temporal_point_info(cm, rb);
#endif  // !CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME
    } else {
#if CONFIG_CWG_F317
      if (cm->bridge_frame_info.is_bridge_frame) {
        cm->showable_frame = 0;
      } else {
#endif  // CONFIG_CWG_F317
        // See if this frame can be used as show_existing_frame in future
        cm->showable_frame = aom_rb_read_bit(rb);
#if CONFIG_CWG_F317
      }
#endif  // CONFIG_CWG_F317
    }
#if CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME
    if ((cm->show_frame || cm->showable_frame) &&
        seq_params->decoder_model_info_present_flag &&
        seq_params->timing_info.equal_picture_interval == 0)
      read_temporal_point_info(cm, rb);
#endif  // CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME
    if (
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
        seq_params->enable_frame_output_order &&
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
        current_frame->frame_type == KEY_FRAME && cm->showable_frame) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "showable_frame should be equal to 0"
#if CONFIG_F253_REMOVE_OUTPUTFLAG
                         "Frame "
#else
                           "when enable_frame_output_order is enabled and "
                           "frame "
#endif  // CONFIG_F253_REMOVE_OUTPUTFLAG
                         "type is KEY_FRAME.");
    }
    cm->cur_frame->showable_frame = cm->showable_frame;
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
    if (cm->seq_params.enable_frame_output_order)
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG
      cm->cur_frame->frame_output_done = 0;
#if !CONFIG_F253_REMOVE_OUTPUTFLAG
    else
      cm->cur_frame->frame_output_done = 1;
#endif  // !CONFIG_F253_REMOVE_OUTPUTFLAG

#if !CONFIG_F322_OBUER_ERM
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      features->error_resilient_mode = 0;
    } else {
#endif  // CONFIG_CWG_F317
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
      if ((frame_is_sframe(cm) && pbi->obu_type != OBU_RAS_FRAME) ||
          (current_frame->frame_type == KEY_FRAME && cm->show_frame)) {
        features->error_resilient_mode = 1;
      } else {
        features->error_resilient_mode = aom_rb_read_bit(rb);
      }
#else   // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    features->error_resilient_mode =
        frame_is_sframe(cm) ||
                (current_frame->frame_type == KEY_FRAME && cm->show_frame)
            ? 1
            : aom_rb_read_bit(rb);
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_CWG_F317
    }
#endif  // CONFIG_CWG_F317
#endif  // !CONFIG_F322_OBUER_ERM
  }

  av1_set_frame_sb_size(cm, cm->seq_params.sb_size);

  if (current_frame->frame_type == KEY_FRAME && cm->show_frame) {
    /* All frames need to be marked as not valid for referencing */
    for (int i = 0; i < seq_params->ref_frames; i++) {
      pbi->valid_for_referencing[i] = 0;
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
      pbi->long_term_ids_in_buffer[i] = -1;
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    }
  }

  int frame_size_override_flag = 0;
  features->allow_intrabc = 0;
  features->allow_global_intrabc = 0;
  features->allow_local_intrabc = 0;
  features->primary_ref_frame = PRIMARY_REF_NONE;

  int signal_primary_ref_frame = -1;
  features->derived_primary_ref_frame = PRIMARY_REF_NONE;
  pbi->signal_primary_ref_frame = -1;

  if (!seq_params->single_picture_hdr_flag) {
#if !CWG_F215_CONFIG_REMOVE_FRAME_ID
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
#if CONFIG_CWG_F317
      if (cm->bridge_frame_info.is_bridge_frame) {
        const int ref_frame =
            cm->bridge_frame_info.bridge_frame_ref_idx_remapped;
        assert(!is_tip_ref_frame(
            ref_frame));  // TIP frame reference is not allowed
        const int map_idx = get_ref_frame_map_idx(cm, ref_frame);
        cm->current_frame_id = cm->ref_frame_id[map_idx];
      } else {
#endif  // CONFIG_CWG_F317
        cm->current_frame_id = aom_rb_read_literal(rb, frame_id_length);
#if CONFIG_CWG_F317
      }
#endif  // CONFIG_CWG_F317

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
      for (int i = 0; i < seq_params->ref_frames; i++) {
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
#endif  // !CWG_F215_CONFIG_REMOVE_FRAME_ID

#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      frame_size_override_flag = 1;
    } else {
#endif  // CONFIG_CWG_F317
      frame_size_override_flag = frame_is_sframe(cm) ? 1 : aom_rb_read_bit(rb);
      current_frame->order_hint = aom_rb_read_literal(
          rb, seq_params->order_hint_info.order_hint_bits_minus_1 + 1);

      current_frame->display_order_hint = get_disp_order_hint(cm);
      current_frame->frame_number = current_frame->order_hint;
#if CONFIG_CWG_F317
    }
#endif  // CONFIG_CWG_F317

    if (
#if CONFIG_F322_OBUER_ERM
        !frame_is_sframe(cm)
#else
          !features->error_resilient_mode
#endif
        && !frame_is_intra_only(cm)) {
#if CONFIG_CWG_F317
      if (!cm->bridge_frame_info.is_bridge_frame) {
#endif  // CONFIG_CWG_F317
        signal_primary_ref_frame = aom_rb_read_bit(rb);
        pbi->signal_primary_ref_frame = signal_primary_ref_frame;
        if (signal_primary_ref_frame)
          features->primary_ref_frame =
              aom_rb_read_literal(rb, PRIMARY_REF_BITS);
#if CONFIG_CWG_F317
      }
#endif  // CONFIG_CWG_F317
    }
  }

#if !CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING
  if (seq_params->decoder_model_info_present_flag) {
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      cm->buffer_removal_time_present = 0;
    } else {
#endif  // CONFIG_CWG_F317
      cm->buffer_removal_time_present = aom_rb_read_bit(rb);
#if CONFIG_CWG_F317
    }
#endif  // CONFIG_CWG_F317
    if (cm->buffer_removal_time_present) {
      for (int op_num = 0;
           op_num < seq_params->operating_points_cnt_minus_1 + 1; op_num++) {
        if (seq_params->op_params[op_num].decoder_model_param_present_flag) {
          if ((((seq_params->operating_point_idc[op_num] >> cm->tlayer_id) &
                0x1) &&
               ((seq_params->operating_point_idc[op_num] >>
                 (cm->mlayer_id + MAX_NUM_TLAYERS)) &
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
#endif  // CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING
  if (current_frame->frame_type == KEY_FRAME) {
    if (!cm->show_frame) {  // unshown keyframe (forward keyframe)
      current_frame->refresh_frame_flags =
          aom_rb_read_literal(rb, seq_params->ref_frames);
    } else {  // shown keyframe
      current_frame->refresh_frame_flags = (1 << seq_params->ref_frames) - 1;
    }

    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      cm->remapped_ref_idx[i] = INVALID_IDX;
    }
    if (pbi->need_resync) {
      reset_ref_frame_map(cm);
      pbi->need_resync = 0;
    }
  } else {
    const int short_refresh_frame_flags =
        cm->seq_params.enable_short_refresh_frame_flags &&
#if CONFIG_F322_OBUER_ERM
        !frame_is_sframe(cm)
#else
          !cm->features.error_resilient_mode
#endif
        ;
    const int refresh_frame_flags_bits = short_refresh_frame_flags
                                             ? seq_params->ref_frames_log2
                                             : seq_params->ref_frames;

    if (current_frame->frame_type == INTRA_ONLY_FRAME) {
      if (short_refresh_frame_flags) {
#if CONFIG_CWG_F260_REFRESH_FLAG
        const bool has_refresh_frame_flags = aom_rb_read_bit(rb);
        if (has_refresh_frame_flags) {
          const int refresh_idx =
              aom_rb_read_literal(rb, refresh_frame_flags_bits);
          if (refresh_idx >= seq_params->ref_frames) {
            aom_internal_error(
                &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                "refresh_idx must be less than %d but is set to %d",
                seq_params->ref_frames, refresh_idx);
          }
          current_frame->refresh_frame_flags = 1 << refresh_idx;
        } else {
          current_frame->refresh_frame_flags = 0;
        }
#else
          const int refresh_idx =
              aom_rb_read_literal(rb, refresh_frame_flags_bits);
          if (refresh_idx >= seq_params->ref_frames) {
            aom_internal_error(
                &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                "refresh_idx must be less than %d but is set to %d",
                seq_params->ref_frames, refresh_idx);
          }
          if (refresh_idx == 0) {
            const bool has_refresh_frame_flags = aom_rb_read_bit(rb);
            current_frame->refresh_frame_flags =
                has_refresh_frame_flags ? 1 : 0;
          } else {
            current_frame->refresh_frame_flags = 1 << refresh_idx;
          }
#endif  // CONFIG_CWG_F260_REFRESH_FLAG
      } else {
        current_frame->refresh_frame_flags =
            aom_rb_read_literal(rb, refresh_frame_flags_bits);
      }
      assert(seq_params->ref_frames >= 1);
      if (seq_params->ref_frames > 1 &&
          current_frame->refresh_frame_flags ==
              ((1 << seq_params->ref_frames) - 1)) {
        aom_internal_error(
            &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
            "Intra only frames cannot refresh all the available entries in "
            "the decoded picture buffer (DPB) if the DPB size is larger than "
            "1. A key-frame should be inserted to refresh all entries.");
      }
      if (pbi->need_resync) {
        reset_ref_frame_map(cm);
        pbi->need_resync = 0;
      }
    } else if (pbi->need_resync != 1) { /* Skip if need resync */
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
      if (pbi->obu_type == OBU_RAS_FRAME) {
        current_frame->refresh_frame_flags =
            ras_frame_refresh_frame_flags_derivation(pbi);
      } else if (frame_is_sframe(cm)) {
        current_frame->refresh_frame_flags =
            aom_rb_read_literal(rb, seq_params->ref_frames);
      } else {
#else   // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
        if (frame_is_sframe(cm)) {
          current_frame->refresh_frame_flags =
              ((1 << seq_params->ref_frames) - 1);
        } else {
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_CWG_F317
        if (cm->bridge_frame_info.is_bridge_frame) {
          cm->bridge_frame_info.bridge_frame_overwrite_flag =
              aom_rb_read_bit(rb);
          if (!cm->bridge_frame_info.bridge_frame_overwrite_flag) {
            current_frame->refresh_frame_flags =
                1 << cm->bridge_frame_info.bridge_frame_ref_idx;
          }
        }

        if (!cm->bridge_frame_info.is_bridge_frame ||
            cm->bridge_frame_info.bridge_frame_overwrite_flag) {
#endif  // CONFIG_CWG_F317
          if (short_refresh_frame_flags) {
#if CONFIG_CWG_F260_REFRESH_FLAG
            const bool has_refresh_frame_flags = aom_rb_read_bit(rb);
            if (has_refresh_frame_flags) {
              const int refresh_idx =
                  aom_rb_read_literal(rb, refresh_frame_flags_bits);
              if (refresh_idx >= seq_params->ref_frames) {
                aom_internal_error(
                    &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                    "refresh_idx must be less than %d but is set to %d",
                    seq_params->ref_frames, refresh_idx);
              }
              current_frame->refresh_frame_flags = 1 << refresh_idx;
            } else {
              current_frame->refresh_frame_flags = 0;
            }
#else
            const int refresh_idx =
                aom_rb_read_literal(rb, refresh_frame_flags_bits);
            if (refresh_idx >= seq_params->ref_frames) {
              aom_internal_error(
                  &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                  "refresh_idx must be less than %d but is set to %d",
                  seq_params->ref_frames, refresh_idx);
            }
            if (refresh_idx == 0) {
              const bool has_refresh_frame_flags = aom_rb_read_bit(rb);
              current_frame->refresh_frame_flags =
                  has_refresh_frame_flags ? 1 : 0;
            } else {
              current_frame->refresh_frame_flags = 1 << refresh_idx;
            }
#endif  // CONFIG_CWG_F260_REFRESH_FLAG
          } else {
            current_frame->refresh_frame_flags =
                aom_rb_read_literal(rb, refresh_frame_flags_bits);
          }
#if CONFIG_CWG_F317
        }  // CONFIG_CWG_F317
#endif
      }
    }
  }

#if CONFIG_F322_OBUER_ERM
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  if (pbi->obu_type == OBU_RAS_FRAME) {
    mark_reference_frames_with_long_term_ids(pbi);
  }
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#else
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    if (pbi->obu_type == OBU_RAS_FRAME) {
      mark_reference_frames_with_long_term_ids(pbi);
    } else if (!frame_is_intra_only(cm) ||
               current_frame->refresh_frame_flags !=
                   ((1 << seq_params->ref_frames) - 1))
#else   // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  if (!frame_is_intra_only(cm) ||
      current_frame->refresh_frame_flags != ((1 << seq_params->ref_frames) - 1))
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    {
      // Read all ref frame order hints if error_resilient_mode == 1
      if (features->error_resilient_mode
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
          && seq_params->order_hint_info.enable_order_hint
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
      ) {
        for (int ref_idx = 0; ref_idx < seq_params->ref_frames; ref_idx++) {
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
            check_ref_count_status_dec(pbi);
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
                    pool->get_fb_cb, pool->cb_priv, false)) {
              decrease_ref_count(buf, pool);
              unlock_buffer_pool(pool);
              aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                                 "Failed to allocate frame buffer");
            }
            unlock_buffer_pool(pool);
            // According to the specification, valid bitstreams are required to
            // never use missing reference frames so the filling process for
            // missing frames is not normatively defined and RefValid for
            // missing frames is set to 0.

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
            buf->display_order_hint = get_ref_frame_disp_order_hint(cm, buf);
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
            buf->long_term_id = -1;
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
          }
        }
      }
      if (features->error_resilient_mode) {
        // Read all ref frame base_qindex
        for (int ref_idx = 0; ref_idx < seq_params->ref_frames; ref_idx++) {
          RefCntBuffer *buf = cm->ref_frame_map[ref_idx];
          buf->base_qindex = aom_rb_read_literal(
              rb, cm->seq_params.bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT
                                                         : QINDEX_BITS);
        }
      }
    }
#endif  // CONFIG_F322_OBUER_ERM

  features->allow_lf_sub_pu = 0;
  if (current_frame->frame_type == KEY_FRAME) {
    cm->current_frame.pyramid_level = 1;
    cm->current_frame.temporal_layer_id = cm->tlayer_id;
#if CONFIG_MULTILAYER_CORE
    cm->current_frame.layer_id = cm->mlayer_id;
#endif  // CONFIG_MULTILAYER_CORE

    features->tip_frame_mode = TIP_FRAME_DISABLED;
    setup_frame_size(cm, frame_size_override_flag, rb);
    read_screen_content_params(cm, rb);
    features->allow_intrabc = aom_rb_read_bit(rb);
    if (features->allow_intrabc) {
      features->allow_global_intrabc = aom_rb_read_bit(rb);
      features->allow_local_intrabc =
          features->allow_global_intrabc ? aom_rb_read_bit(rb) : 1;
      read_frame_max_bvp_drl_bits(cm, rb);
    }

    features->allow_ref_frame_mvs = 0;
    cm->prev_frame = NULL;

    cm->cur_frame->num_ref_frames = 0;
  } else {
    cm->current_frame.temporal_layer_id = cm->tlayer_id;
#if CONFIG_MULTILAYER_CORE
    cm->current_frame.layer_id = cm->mlayer_id;
#endif  // CONFIG_MULTILAYER_CORE
    features->allow_ref_frame_mvs = 0;
    features->tip_frame_mode = TIP_FRAME_DISABLED;
    if (current_frame->frame_type == INTRA_ONLY_FRAME) {
      cm->cur_frame->film_grain_params_present =
          seq_params->film_grain_params_present;
      setup_frame_size(cm, frame_size_override_flag, rb);
      read_screen_content_params(cm, rb);
      features->allow_intrabc = aom_rb_read_bit(rb);
      if (features->allow_intrabc) {
        features->allow_global_intrabc = aom_rb_read_bit(rb);
        features->allow_local_intrabc =
            features->allow_global_intrabc ? aom_rb_read_bit(rb) : 1;
        read_frame_max_bvp_drl_bits(cm, rb);
      }

      cm->cur_frame->num_ref_frames = 0;

    } else if (pbi->need_resync != 1) { /* Skip if need resync */
      // Implicitly derive the reference mapping
      init_ref_map_pair(cm, cm->ref_frame_map_pairs,
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                        current_frame->frame_type == KEY_FRAME,
                        pbi->obu_type == OBU_RAS_FRAME);
#else   // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                          current_frame->frame_type == KEY_FRAME);
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_CWG_F317
      if (cm->bridge_frame_info.is_bridge_frame) {
        current_frame->order_hint =
            cm->ref_frame_map[cm->bridge_frame_info.bridge_frame_ref_idx]
                ->order_hint;
        current_frame->display_order_hint =
            cm->ref_frame_map[cm->bridge_frame_info.bridge_frame_ref_idx]
                ->display_order_hint;
        current_frame->frame_number =
            cm->ref_frame_map[cm->bridge_frame_info.bridge_frame_ref_idx]
                ->order_hint;
      }
#endif  // CONFIG_CWG_F317
#if CONFIG_ACROSS_SCALE_REF_OPT
      // For implicit reference mode, the reference mapping is derived without
      // considering the resolution first. Later, setup_frame_size_with_refs
      // uses the reference information to obtain the resolution.
      av1_get_ref_frames(cm, current_frame->display_order_hint, 0,
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                         0,
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                         cm->ref_frame_map_pairs);
#else
        av1_get_ref_frames(cm, current_frame->display_order_hint,
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                           0,
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                           cm->ref_frame_map_pairs);
#endif  // CONFIG_ACROSS_SCALE_REF_OPT

      // Reference rankings will be implicitly derived in av1_get_ref_frames,
      // but if the explicit mode is used, reference indices will be signaled,
      // which overwrites the implictly derived ones.
#if CONFIG_F322_OBUER_EXPLICIT_REFLIST
      int explicit_ref_frame_map = -1;
      // explicit_ref_frame_map is always true when frame_type is s_frame.
      // explicit_ref_frame_map is always false when frame_type is key_frame or
      // intra_only frame. explicit_ref_frame_map is always false when
      // seq_params->explicit_ref_frame_map is false and frame_type is not
      // s_frame.
      if (frame_is_sframe(cm))
        explicit_ref_frame_map = 1;
      else if (frame_is_intra_only(cm))
        explicit_ref_frame_map = 0;
#if CONFIG_CWG_F317
      else if (cm->bridge_frame_info.is_bridge_frame)
        explicit_ref_frame_map = 0;
#endif  // CONFIG_CWG_F317
      else {
        if (seq_params->explicit_ref_frame_map)
          explicit_ref_frame_map = aom_rb_read_bit(rb);
        else
          explicit_ref_frame_map = 0;
      }
#else
        const int explicit_ref_frame_map =
#if CONFIG_CWG_F317
            (
#endif  // CONFIG_CWG_F317
                cm->features.error_resilient_mode || frame_is_sframe(cm) ||
                seq_params->explicit_ref_frame_map
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
                || !seq_params->order_hint_info.enable_order_hint
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
#if CONFIG_CWG_F317
                ) &&
            !cm->bridge_frame_info.is_bridge_frame
#endif  // CONFIG_CWG_F317
            ;
#endif  // CONFIG_F322_OBUER_EXPLICIT_REFLIST

      if (explicit_ref_frame_map) {
        cm->ref_frames_info.num_total_refs =
            aom_rb_read_literal(rb, MAX_REFS_PER_FRAME_LOG2);
#if CONFIG_CWG_F168_DPB_HLS
        const int max_num_ref_frames =
            AOMMIN(seq_params->ref_frames, INTER_REFS_PER_FRAME);
#endif  // CONFIG_CWG_F168_DPB_HLS
        // Check whether num_total_refs read is valid
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
        if (current_frame->frame_type != S_FRAME)
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_ACROSS_SCALE_REF_OPT
          if (cm->ref_frames_info.num_total_refs < 0 ||
#else
          if (cm->ref_frames_info.num_total_refs <= 0 ||
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
              cm->ref_frames_info.num_total_refs >
#if CONFIG_CWG_F168_DPB_HLS
                  max_num_ref_frames)
#else
                  seq_params->max_reference_frames)
#endif  // CONFIG_CWG_F168_DPB_HLS
            aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                               "Invalid num_total_refs");
#if CONFIG_ACROSS_SCALE_REF_OPT
        for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i) {
          int ref = aom_rb_read_literal(rb, seq_params->ref_frames_log2);
          if (ref >= seq_params->ref_frames) {
            aom_internal_error(
                &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                "Explicit ref frame idx must be less than %d but is set to %d",
                seq_params->ref_frames, ref);
          }

          // Most of the time, streams start with a keyframe. In that case,
          // ref_frame_map will have been filled in at that point and will not
          // contain any NULLs. However, streams are explicitly allowed to start
          // with an intra-only frame, so long as they don't then signal a
          // reference to a slot that hasn't been set yet. That's what we are
          // checking here.
          if (cm->ref_frame_map[ref] == NULL)
            aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                               "Inter frame requests nonexistent reference");
#if CONFIG_MULTILAYER_CORE_HLS
          // mlayer and tlayer scalability related bitstream constraints for the
          // explicit reference frame signaling
          const int cur_mlayer_id = current_frame->layer_id;
          const int ref_mlayer_id = cm->ref_frame_map[ref]->layer_id;
          if (!is_mlayer_scalable_and_dependent(seq_params, cur_mlayer_id,
                                                ref_mlayer_id)) {
            aom_internal_error(
                &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                "Unsupported bitstream: embedded layer scalability shall be "
                "maintained in explicit reference map signaling.");
          }
          const int cur_tlayer_id = current_frame->temporal_layer_id;
          const int ref_tlayer_id = cm->ref_frame_map[ref]->temporal_layer_id;
          if (!is_tlayer_scalable_and_dependent(seq_params, cur_tlayer_id,
                                                ref_tlayer_id)) {
            aom_internal_error(
                &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                "Unsupported bitstream: temporal layer scalability shall be "
                "maintained in explicit reference map signaling.");
          }
#endif  // CONFIG_MULTILAYER_CORE_HLS
          cm->remapped_ref_idx[i] = ref;
        }
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
      }
#if CONFIG_ACROSS_SCALE_REF_OPT
#if CONFIG_CWG_F317
      if (
#if CONFIG_F322_OBUER_ERM
          !frame_is_sframe(cm)
#else
          !features->error_resilient_mode
#endif  // CONFIG_F322_OBUER_ERM
          && frame_size_override_flag &&
          !cm->bridge_frame_info.is_bridge_frame) {
#else
      if (!features->error_resilient_mode && frame_size_override_flag) {
#endif  // CONFIG_CWG_F317
        setup_frame_size_with_refs(cm, explicit_ref_frame_map, rb);
      } else {
        setup_frame_size(cm, frame_size_override_flag, rb);
      }

      // Overwrite the reference mapping considering the resolution
      if (!explicit_ref_frame_map) {
        av1_get_ref_frames(cm, current_frame->display_order_hint, 1,
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                           0,
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                           cm->ref_frame_map_pairs);
      }

      // Check to make sure all reference frames have valid dimensions.
      for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i) {
        const RefCntBuffer *const ref_frame = get_ref_frame_buf(cm, i);
        int valid_ref_frame = valid_ref_frame_size(ref_frame->buf.y_crop_width,
                                                   ref_frame->buf.y_crop_height,
                                                   cm->width, cm->height);
        if (!valid_ref_frame)
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Referenced frame has invalid size");
      }
#if CONFIG_CWG_F317
      if (cm->bridge_frame_info.is_bridge_frame) {
        assert(cm->ref_frames_info.num_total_refs == 1);
      }
#endif  // CONFIG_CWG_F317
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
#if CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_TIP
      if (obu_type != OBU_TIP && current_frame->frame_type == INTER_FRAME)
#else
        if (current_frame->frame_type == INTER_FRAME)
#endif
      {
        setup_bru_active_info(cm, rb);
        if (cm->bru.enabled) {
          const RefCntBuffer *const bru_ref =
              get_ref_frame_buf(cm, cm->bru.update_ref_idx);
          if (bru_ref == NULL) {
            aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                               "Invalid BRU Ref: invalid reference buffer");
          } else {
            if (bru_ref->width != cm->width || bru_ref->height != cm->height) {
              aom_internal_error(
                  &cm->error, AOM_CODEC_CORRUPT_FRAME,
                  "BRU reference frame size is not the same as current frame");
            }
          }
        }
      }
#if CONFIG_CWG_F317
      if (cm->bru.frame_inactive_flag ||
          cm->bridge_frame_info.is_bridge_frame) {
#else
        if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
        cm->features.disable_cdf_update = 1;
      }
      cm->ref_frames_info.num_same_ref_compound =
          AOMMIN(cm->seq_params.num_same_ref_compound,
                 cm->ref_frames_info.num_total_refs);
      if (features->primary_ref_frame >= cm->ref_frames_info.num_total_refs &&
          features->primary_ref_frame != PRIMARY_REF_NONE) {
        aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                           "Invalid primary_ref_frame");
      }
      for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i) {
        int ref = 0;
        if (!explicit_ref_frame_map) {
#if CONFIG_CWG_F317
          if (cm->bridge_frame_info.is_bridge_frame) {
            const int ref_frame =
                cm->bridge_frame_info.bridge_frame_ref_idx_remapped;
            assert(!is_tip_ref_frame(
                ref_frame));  // TIP frame reference is not allowed
            ref = get_ref_frame_map_idx(cm, ref_frame);
          } else {
#endif  // CONFIG_CWG_F317
            ref = cm->remapped_ref_idx[i];
            if (cm->ref_frame_map[ref] == NULL)
              aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                                 "Inter frame requests nonexistent reference");
#if CONFIG_CWG_F317
          }
#endif  // CONFIG_CWG_F317
        } else {
#if CONFIG_ACROSS_SCALE_REF_OPT
          ref = cm->remapped_ref_idx[i];
#else
            ref = aom_rb_read_literal(rb, seq_params->ref_frames_log2);
            if (ref >= seq_params->ref_frames) {
              aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                                 "Explicit ref frame idx must be less than %d "
                                 "but is set to %d",
                                 seq_params->ref_frames, ref);
            }

            // Most of the time, streams start with a keyframe. In that case,
            // ref_frame_map will have been filled in at that point and will not
            // contain any NULLs. However, streams are explicitly allowed to
            // start with an intra-only frame, so long as they don't then signal
            // a reference to a slot that hasn't been set yet. That's what we
            // are checking here.
            if (cm->ref_frame_map[ref] == NULL)
              aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                                 "Inter frame requests nonexistent reference");
#if CONFIG_MULTILAYER_CORE_HLS
            // mlayer and tlayer scalability related bitstream constraints for
            // the explicit reference frame signaling
            const int cur_mlayer_id = current_frame->layer_id;
            const int ref_mlayer_id = cm->ref_frame_map[ref]->layer_id;
            if (!is_mlayer_scalable_and_dependent(seq_params, cur_mlayer_id,
                                                  ref_mlayer_id)) {
              aom_internal_error(
                  &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                  "Unsupported bitstream: embedded layer scalability shall be "
                  "maintained in explicit reference map signaling.");
            }
            const int cur_tlayer_id = current_frame->temporal_layer_id;
            const int ref_tlayer_id = cm->ref_frame_map[ref]->temporal_layer_id;
            if (!is_tlayer_scalable_and_dependent(seq_params, cur_tlayer_id,
                                                  ref_tlayer_id)) {
              aom_internal_error(
                  &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                  "Unsupported bitstream: temporal layer scalability shall be "
                  "maintained in explicit reference map signaling.");
            }
#endif  // CONFIG_MULTILAYER_CORE_HLS
            cm->remapped_ref_idx[i] = ref;
#endif  // CONFIG_ACROSS_SCALE_REF_OPT
        }
        // find corresponding bru ref idx given explicit_bru_idx
        if (cm->bru.enabled && cm->bru.update_ref_idx == i) {
          cm->bru.ref_disp_order = cm->ref_frame_map[ref]->display_order_hint;
          cm->bru.explicit_ref_idx = ref;
        }
        // Check valid for referencing
        if (pbi->valid_for_referencing[ref] == 0)
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Reference frame not valid for referencing");

#if !CWG_F215_CONFIG_REMOVE_FRAME_ID
#if CONFIG_CWG_F317
        if (seq_params->frame_id_numbers_present_flag &&
            !cm->bridge_frame_info.is_bridge_frame) {
#else
        if (seq_params->frame_id_numbers_present_flag) {
#endif  // CONFIG_CWG_F317
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
#endif  // !CWG_F215_CONFIG_REMOVE_FRAME_ID
      }
      if (cm->bru.update_ref_idx != -1) {
        for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i) {
          if (cm->bru.update_ref_idx != i) {
            if (cm->ref_frame_map[cm->bru.explicit_ref_idx] ==
                cm->ref_frame_map[cm->remapped_ref_idx[i]]) {
              aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                                 "Only one reference can be updated for BRU");
            }
          }
        }
      }
      if (cm->bru.enabled) {
        int n_future = 0;
        int cur_frame_disp = (int)current_frame->display_order_hint;
        for (int i = 0; i < cm->seq_params.ref_frames; i++) {
          const RefCntBuffer *const buf = cm->ref_frame_map[i];
          if (buf) {
            int ref_disp = (int)buf->display_order_hint;
            const int disp_diff = get_relative_dist(
                &cm->seq_params.order_hint_info, cur_frame_disp, ref_disp);
            if (disp_diff < 0) {
              n_future++;
              break;
            }
          }
        }
        if (n_future > 0) {
          aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                             "BRU can only use in LD");
        }
      }
      // With explicit_ref_frame_map, cm->remapped_ref_idx has been
      // overwritten. The reference lists also needs to be reset.
      if (explicit_ref_frame_map) {
        RefScoreData scores[REF_FRAMES];
        for (int i = 0; i < seq_params->ref_frames; i++)
          scores[i].score = INT_MAX;
        for (int i = 0; i < cm->ref_frames_info.num_total_refs; i++) {
          scores[i].score = i;
          int ref = cm->remapped_ref_idx[i];
          scores[i].distance =
#if CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
              get_relative_dist(&seq_params->order_hint_info,
                                (int)current_frame->display_order_hint,
                                (int)cm->ref_frame_map_pairs[ref].disp_order);
#else
                seq_params->order_hint_info.enable_order_hint
                    ? get_relative_dist(
                          &seq_params->order_hint_info,
                          (int)current_frame->display_order_hint,
                          (int)cm->ref_frame_map_pairs[ref].disp_order)
                    : 1;
#endif  // CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
          cm->ref_frames_info.ref_frame_distance[i] = scores[i].distance;
        }
        av1_get_past_future_cur_ref_lists(cm, scores);
      }
      cm->cur_frame->num_ref_frames = cm->ref_frames_info.num_total_refs;

#if !CONFIG_ACROSS_SCALE_REF_OPT
#if CONFIG_CWG_F317
      if (
#if CONFIG_F322_OBUER_ERM
          !frame_is_sframe(cm)
#else
          !features->error_resilient_mode
#endif  // CONFIG_CWG_F317
          && frame_size_override_flag &&
          !cm->bridge_frame_info.is_bridge_frame) {
#else
      if (!features->error_resilient_mode && frame_size_override_flag) {
#endif  // CONFIG_CWG_F317
        setup_frame_size_with_refs(cm, rb);
      } else {
        setup_frame_size(cm, frame_size_override_flag, rb);
      }
#endif  // !CONFIG_ACROSS_SCALE_REF_OPT

      if (frame_might_allow_ref_frame_mvs(cm))
        features->allow_ref_frame_mvs = aom_rb_read_bit(rb);
      else
        features->allow_ref_frame_mvs = 0;

      if (features->allow_ref_frame_mvs &&
          cm->ref_frames_info.num_total_refs > 1
#if !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
          && seq_params->order_hint_info.enable_order_hint
#endif  // !CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT
      ) {
        // Get the TMVP sampling mode
        cm->tmvp_sample_step = aom_rb_read_bit(rb) + 1;
        cm->tmvp_sample_stepl2 = cm->tmvp_sample_step == 1 ? 0 : 1;
      } else {
        cm->tmvp_sample_step = 1;
        cm->tmvp_sample_stepl2 = 0;
      }

      if (cm->seq_params.enable_lf_sub_pu) {
#if CONFIG_CWG_F317
        if (cm->bridge_frame_info.is_bridge_frame) {
          features->allow_lf_sub_pu = 0;
        } else {
#endif  // CONFIG_CWG_F317
          features->allow_lf_sub_pu = aom_rb_read_bit(rb);
#if CONFIG_CWG_F317
        }
#endif  // CONFIG_CWG_F317
      }

      cm->tip_global_motion.as_int = 0;
      cm->tip_interp_filter = MULTITAP_SHARP;
      cm->tip_global_wtd_index = 0;
      cm->has_both_sides_refs = (cm->ref_frames_info.num_future_refs > 0) &&
                                (cm->ref_frames_info.num_past_refs > 0);
      if (cm->seq_params.enable_tip && features->allow_ref_frame_mvs &&
          cm->ref_frames_info.num_total_refs >= 2 &&
#if CONFIG_CWG_F317
          !cm->bridge_frame_info.is_bridge_frame &&
#endif  // CONFIG_CWG_F317
          !cm->bru.frame_inactive_flag) {
#if CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_TIP
        if (obu_type == OBU_TIP) {
          features->tip_frame_mode = TIP_FRAME_AS_OUTPUT;
        }
#else
          if (cm->seq_params.enable_tip == 1 && aom_rb_read_bit(rb)) {
            features->tip_frame_mode = TIP_FRAME_AS_OUTPUT;
          }
#endif  // CONFIG_F106_OBU_TILEGROUP && CONFIG_F106_OBU_TIP
        else {
          features->tip_frame_mode =
              aom_rb_read_bit(rb) ? TIP_FRAME_AS_REF : TIP_FRAME_DISABLED;
        }

        if (features->tip_frame_mode >= TIP_FRAME_MODES) {
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Invalid TIP mode.");
        }

#if CONFIG_FIX_OPFL_AUTO
        read_frame_opfl_refine_type(cm, rb);
#endif  // CONFIG_FIX_OPFL_AUTO

        if (features->tip_frame_mode && cm->seq_params.enable_tip_hole_fill) {
          features->allow_tip_hole_fill = aom_rb_read_bit(rb);
        } else {
          features->allow_tip_hole_fill = false;
        }

        if (features->tip_frame_mode && is_unequal_weighted_tip_allowed(cm)) {
          cm->tip_global_wtd_index = aom_rb_read_literal(rb, 3);
        }
        if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT &&
            cm->seq_params.enable_lf_sub_pu && features->allow_lf_sub_pu) {
          cm->lf.tip_filter_level = aom_rb_read_bit(rb);
          if (cm->lf.tip_filter_level) {
            cm->lf.tip_delta = 0;
          }
        }

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

          if (aom_rb_read_bit(rb)) {
            cm->tip_interp_filter = MULTITAP_SHARP;
          } else if (aom_rb_read_bit(rb)) {
            cm->tip_interp_filter = EIGHTTAP_REGULAR;
          } else {
            cm->tip_interp_filter = EIGHTTAP_SMOOTH;
          }
        }
      } else {
        features->tip_frame_mode = TIP_FRAME_DISABLED;
#if CONFIG_FIX_OPFL_AUTO
#if CONFIG_CWG_F317
        if (!cm->bru.frame_inactive_flag &&
            !cm->bridge_frame_info.is_bridge_frame)
          read_frame_opfl_refine_type(cm, rb);
#else
        if (!cm->bru.frame_inactive_flag) read_frame_opfl_refine_type(cm, rb);
#endif
#endif  // CONFIG_FIX_OPFL_AUTO
      }

      if (features->tip_frame_mode != TIP_FRAME_AS_OUTPUT &&
#if CONFIG_CWG_F317
          !cm->bridge_frame_info.is_bridge_frame &&
#endif  // CONFIG_CWG_F317
          !cm->bru.frame_inactive_flag) {
        read_screen_content_params(cm, rb);
        features->allow_intrabc = aom_rb_read_bit(rb);
        features->allow_global_intrabc = 0;
        features->allow_local_intrabc = features->allow_intrabc;

        read_frame_max_drl_bits(cm, rb);
        if (features->allow_intrabc) {
          read_frame_max_bvp_drl_bits(cm, rb);
        }

        if (features->cur_frame_force_integer_mv) {
          features->fr_mv_precision = MV_PRECISION_ONE_PEL;
        } else {
#if CONFIG_FRAME_HALF_PRECISION
          if (aom_rb_read_bit(rb)) {
            features->fr_mv_precision = MV_PRECISION_QTR_PEL;
          } else {
            features->fr_mv_precision = aom_rb_read_bit(rb)
                                            ? MV_PRECISION_ONE_EIGHTH_PEL
                                            : MV_PRECISION_HALF_PEL;
          }
#else
            features->fr_mv_precision = aom_rb_read_bit(rb)
                                            ? MV_PRECISION_ONE_EIGHTH_PEL
                                            : MV_PRECISION_QTR_PEL;

#endif  //  CONFIG_FRAME_HALF_PRECISION

          features->most_probable_fr_mv_precision = features->fr_mv_precision;
        }
        if (features->fr_mv_precision == MV_PRECISION_ONE_PEL) {
          features->use_pb_mv_precision = 0;
        } else {
          features->use_pb_mv_precision = cm->seq_params.enable_flex_mvres;
        }

        features->interp_filter = read_frame_interp_filter(rb);
        int seq_enabled_motion_modes = cm->seq_params.seq_enabled_motion_modes;

#if CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
        if (cm->seq_params.seq_frame_motion_modes_present_flag) {
#endif  // CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
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
#if CONFIG_MOTION_MODE_FRAME_HEADERS_OPT
        } else {
          features->enabled_motion_modes =
              cm->seq_params.seq_enabled_motion_modes;
        }
#endif  // CONFIG_MOTION_MODE_FRAME_HEADERS_OPT

#if !CONFIG_FIX_OPFL_AUTO
        if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_AUTO) {
          if (aom_rb_read_bit(rb)) {
            features->opfl_refine_type = REFINE_SWITCHABLE;
          } else {
            features->opfl_refine_type =
                aom_rb_read_bit(rb) ? REFINE_ALL : REFINE_NONE;
          }
        } else {
          features->opfl_refine_type = cm->seq_params.enable_opfl_refine;
        }
      } else {
        if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_NONE) {
          features->opfl_refine_type = REFINE_NONE;
        } else {
          features->opfl_refine_type = REFINE_ALL;
        }
#endif  // !CONFIG_FIX_OPFL_AUTO
      }
    }

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

  YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tmp_tip_frame->buf;
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
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
    if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
    // Set parameters corresponding to no filtering.
    struct loopfilter *lf = &cm->lf;
    lf->filter_level[0] = 0;
    lf->filter_level[1] = 0;
    cm->cdef_info.cdef_frame_enable = 0;
    cm->gdf_info.gdf_mode = 0;
    // set cm->rst_info and copy it to cur_frame->rst_info
    for (int plane = 0; plane < av1_num_planes(cm); plane++) {
      cm->rst_info[plane].frame_restoration_type = RESTORE_NONE;
      cm->rst_info[plane].frame_filters_on = 0;
      av1_copy_rst_frame_filters(&cm->cur_frame->rst_info[plane],
                                 &cm->rst_info[plane]);
      cm->cur_frame->rst_info[plane].num_filter_classes = 0;
      cm->cur_frame->rst_info[plane].rst_ref_pic_idx = 0;
      cm->cur_frame->rst_info[plane].temporal_pred_flag = 0;
    }
    features->refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;
    features->disable_cdf_update = 1;
#if CONFIG_CWG_F317
    const RefCntBuffer *ref_buf;
    if (cm->bridge_frame_info.is_bridge_frame) {
      ref_buf = get_ref_frame_buf(
          cm, cm->bridge_frame_info.bridge_frame_ref_idx_remapped);
    } else {
      ref_buf = get_ref_frame_buf(cm, cm->bru.update_ref_idx);
    }
#else
      const RefCntBuffer *bru_ref_buf =
          get_ref_frame_buf(cm, cm->bru.update_ref_idx);
#endif  // CONFIG_CWG_F317
    // set cm->ccso_info and copy to cur_frame->ccso_info
    // doing once in uncompred header
    cm->ccso_info.ccso_frame_flag = 0;
    for (int plane = 0; plane < CCSO_NUM_COMPONENTS; plane++) {
#if CONFIG_CWG_F317
      if (cm->bru.frame_inactive_flag ||
          cm->bridge_frame_info.is_bridge_frame) {
#else
        if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
        cm->ccso_info.reuse_ccso[plane] = 0;
        cm->ccso_info.sb_reuse_ccso[plane] = 0;
        cm->ccso_info.ccso_ref_idx[plane] = UINT8_MAX;
        cm->ccso_info.ccso_enable[plane] = 0;
        av1_copy_ccso_filters(&cm->cur_frame->ccso_info, &cm->ccso_info, plane,
                              1, 0, 0);
        continue;
      }
    }
#if CONFIG_CWG_F317
    cm->quant_params.base_qindex = ref_buf->base_qindex;
    if (av1_num_planes(cm) > 1) {
      cm->quant_params.u_ac_delta_q = ref_buf->u_ac_delta_q;
      cm->quant_params.v_ac_delta_q = ref_buf->v_ac_delta_q;
#else  // CONFIG_CWG_F317
      cm->quant_params.base_qindex = bru_ref_buf->base_qindex;
      if (av1_num_planes(cm) > 1) {
        cm->quant_params.u_ac_delta_q = bru_ref_buf->u_ac_delta_q;
        cm->quant_params.v_ac_delta_q = bru_ref_buf->v_ac_delta_q;
#endif
    } else {
      cm->quant_params.v_ac_delta_q = cm->quant_params.u_ac_delta_q = 0;
    }
    cm->cur_frame->base_qindex = cm->quant_params.base_qindex;
    cm->cur_frame->u_ac_delta_q = cm->quant_params.u_ac_delta_q;
    cm->cur_frame->v_ac_delta_q = cm->quant_params.v_ac_delta_q;
    xd->bd = (int)seq_params->bit_depth;
    set_primary_ref_frame_and_ctx(pbi);

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

    cm->cur_frame->film_grain_params_present =
        seq_params->film_grain_params_present;
    read_film_grain(cm, rb);

    return 0;
  }

  if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    // Set parameters corresponding to no filtering.
    struct loopfilter *lf = &cm->lf;
    lf->filter_level[0] = 0;
    lf->filter_level[1] = 0;

    cm->gdf_info.gdf_mode = 0;

    cm->cdef_info.cdef_frame_enable = 0;
    cm->rst_info[0].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_restoration_type = RESTORE_NONE;
  }

  if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    if (cm->seq_params.enable_tip_explicit_qp) {
      cm->quant_params.base_qindex = aom_rb_read_literal(
          rb, cm->seq_params.bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT
                                                     : QINDEX_BITS);
      if (av1_num_planes(cm) > 1 && cm->seq_params.uv_ac_delta_q_enabled) {
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
    features->refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;
    if (cm->seq_params.enable_lf_sub_pu && cm->features.allow_lf_sub_pu &&
        cm->lf.tip_filter_level) {
      read_tile_info(pbi, rb);
    }
    features->disable_cdf_update = 1;
    cm->cur_frame->film_grain_params_present =
        seq_params->film_grain_params_present;
    read_film_grain(cm, rb);
    // TIP frame will be output for displaying
    // No futher processing needed
    return 0;
  }
#if CONFIG_CWG_F317
  if (!cm->bridge_frame_info.is_bridge_frame) {
#endif  // CONFIG_CWG_F317
    features->disable_cdf_update = aom_rb_read_bit(rb);
#if CONFIG_CWG_F317
  } else {
    features->disable_cdf_update = 1;
  }
#endif  // CONFIG_CWG_F317

#if CONFIG_CWG_F317
  if (!cm->bridge_frame_info.is_bridge_frame) {
#endif  // CONFIG_CWG_F317
    const int might_bwd_adapt = !(seq_params->single_picture_hdr_flag) &&
                                !(features->disable_cdf_update);
    if (might_bwd_adapt) {
      features->refresh_frame_context = aom_rb_read_bit(rb)
                                            ? REFRESH_FRAME_CONTEXT_DISABLED
                                            : REFRESH_FRAME_CONTEXT_BACKWARD;
    } else {
      features->refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;
    }
#if CONFIG_CWG_F317
  }
#endif  // CONFIG_CWG_F317

  read_tile_info(pbi, rb);
  if (!av1_is_min_tile_width_satisfied(cm)) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Minimum tile width requirement not satisfied");
  }

  CommonQuantParams *const quant_params = &cm->quant_params;
  setup_quantization(quant_params, av1_num_planes(cm), &cm->seq_params, rb);
  cm->cur_frame->base_qindex = quant_params->base_qindex;
  cm->cur_frame->u_ac_delta_q = quant_params->u_ac_delta_q;
  cm->cur_frame->v_ac_delta_q = quant_params->v_ac_delta_q;
  xd->bd = (int)seq_params->bit_depth;

  set_primary_ref_frame_and_ctx(pbi);

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

  setup_segmentation(cm, rb);

  setup_qm_params(&cm->seq_params, quant_params, cm->seg.enabled,
                  av1_num_planes(cm), rb);

  cm->delta_q_info.delta_q_res = 1;
  cm->delta_q_info.delta_lf_res = 1;
  cm->delta_q_info.delta_lf_present_flag = 0;
  cm->delta_q_info.delta_lf_multi = 0;
  cm->delta_q_info.delta_q_present_flag =
      quant_params->base_qindex > 0 ? aom_rb_read_bit(rb) : 0;
  if (cm->delta_q_info.delta_q_present_flag) {
    xd->current_base_qindex = quant_params->base_qindex;
    cm->delta_q_info.delta_q_res = 1 << aom_rb_read_literal(rb, 2);
    cm->delta_q_info.delta_lf_present_flag = aom_rb_read_bit(rb);
    if (cm->delta_q_info.delta_lf_present_flag) {
      cm->delta_q_info.delta_lf_res = 1 << aom_rb_read_literal(rb, 2);
      cm->delta_q_info.delta_lf_multi = aom_rb_read_bit(rb);
      av1_reset_loop_filter_delta(xd, av1_num_planes(cm));
    }
  }

  xd->cur_frame_force_integer_mv = features->cur_frame_force_integer_mv;
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
  features->has_lossless_segment = 0;
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS

#if CONFIG_EXT_SEG
  const int max_seg_num =
      cm->seg.enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
#else   // CONFIG_EXT_SEG
    const int max_seg_num = MAX_SEGMENTS;
#endif  // CONFIG_EXT_SEG
  for (int i = 0; i < max_seg_num; i++) {
    const int qindex = av1_get_qindex(&cm->seg, i, quant_params->base_qindex,
                                      cm->seq_params.bit_depth);
    xd->lossless[i] =
        qindex == 0 && cm->delta_q_info.delta_q_present_flag == 0 &&
        (quant_params->y_dc_delta_q + cm->seq_params.base_y_dc_delta_q <= 0) &&
        (quant_params->u_dc_delta_q + cm->seq_params.base_uv_dc_delta_q <= 0) &&
        (quant_params->v_dc_delta_q + cm->seq_params.base_uv_dc_delta_q <= 0) &&
        (quant_params->u_ac_delta_q + cm->seq_params.base_uv_ac_delta_q <= 0) &&
        (quant_params->v_ac_delta_q + cm->seq_params.base_uv_ac_delta_q <= 0);

#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
    features->lossless_segment[i] = xd->lossless[i];
    if (xd->lossless[i]) features->has_lossless_segment = 1;
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS

    xd->qindex[i] = qindex;
    if (av1_use_qmatrix(quant_params, xd, i)) {
      if (quant_params->qm_index_bits > 0) {
        quant_params->qm_index[i] =
            aom_rb_read_literal(rb, quant_params->qm_index_bits);
#if CONFIG_QM_DEBUG
        printf("[DEC-FRM] qm_index[%d]: %d\n", i, quant_params->qm_index[i]);
#endif
        if (quant_params->qm_index[i] >= quant_params->pic_qm_num) {
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Invalid qm_index[%d]=%d >= %d", i,
                             quant_params->qm_index[i],
                             quant_params->pic_qm_num);
        }
      } else {
        quant_params->qm_index[i] = 0;
      }
    }
  }
  features->coded_lossless = is_coded_lossless(cm, xd);
  features->all_lossless = features->coded_lossless;

  // Decode frame-level TCQ flag, if applicable.
  if (features->coded_lossless) {
    features->tcq_mode = 0;
  } else if (seq_params->enable_tcq >= TCQ_8ST_FR) {
    features->tcq_mode = aom_rb_read_bit(rb);
  } else {
    features->tcq_mode = seq_params->enable_tcq;
  }

  if (features->coded_lossless || !cm->seq_params.enable_parity_hiding ||
      features->tcq_mode)
    features->allow_parity_hiding = false;
  else
    features->allow_parity_hiding = aom_rb_read_bit(rb);

  setup_segmentation_dequant(cm, xd);

  if (features->coded_lossless) {
    cm->lf.filter_level[0] = 0;
    cm->lf.filter_level[1] = 0;
  }
  if (features->coded_lossless || !seq_params->enable_cdef) {
    cm->cdef_info.cdef_frame_enable = 0;
  }
  if (features->all_lossless || !seq_params->enable_restoration) {
    cm->rst_info[0].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[0].frame_filters_on = 0;
    cm->rst_info[1].frame_filters_on = 0;
    cm->rst_info[2].frame_filters_on = 0;
  }
  setup_loopfilter(cm, rb);

  if (!features->coded_lossless && seq_params->enable_gdf) {
    setup_gdf(cm, rb);
  } else {
    cm->gdf_info.gdf_mode = 0;
  }
  if (!features->coded_lossless && seq_params->enable_cdef) {
    setup_cdef(cm, rb);
  }
  if (!features->all_lossless && seq_params->enable_restoration) {
    decode_restoration_mode(cm, rb);
  }
  for (int plane = 0; plane < CCSO_NUM_COMPONENTS; plane++) {
    cm->ccso_info.ccso_enable[plane] = false;
  }
  if (!features->coded_lossless && seq_params->enable_ccso) {
    setup_ccso(cm, rb);
  }

  features->tx_mode = read_tx_mode(rb, features->coded_lossless);
  current_frame->reference_mode = read_frame_reference_mode(cm, rb);

  av1_setup_skip_mode_allowed(cm);
  current_frame->skip_mode_info.skip_mode_flag =
      current_frame->skip_mode_info.skip_mode_allowed ? aom_rb_read_bit(rb) : 0;

  if (!frame_is_intra_only(cm) && seq_params->enable_bawp)
    features->enable_bawp = aom_rb_read_bit(rb);
  else
    features->enable_bawp = 0;

  features->enable_intra_bawp = seq_params->enable_bawp;

  features->enable_cwp = seq_params->enable_cwp;
  features->allow_warpmv_mode = 0;
  if (!frame_is_intra_only(cm) &&
      (features->enabled_motion_modes & (1 << WARP_DELTA)) != 0) {
    features->allow_warpmv_mode = aom_rb_read_bit(rb);
  }

  features->enable_imp_msk_bld = seq_params->enable_imp_msk_bld;

  features->reduced_tx_set_used =
#if CONFIG_REDUCED_TX_SET_EXT
      aom_rb_read_literal(rb, 2);
#else
        aom_rb_read_bit(rb);
#endif  // CONFIG_REDUCED_TX_SET_EXT

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

#if CONFIG_EXT_SEG
  features->enable_ext_seg = seq_params->enable_ext_seg;
#endif  // CONFIG_EXT_SEG

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

static AOM_INLINE void tip_mode_legal_check(AV1Decoder *const pbi) {
  AV1_COMMON *const cm = &pbi->common;
  if (cm->features.tip_frame_mode == TIP_FRAME_DISABLED) return;

  if (cm->current_frame.frame_type == KEY_FRAME ||
      cm->current_frame.frame_type == INTRA_ONLY_FRAME ||
      cm->current_frame.frame_type == S_FRAME) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid TIP mode.");
  }

  if (!cm->features.allow_ref_frame_mvs) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid TIP mode.");
  }

  if (!cm->seq_params.enable_tip) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid TIP mode.");
  }

  if (!cm->has_both_sides_refs && cm->ref_frames_info.num_past_refs < 2) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid TIP mode.");
  }

  TIP *tip_ref = &cm->tip_ref;
  if (tip_ref->ref_frame[0] == NONE_FRAME ||
      tip_ref->ref_frame[1] == NONE_FRAME) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid TIP mode.");
  }

  const int tip_mode_allowed =
      tip_ref->ref_frame[0] != NONE_FRAME &&
      tip_ref->ref_frame[1] != NONE_FRAME &&
      is_ref_motion_field_eligible_by_frame_size(
          cm, get_ref_frame_buf(cm, tip_ref->ref_frame[0])) &&
      is_ref_motion_field_eligible_by_frame_size(
          cm, get_ref_frame_buf(cm, tip_ref->ref_frame[1]));

  if (!tip_mode_allowed ||
      (!is_ref_motion_field_eligible_by_frame_type(
           get_ref_frame_buf(cm, tip_ref->ref_frame[0])) &&
       !is_ref_motion_field_eligible_by_frame_type(
           get_ref_frame_buf(cm, tip_ref->ref_frame[1])))) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid TIP mode.");
  }
}

static AOM_INLINE void process_tip_mode(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &pbi->dcb.xd;

  tip_mode_legal_check(pbi);

  if (!cm->features.allow_ref_frame_mvs) return;

  if (cm->features.tip_frame_mode == TIP_FRAME_DISABLED) {
    // TPL mvs at non-sampled locations will be filled after it is hole-filled
    // and smoothed.
    av1_fill_tpl_mvs_sample_gap(cm);
    return;
  }

  if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    xd->opfl_vxy_bufs = pbi->td.opfl_vxy_bufs;
    av1_dec_setup_tip_frame(cm, xd, pbi->td.mc_buf, pbi->td.tmp_conv_dst);
  } else if (cm->features.tip_frame_mode == TIP_FRAME_AS_REF) {
    av1_setup_tip_motion_field(cm);
  }

  if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    for (int plane = 0; plane < av1_num_planes(cm); plane++) {
      cm->cur_frame->ccso_info.ccso_enable[plane] = 0;
    }
    aom_yv12_copy_frame(&cm->tip_ref.tip_frame->buf, &cm->cur_frame->buf,
                        num_planes);
    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      cm->global_motion[i] = default_warp_params;
      cm->cur_frame->global_motion[i] = default_warp_params;
    }

    set_primary_ref_frame_and_ctx(pbi);
    cm->seg.enabled = 0;
    if (cm->cur_frame->seg_map) {
      memset(cm->cur_frame->seg_map, 0,
             (cm->cur_frame->mi_rows * cm->cur_frame->mi_cols));
    }
    memset(&cm->seg, 0, sizeof(cm->seg));
#if CONFIG_EXT_SEG
    cm->seg.enable_ext_seg = cm->seq_params.enable_ext_seg;
#endif  // CONFIG_EXT_SEG
    segfeatures_copy(&cm->cur_frame->seg, &cm->seg);
    if (!cm->tiles.large_scale) {
      cm->cur_frame->frame_context = *cm->fc;
    }
  }
}
#if CONFIG_F106_OBU_TILEGROUP
static int32_t read_tile_indices_in_tilegroup(AV1Decoder *pbi,
                                              struct aom_read_bit_buffer *rb,
                                              int *start_tile, int *end_tile) {
  AV1_COMMON *const cm = &pbi->common;
  CommonTileParams *const tiles = &cm->tiles;
  uint32_t saved_bit_offset = rb->bit_offset;
  int tile_start_and_end_present_flag = 0;
  const int num_tiles = tiles->rows * tiles->cols;

  if (!tiles->large_scale && num_tiles > 1) {
    tile_start_and_end_present_flag = aom_rb_read_bit(rb);
  }
  if (tiles->large_scale || num_tiles == 1 ||
      !tile_start_and_end_present_flag) {
    *start_tile = 0;
    *end_tile = num_tiles - 1;
  } else {
    int tile_bits = tiles->log2_rows + tiles->log2_cols;
    *start_tile = aom_rb_read_literal(rb, tile_bits);
    *end_tile = aom_rb_read_literal(rb, tile_bits);
  }
  if (*start_tile != pbi->next_start_tile) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "tg_start (%d) must be equal to %d", *start_tile,
                       pbi->next_start_tile);
    return -1;
  }
  if (*start_tile > *end_tile) {
    aom_internal_error(
        &cm->error, AOM_CODEC_CORRUPT_FRAME,
        "tg_end (%d) must be greater than or equal to tg_start (%d)", *end_tile,
        *start_tile);
    return -1;
  }
  if (*end_tile >= num_tiles) {
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "tg_end (%d) must be less than NumTiles (%d)", *end_tile,
                       num_tiles);
    return -1;
  }
  pbi->next_start_tile = (*end_tile == num_tiles - 1) ? 0 : *end_tile + 1;

  if (cm->bru.enabled) {
    if (num_tiles > 1) {
      for (int tile_idx = *start_tile; tile_idx <= *end_tile; tile_idx++) {
        const int active_bitmap_byte = tile_idx >> 3;
        const int active_bitmap_bit = tile_idx & 7;
        tiles->tile_active_bitmap[active_bitmap_byte] +=
            (aom_rb_read_bit(rb) << active_bitmap_bit);
      }
    } else {
      tiles->tile_active_bitmap[0] = 1;
    }
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

int32_t av1_read_tilegroup_header(
    AV1Decoder *pbi, struct aom_read_bit_buffer *rb, const uint8_t *data,
    const uint8_t **p_data_end, int *first_tile_group_in_frame, int *start_tile,
    int *end_tile, OBU_TYPE obu_type) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(pbi, av1_read_tilegroup_header);
#endif

  assert(rb->bit_offset == 0);

  AV1_COMMON *const cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &pbi->dcb.xd;

  int is_first_tile_group = 1;
  int send_uncompressed_header_flag = 1;
  bool send_first_tile_group_indication = true;
#if CONFIG_F106_OBU_SEF
  send_first_tile_group_indication &= obu_type != OBU_SEF;
#endif  // CONFIG_F106_OBU_SEF
#if CONFIG_F106_OBU_TIP
  send_first_tile_group_indication &= obu_type != OBU_TIP;
#endif  // CONFIG_F106_OBU_TIP
  if (send_first_tile_group_indication)
    is_first_tile_group = aom_rb_read_bit(rb);
  *first_tile_group_in_frame = is_first_tile_group;

  if (is_first_tile_group) {
#if CONFIG_MISMATCH_DEBUG
    mismatch_move_frame_idx_r(1);
#endif  // CONFIG_MISMATCH_DEBUG
    pbi->seen_frame_header = 1;
    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      cm->global_motion[i] = default_warp_params;
      cm->cur_frame->global_motion[i] = default_warp_params;
    }

    for (int p = 0; p < num_planes; ++p) {
      cm->cur_frame->rst_info[p].frame_filters_on = 0;
    }

    xd->global_motion = cm->global_motion;
  }  // first_tile_group_in_frame
  else {
    send_uncompressed_header_flag = aom_rb_read_bit(rb);
  }

  if (send_uncompressed_header_flag) {
    if (!is_first_tile_group) {
      rb->bit_offset += pbi->uncomp_hdr_size_in_bits;
    } else {
      uint32_t uncomp_hdr_start_point = rb->bit_offset;
      read_uncompressed_header(pbi, obu_type, rb);
      pbi->uncomp_hdr_size_in_bits = rb->bit_offset - uncomp_hdr_start_point;
    }
  }

  if (is_first_tile_group) {
#if CONFIG_BITSTREAM_DEBUG
#if CONFIG_FRAME_OUTPUT_ORDER_WITH_LAYER_ID
    aom_bitstream_queue_set_frame_read(
        derive_output_order_idx(cm, cm->current_frame) * 2 + cm->show_frame);
#else   // CONFIG_FRAME_OUTPUT_ORDER_WITH_LAYER_ID
    aom_bitstream_queue_set_frame_read(cm->current_frame.order_hint * 2 +
                                       cm->show_frame);
#endif  // CONFIG_FRAME_OUTPUT_ORDER_WITH_LAYER_ID
#endif
    if (!cm->tiles.single_tile_decoding &&
        (pbi->dec_tile_row >= 0 || pbi->dec_tile_col >= 0)) {
      pbi->dec_tile_row = -1;
      pbi->dec_tile_col = -1;
    }

    // aom_rb_bytes_read()= (rb->bit_offset + 7) >> 3;
    const uint32_t uncomp_hdr_size =
        (uint32_t)aom_rb_bytes_read(rb);  // Size of the uncompressed header
    YV12_BUFFER_CONFIG *new_fb = &cm->cur_frame->buf;
    xd->cur_buf = new_fb;
    if (av1_allow_intrabc(cm, xd, BLOCK_4X4) && xd->tree_type != CHROMA_PART) {
      av1_setup_scale_factors_for_frame(
          &cm->sf_identity, xd->cur_buf->y_crop_width,
          xd->cur_buf->y_crop_height, xd->cur_buf->y_crop_width,
          xd->cur_buf->y_crop_height);
    }
#if CONFIG_F106_OBU_SEF
    if (obu_type == OBU_SEF)
#else
    if (cm->show_existing_frame)
#endif  // CONFIG_F106_OBU_SEF
    {
      // showing a frame directly
      *p_data_end = data + uncomp_hdr_size;
      if (pbi->reset_decoder_state) {
        // Use the default frame context values.
        *cm->fc = *cm->default_frame_context;
        if (!cm->fc->initialized)
          aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                             "Uninitialized entropy context.");
      }
#if CONFIG_COLLECT_COMPONENT_TIMING
      end_timing(pbi, av1_read_tilegroup_header);
#endif
      return uncomp_hdr_size;
    }

    cm->mi_params.setup_mi(&cm->mi_params);

    if (cm->features.allow_ref_frame_mvs)
      av1_setup_motion_field(cm);
    else
      av1_setup_ref_frame_sides(cm);

#if CONFIG_CWG_F317
    if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
    if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
      for (int plane = 0; plane < av1_num_planes(cm); plane++) {
        cm->cur_frame->ccso_info.ccso_enable[plane] = 0;
      }
      MV_REF *frame_mvs = cm->cur_frame->mvs;
      const int mvs_rows =
          ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
      const int mvs_cols =
          ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
      const int mvs_stride = mvs_cols;

      for (int h = 0; h < mvs_rows; h++) {
        MV_REF *mv = frame_mvs;
        for (int w = 0; w < mvs_cols; w++) {
#if CONFIG_CWG_F317
          if (cm->bridge_frame_info.is_bridge_frame) {
            mv->ref_frame[0] =
                cm->bridge_frame_info.bridge_frame_ref_idx_remapped;
          } else {
            mv->ref_frame[0] = cm->bru.update_ref_idx;
          }
#else
          mv->ref_frame[0] = cm->bru.update_ref_idx;
#endif  // CONFIG_CWG_F317
          mv->ref_frame[1] = NONE_FRAME;
          mv->mv[0].as_int = 0;
          mv->mv[1].as_int = 0;
          mv++;
        }
        frame_mvs += mvs_stride;
      }
      for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
        cm->global_motion[i] = default_warp_params;
        cm->cur_frame->global_motion[i] = default_warp_params;
      }

      set_primary_ref_frame_and_ctx(pbi);
      cm->seg.enabled = 0;
      if (cm->cur_frame->seg_map) {
        memset(cm->cur_frame->seg_map, 0,
               (cm->cur_frame->mi_rows * cm->cur_frame->mi_cols));
      }
      memset(&cm->seg, 0, sizeof(cm->seg));
#if CONFIG_EXT_SEG
      cm->seg.enable_ext_seg = cm->seq_params.enable_ext_seg;
#endif  // CONFIG_EXT_SEG
      segfeatures_copy(&cm->cur_frame->seg, &cm->seg);

      if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
        // use the default frame context values
        *cm->fc = *cm->default_frame_context;
      } else {
        *cm->fc = get_primary_ref_frame_buf(cm, cm->features.primary_ref_frame)
                      ->frame_context;
        int ref_frame_used = PRIMARY_REF_NONE;
        int map_idx = INVALID_IDX;
        get_secondary_reference_frame_idx(cm, &ref_frame_used, &map_idx);
        avg_primary_secondary_references(cm, ref_frame_used, map_idx);
      }
      *p_data_end = data + uncomp_hdr_size;
#if CONFIG_COLLECT_COMPONENT_TIMING
      end_timing(pbi, av1_read_tilegroup_header);
#endif
      return uncomp_hdr_size;
    }

    process_tip_mode(pbi);
#if CONFIG_F106_OBU_TIP
    if (obu_type == OBU_TIP)
#else
    if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
#endif  // CONFIG_F106_OBU_TIP
    {
      *p_data_end = data + uncomp_hdr_size;
#if CONFIG_COLLECT_COMPONENT_TIMING
      end_timing(pbi, av1_read_tilegroup_header);
#endif
      return uncomp_hdr_size;
    }

    av1_setup_block_planes(xd, cm->seq_params.subsampling_x,
                           cm->seq_params.subsampling_y, num_planes);
    if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
      // use the default frame context values
      *cm->fc = *cm->default_frame_context;
    } else {
      *cm->fc = get_primary_ref_frame_buf(cm, cm->features.primary_ref_frame)
                    ->frame_context;
      int ref_frame_used = PRIMARY_REF_NONE;
      int map_idx = INVALID_IDX;
      get_secondary_reference_frame_idx(cm, &ref_frame_used, &map_idx);
      avg_primary_secondary_references(cm, ref_frame_used, map_idx);
    }
    if (!cm->fc->initialized)
      aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                         "Uninitialized entropy context.");

    pbi->dcb.corrupted = 0;
  }

  bool tile_indices_present_flag = true;
#if CONFIG_F106_OBU_SEF
  tile_indices_present_flag &= obu_type != OBU_SEF;
#else
  tile_indices_present_flag &= !cm->show_existing_frame;
#endif  // CONFIG_F106_OBU_SEF
#if CONFIG_F106_OBU_TIP
  tile_indices_present_flag &= obu_type != OBU_TIP;
#else
  tile_indices_present_flag &=
      (cm->features.tip_frame_mode != TIP_FRAME_AS_OUTPUT);
#endif  // CONFIG_F106_OBU_TIP
#if CONFIG_CWG_F317
  tile_indices_present_flag &=
      (!cm->bru.frame_inactive_flag && !cm->bridge_frame_info.is_bridge_frame);
#else
  tile_indices_present_flag &= !cm->bru.frame_inactive_flag;
#endif  // CONFIG_CWG_F317
  if (tile_indices_present_flag)
    read_tile_indices_in_tilegroup(pbi, rb, start_tile, end_tile);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(pbi, av1_read_tilegroup_header);
#endif
  return (int32_t)aom_rb_bytes_read(rb);
}
#endif  // CONFIG_F106_OBU_TILEGROUP
#if !CONFIG_F106_OBU_TILEGROUP
uint32_t av1_decode_frame_headers_and_setup(AV1Decoder *pbi,
                                            struct aom_read_bit_buffer *rb,
                                            const uint8_t *data,
                                            const uint8_t **p_data_end,
                                            int trailing_bits_present) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(pbi, av1_decode_frame_headers_and_setup_time);
#endif

  AV1_COMMON *const cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &pbi->dcb.xd;

#if CONFIG_MISMATCH_DEBUG
  mismatch_move_frame_idx_r(1);
#endif  // CONFIG_MISMATCH_DEBUG

  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    cm->global_motion[i] = default_warp_params;
    cm->cur_frame->global_motion[i] = default_warp_params;
  }

  for (int p = 0; p < num_planes; ++p) {
    cm->cur_frame->rst_info[p].frame_filters_on = 0;
  }

  xd->global_motion = cm->global_motion;

  read_uncompressed_header(pbi, rb);

#if CONFIG_BITSTREAM_DEBUG
#if CONFIG_FRAME_OUTPUT_ORDER_WITH_LAYER_ID
  aom_bitstream_queue_set_frame_read(
      derive_output_order_idx(cm, cm->current_frame) * 2 + cm->show_frame);
#else   // CONFIG_FRAME_OUTPUT_ORDER_WITH_LAYER_ID
  aom_bitstream_queue_set_frame_read(cm->current_frame.order_hint * 2 +
                                     cm->show_frame);
#endif  // CONFIG_FRAME_OUTPUT_ORDER_WITH_LAYER_ID
#endif

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
  if (av1_allow_intrabc(cm, xd, BLOCK_4X4) && xd->tree_type != CHROMA_PART) {
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

#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(pbi, av1_decode_frame_headers_and_setup_time);
#endif

    return uncomp_hdr_size;
  }

  cm->mi_params.setup_mi(&cm->mi_params);

  if (cm->features.allow_ref_frame_mvs)
    av1_setup_motion_field(cm);
  else
    av1_setup_ref_frame_sides(cm);

#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame) {
#else
  if (cm->bru.frame_inactive_flag) {
#endif  // CONFIG_CWG_F317
    for (int plane = 0; plane < av1_num_planes(cm); plane++) {
      cm->cur_frame->ccso_info.ccso_enable[plane] = 0;
    }
    MV_REF *frame_mvs = cm->cur_frame->mvs;
    const int mvs_rows =
        ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
    const int mvs_cols =
        ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
    const int mvs_stride = mvs_cols;

    for (int h = 0; h < mvs_rows; h++) {
      MV_REF *mv = frame_mvs;
      for (int w = 0; w < mvs_cols; w++) {
#if CONFIG_CWG_F317
        if (cm->bridge_frame_info.is_bridge_frame) {
          mv->ref_frame[0] =
              cm->bridge_frame_info.bridge_frame_ref_idx_remapped;
        } else {
          mv->ref_frame[0] = cm->bru.update_ref_idx;
        }
#else
        mv->ref_frame[0] = cm->bru.update_ref_idx;
#endif  // CONFIG_CWG_F317
        mv->ref_frame[1] = NONE_FRAME;
        mv->mv[0].as_int = 0;
        mv->mv[1].as_int = 0;
        mv++;
      }
      frame_mvs += mvs_stride;
    }
    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      cm->global_motion[i] = default_warp_params;
      cm->cur_frame->global_motion[i] = default_warp_params;
    }
    set_primary_ref_frame_and_ctx(pbi);
    cm->seg.enabled = 0;
    if (cm->cur_frame->seg_map) {
      memset(cm->cur_frame->seg_map, 0,
             (cm->cur_frame->mi_rows * cm->cur_frame->mi_cols));
    }
    memset(&cm->seg, 0, sizeof(cm->seg));
#if CONFIG_EXT_SEG
    cm->seg.enable_ext_seg = cm->seq_params.enable_ext_seg;
#endif  // CONFIG_EXT_SEG
    segfeatures_copy(&cm->cur_frame->seg, &cm->seg);

    if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
      // use the default frame context values
      *cm->fc = *cm->default_frame_context;
    } else {
      *cm->fc = get_primary_ref_frame_buf(cm, cm->features.primary_ref_frame)
                    ->frame_context;
      int ref_frame_used = PRIMARY_REF_NONE;
      int map_idx = INVALID_IDX;
      get_secondary_reference_frame_idx(cm, &ref_frame_used, &map_idx);
      avg_primary_secondary_references(cm, ref_frame_used, map_idx);
    }
    *p_data_end = data + uncomp_hdr_size;
    return uncomp_hdr_size;
  }

  process_tip_mode(pbi);
  if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    *p_data_end = data + uncomp_hdr_size;

#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(pbi, av1_decode_frame_headers_and_setup_time);
#endif

    return uncomp_hdr_size;
  }

  av1_setup_block_planes(xd, cm->seq_params.subsampling_x,
                         cm->seq_params.subsampling_y, num_planes);
  if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
    // use the default frame context values
    *cm->fc = *cm->default_frame_context;
  } else {
    *cm->fc = get_primary_ref_frame_buf(cm, cm->features.primary_ref_frame)
                  ->frame_context;
    int ref_frame_used = PRIMARY_REF_NONE;
    int map_idx = INVALID_IDX;
    get_secondary_reference_frame_idx(cm, &ref_frame_used, &map_idx);
    avg_primary_secondary_references(cm, ref_frame_used, map_idx);
  }
  if (!cm->fc->initialized)
    aom_internal_error(&cm->error, AOM_CODEC_CORRUPT_FRAME,
                       "Uninitialized entropy context.");

  pbi->dcb.corrupted = 0;

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(pbi, av1_decode_frame_headers_and_setup_time);
#endif

  return uncomp_hdr_size;
}
#endif  // !CONFIG_F106_OBU_TILEGROUP
// Once-per-frame initialization
static AOM_INLINE void setup_frame_info(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;

  if (cm->rst_info[0].frame_restoration_type != RESTORE_NONE ||
      cm->rst_info[1].frame_restoration_type != RESTORE_NONE ||
      cm->rst_info[2].frame_restoration_type != RESTORE_NONE) {
    av1_alloc_restoration_buffers(cm);
  } else {
    if (cm->gdf_info.gdf_mode) {
      av1_alloc_restoration_boundary_buffers(cm, 1);
    }
  }
  const int buf_size = MC_TEMP_BUF_PELS << 1;
  if (pbi->td.mc_buf_size != buf_size) {
    av1_free_mc_tmp_buf(&pbi->td);
    av1_free_opfl_tmp_bufs(&pbi->td);

    allocate_mc_tmp_buf(cm, &pbi->td, buf_size);
    allocate_opfl_tmp_bufs(cm, &pbi->td);
  }
}

// 1) For multiple tiles-based coding, calculate the average CDFs from the
// allowed tiles, and use the average CDFs of the tiles as the frame's CDFs
// 2) For one tile coding, directly use that tile's CDFs as the frame's CDFs
void decoder_avg_tiles_cdfs(AV1Decoder *const pbi) {
  AV1_COMMON *const cm = &pbi->common;
  const CommonTileParams *const tiles = &cm->tiles;
  const int total_tiles = tiles->rows * tiles->cols;
  if (total_tiles == 1) {
    *cm->fc = pbi->tile_data[0].tctx;
  } else {
    const unsigned int total_tiles_log2 = av1_compute_allowed_tiles_log2(cm);
    const unsigned int used_tiles = (1 << total_tiles_log2);
    *cm->fc = pbi->tile_data[0].tctx;
    av1_shift_cdf_symbols(cm->fc, total_tiles_log2);
    for (unsigned int tile_idx = 1; tile_idx < used_tiles; ++tile_idx) {
      av1_cumulative_avg_cdf_symbols(cm->fc, &pbi->tile_data[tile_idx].tctx,
                                     total_tiles_log2);
    }
  }
}

void av1_gdf_frame_dec(AV1_COMMON *cm) { gdf_filter_frame(cm); }

void av1_decode_tg_tiles_and_wrapup(AV1Decoder *pbi, const uint8_t *data,
                                    const uint8_t *data_end,
                                    const uint8_t **p_data_end, int start_tile,
                                    int end_tile, int initialize_flag) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(pbi, av1_decode_tg_tiles_and_wrapup_time);
#endif

  AV1_COMMON *const cm = &pbi->common;
  CommonTileParams *const tiles = &cm->tiles;
  MACROBLOCKD *const xd = &pbi->dcb.xd;
  const int tile_count_tg = end_tile - start_tile + 1;

  if (!cm->wedge_mask_initialized &&
      cm->current_frame.frame_type != KEY_FRAME) {
    av1_init_wedge_masks();
    cm->wedge_mask_initialized = true;
  }
  if (initialize_flag) setup_frame_info(pbi);
  const int num_planes = av1_num_planes(cm);
#if CONFIG_INSPECTION
  aom_realloc_frame_buffer(
      &cm->predicted_pixels, cm->width, cm->height,
      cm->seq_params.subsampling_x, cm->seq_params.subsampling_y,
      AOM_DEC_BORDER_IN_PIXELS, cm->features.byte_alignment, NULL, NULL, NULL,
      false);
  aom_realloc_frame_buffer(
      &cm->prefiltered_pixels, cm->width, cm->height,
      cm->seq_params.subsampling_x, cm->seq_params.subsampling_y,
      AOM_DEC_BORDER_IN_PIXELS, cm->features.byte_alignment, NULL, NULL, NULL,
      false);
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

  av1_alloc_cdef_buffers(cm, &pbi->cdef_worker, &pbi->cdef_sync,
                         pbi->num_workers);
  av1_alloc_cdef_sync(cm, &pbi->cdef_sync, pbi->num_workers);

  // verify active region
  if (!bru_active_map_validation(cm)) {
    aom_internal_error(&cm->error, AOM_CODEC_ERROR, "Invalid active region");
  }
  // bru swap in mode 0, in mode 1, still use cur frame and then drop bru ref
  // after filtering
  // backward reference frame update
  // check if any active block
  // 1. intra frames do not have backward update
  // 2. if no active block in the frame and bru enabled, no need to update, just
  // drop the recon
  // 3. if any inactive block exists and bru enabled, update ref and drop the
  // if enabled flag signaled == 1, do swap
  if (cm->bru.enabled && pbi->bru_opt_mode &&
      cm->current_frame.frame_type != KEY_FRAME) {
    dec_bru_swap_stage(cm, xd);
    if (cm->bru.frame_inactive_flag && !tiles->large_scale) {
      cm->cur_frame->frame_context = *cm->fc;
#if CONFIG_PARAKIT_COLLECT_DATA
      for (int i = 0; i < MAX_NUM_CTX_GROUPS; i++)
        for (int j = 0; j < MAX_DIMS_CONTEXT3; j++)
          for (int k = 0; k < MAX_DIMS_CONTEXT2; k++)
            for (int l = 0; l < MAX_DIMS_CONTEXT1; l++)
              for (int h = 0; h < MAX_DIMS_CONTEXT0; h++)
                beginningFrameFlag[i][j][k][l][h] = 1;
#endif
      return;
    }
  }

  if (!cm->bru.frame_inactive_flag && !tiles->single_tile_decoding) {
    if (cm->lf.filter_level[0] || cm->lf.filter_level[1]) {
      if (pbi->num_workers > 1) {
        av1_loop_filter_frame_mt(&cm->cur_frame->buf, cm, &pbi->dcb.xd, 0,
                                 num_planes, 0, pbi->tile_workers,
                                 pbi->num_workers, &pbi->lf_row_sync);
      } else {
        av1_loop_filter_frame(&cm->cur_frame->buf, cm, &pbi->dcb.xd, 0,
                              num_planes, 0);
      }
    }

    const int use_ccso =
        !pbi->skip_loop_filter && !cm->features.coded_lossless &&
        (cm->ccso_info.ccso_enable[0] || cm->ccso_info.ccso_enable[1] ||
         cm->ccso_info.ccso_enable[2]);
    uint16_t *ext_rec_y = NULL;
    if (use_ccso) {
      const int pic_height = cm->cur_frame->buf.y_height;
      const int pic_width = cm->cur_frame->buf.y_width;
      const int dst_stride = cm->cur_frame->buf.y_stride;
      const uint16_t *rec_y = cm->cur_frame->buf.y_buffer;
      const int ccso_stride_ext = pic_width + (CCSO_PADDING_SIZE << 1);
      ext_rec_y = aom_malloc(sizeof(*ext_rec_y) *
                             (pic_height + (CCSO_PADDING_SIZE << 1)) *
                             (pic_width + (CCSO_PADDING_SIZE << 1)));
      for (int r = 0; r < pic_height; ++r) {
        for (int c = 0; c < pic_width; ++c) {
          ext_rec_y[(r + CCSO_PADDING_SIZE) * ccso_stride_ext + c +
                    CCSO_PADDING_SIZE] = rec_y[r * dst_stride + c];
        }
      }
      extend_ccso_border(&cm->cur_frame->buf, ext_rec_y, CCSO_PADDING_SIZE);
    }

    const int do_loop_restoration =
        cm->rst_info[0].frame_restoration_type != RESTORE_NONE ||
        cm->rst_info[1].frame_restoration_type != RESTORE_NONE ||
        cm->rst_info[2].frame_restoration_type != RESTORE_NONE;
    const int do_cdef = !pbi->skip_loop_filter &&
                        !cm->features.coded_lossless &&
                        cm->cdef_info.cdef_frame_enable;
    const int do_gdf = is_gdf_enabled(cm);
    const int optimized_loop_restoration =
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
        !cm->seq_params.disable_loopfilters_across_tiles &&
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
        !do_gdf && !use_ccso && !do_cdef;

    if (!optimized_loop_restoration) {
      if (do_loop_restoration)
        av1_loop_restoration_save_boundary_lines(&pbi->common.cur_frame->buf,
                                                 cm, 0);
      else {
        if (do_gdf)
          save_tile_row_boundary_lines(&pbi->common.cur_frame->buf, 0, cm, 0);
      }

      if (do_cdef) {
        if (pbi->num_workers > 1
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
            && USE_LOOP_RESTORATION_MT
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
        ) {
          av1_cdef_frame_mt(cm, &pbi->dcb.xd, pbi->cdef_worker,
                            pbi->tile_workers, &pbi->cdef_sync,
                            pbi->num_workers, av1_cdef_init_fb_row_mt);
        } else {
          av1_cdef_frame(&pbi->common.cur_frame->buf, cm, &pbi->dcb.xd,
                         av1_cdef_init_fb_row);
        }
      }
      if (use_ccso) {
        if (pbi->num_workers > 1
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
            && !cm->seq_params.disable_loopfilters_across_tiles &&
            USE_LOOP_RESTORATION_MT
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
        ) {
          av1_ccso_frame_mt(&cm->cur_frame->buf, cm, xd, pbi->tile_workers,
                            pbi->num_workers, ext_rec_y, &pbi->ccso_sync);
        } else {
          ccso_frame(&cm->cur_frame->buf, cm, xd, ext_rec_y);
        }
        aom_free(ext_rec_y);
      }
      if (do_gdf) {
        gdf_copy_guided_frame(cm);
      }
      if (do_loop_restoration) {
        av1_loop_restoration_save_boundary_lines(&pbi->common.cur_frame->buf,
                                                 cm, 1);
      } else {
        if (do_gdf)
          save_tile_row_boundary_lines(&pbi->common.cur_frame->buf, 0, cm, 1);
      }
      if (do_loop_restoration) {
        // HERE
        copy_frame_filters_to_runits_if_needed(cm);
        if (pbi->num_workers > 1
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
            && USE_LOOP_RESTORATION_MT
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
        ) {
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
      if (do_gdf) {
        av1_gdf_frame_dec(cm);
        gdf_free_guided_frame(cm);
      }

    } else {
      // In no cdef and no superres case. Provide an optimized version of
      // loop_restoration_filter.
      if (do_loop_restoration) {
        // HERE
        copy_frame_filters_to_runits_if_needed(cm);
        if (pbi->num_workers > 1
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
            && USE_LOOP_RESTORATION_MT
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
        ) {
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

  if (!pbi->dcb.corrupted) {
    if (cm->features.refresh_frame_context == REFRESH_FRAME_CONTEXT_BACKWARD) {
      if (cm->seq_params.enable_avg_cdf && cm->seq_params.avg_cdf_type &&
          cm->tiles.rows * cm->tiles.cols > 1) {
        decoder_avg_tiles_cdfs(pbi);
      } else {
        assert(pbi->context_update_tile_id < pbi->allocated_tiles);
        *cm->fc = pbi->tile_data[pbi->context_update_tile_id].tctx;
      }
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
#if CONFIG_PARAKIT_COLLECT_DATA
    for (int i = 0; i < MAX_NUM_CTX_GROUPS; i++)
      for (int j = 0; j < MAX_DIMS_CONTEXT3; j++)
        for (int k = 0; k < MAX_DIMS_CONTEXT2; k++)
          for (int l = 0; l < MAX_DIMS_CONTEXT1; l++)
            for (int h = 0; h < MAX_DIMS_CONTEXT0; h++)
              beginningFrameFlag[i][j][k][l][h] = 1;
#endif
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(pbi, av1_decode_tg_tiles_and_wrapup_time);
#endif
}
