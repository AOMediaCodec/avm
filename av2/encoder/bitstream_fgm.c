/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
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
#include <limits.h>
#include <stdio.h>

#include "avm/avm_encoder.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/binary_codes_writer.h"
#include "avm_dsp/bitwriter_buffer.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/bitops.h"
#include "avm_ports/mem_ops.h"
#include "avm_ports/system_state.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "avm_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"

#include "av2/common/blockd.h"
#include "av2/common/cdef.h"
#include "av2/common/ccso.h"
#include "av2/common/cfl.h"
#include "av2/common/entropy.h"
#include "av2/common/entropymode.h"
#include "av2/common/entropymv.h"
#include "av2/common/intra_dip.h"
#include "av2/common/mvref_common.h"
#include "av2/common/pred_common.h"
#include "av2/common/quant_common.h"
#include "av2/common/reconinter.h"
#include "av2/common/reconintra.h"
#include "av2/common/seg_common.h"
#include "av2/common/tile_common.h"

#include "av2/encoder/bitstream.h"
#include "av2/common/cost.h"
#include "av2/encoder/encodemv.h"
#include "av2/encoder/encodetxb.h"
#include "av2/encoder/mcomp.h"
#include "av2/encoder/palette.h"
#include "av2/encoder/pickrst.h"
#include "av2/encoder/segmentation.h"
#include "av2/encoder/tokenize.h"
#if CONFIG_F153_FGM_OBU
void set_film_grain_model(const AV2_COMP *const cpi,
                          struct film_grain_model *fgm_current) {
  const avm_film_grain_t *const pars = &cpi->common.film_grain_params;
  const SequenceHeader *const seq_params = &cpi->common.seq_params;

  fgm_current->fgm_id = 0;
  fgm_current->fgm_chroma_idc = CHROMA_FORMAT_420;
  if (seq_params->monochrome)
    fgm_current->fgm_chroma_idc = CHROMA_FORMAT_400;
  else if (seq_params->subsampling_x == 0 && seq_params->subsampling_y == 1)
    fgm_current->fgm_chroma_idc = CHROMA_FORMAT_444;
  else if (seq_params->subsampling_x == 1 && seq_params->subsampling_y == 0)
    fgm_current->fgm_chroma_idc = CHROMA_FORMAT_422;
  for (int c = 0; c < 3; c++) {
    fgm_current->fgm_points[c] = pars->fgm_points[c];
  }
  for (int i = 0; i < 14; i++) {
    for (int j = 0; j < 2; j++) {
      fgm_current->fgm_scaling_points[0][i][j] =
          pars->fgm_scaling_points_0[i][j];
      fgm_current->fgm_scaling_points[1][i][j] =
          pars->fgm_scaling_points_1[i][j];
      fgm_current->fgm_scaling_points[2][i][j] =
          pars->fgm_scaling_points_2[i][j];
    }
  }
  fgm_current->scaling_shift = pars->scaling_shift;
  fgm_current->ar_coeff_lag = pars->ar_coeff_lag;

  // 8 bit values
  for (int i = 0; i < 24; i++)
    fgm_current->ar_coeffs_y[i] = pars->ar_coeffs_y[i];
  for (int i = 0; i < 25; i++) {
    fgm_current->ar_coeffs_cb[i] = pars->ar_coeffs_cb[i];
    fgm_current->ar_coeffs_cr[i] = pars->ar_coeffs_cr[i];
  }
  fgm_current->ar_coeff_shift = pars->ar_coeff_shift;
  fgm_current->cb_mult = pars->cb_mult;
  fgm_current->cb_luma_mult = pars->cb_luma_mult;
  fgm_current->cb_offset = pars->cb_offset;
  fgm_current->cr_mult = pars->cr_mult;
  fgm_current->cr_luma_mult = pars->cr_luma_mult;
  fgm_current->cr_offset = pars->cr_offset;
  fgm_current->overlap_flag = pars->overlap_flag;
  fgm_current->clip_to_restricted_range = pars->clip_to_restricted_range;
  fgm_current->mc_identity = pars->mc_identity;
  fgm_current->fgm_scale_from_channel0_flag =
      pars->fgm_scale_from_channel0_flag;
  fgm_current->grain_scale_shift = pars->grain_scale_shift;
  fgm_current->block_size = pars->block_size;
}

int film_grain_model_decision(int fgm_pos, struct film_grain_model *fgm_in_list,
                              struct film_grain_model *fgm_current) {
  // 0  the contents of both memory blocks are equal
  //  int is_diff = memcmp(fgm_in_list, fgm_current, sizeof(struct
  //  film_grain_model)); return is_diff? -1 : fgm_pos;

  //  //comparison
  for (int c = 0; c < 3; c++) {
    if (fgm_current->fgm_points[c] != fgm_in_list->fgm_points[c]) return -1;
  }
  for (int i = 0; i < 14; i++) {
    for (int j = 0; j < 2; j++) {
      if (fgm_current->fgm_scaling_points[0][i][j] !=
          fgm_in_list->fgm_scaling_points[0][i][j])
        return -1;
    }
  }
  for (int c = 1; c < 3; c++) {
    for (int i = 0; i < 10; i++) {
      for (int j = 0; j < 2; j++) {
        if (fgm_current->fgm_scaling_points[c][i][j] !=
            fgm_in_list->fgm_scaling_points[c][i][j])
          return -1;
      }
    }
  }
  if (fgm_current->scaling_shift != fgm_in_list->scaling_shift) return -1;
  if (fgm_current->ar_coeff_lag != fgm_in_list->ar_coeff_lag) return -1;

  for (int i = 0; i < 24; i++)
    if (fgm_current->ar_coeffs_y[i] != fgm_in_list->ar_coeffs_y[i]) return -1;
  for (int i = 0; i < 25; i++) {
    if (fgm_current->ar_coeffs_cb[i] != fgm_in_list->ar_coeffs_cb[i]) return -1;
    if (fgm_current->ar_coeffs_cr[i] != fgm_in_list->ar_coeffs_cr[i]) return -1;
  }
  if (fgm_current->ar_coeff_shift != fgm_in_list->ar_coeff_shift) return -1;
  if (fgm_current->cb_mult != fgm_in_list->cb_mult) return -1;
  if (fgm_current->cb_luma_mult != fgm_in_list->cb_luma_mult) return -1;
  if (fgm_current->cb_offset != fgm_in_list->cb_offset) return -1;
  if (fgm_current->cr_mult != fgm_in_list->cr_mult) return -1;
  if (fgm_current->cr_luma_mult != fgm_in_list->cr_luma_mult) return -1;
  if (fgm_current->cr_offset != fgm_in_list->cr_offset) return -1;
  if (fgm_current->overlap_flag != fgm_in_list->overlap_flag) return -1;
  if (fgm_current->clip_to_restricted_range !=
      fgm_in_list->clip_to_restricted_range)
    return -1;
  if (fgm_current->mc_identity != fgm_in_list->mc_identity) return -1;
  if (fgm_current->fgm_scale_from_channel0_flag !=
      fgm_in_list->fgm_scale_from_channel0_flag)
    return -1;
  if (fgm_current->grain_scale_shift != fgm_in_list->grain_scale_shift)
    return -1;
  if (fgm_current->block_size != fgm_in_list->block_size) return -1;
  return fgm_pos;
}

int write_fgm_obu(AV2_COMP *cpi, struct film_grain_model *fgm,
                  uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  AV2_COMMON *cm = &cpi->common;

  int fgm_bit_map = 1 << (fgm->fgm_id);
  avm_wb_write_literal(&wb, fgm_bit_map, MAX_FGM_NUM);
  int chroma_format_idc = CHROMA_FORMAT_420;
  if (cm->seq_params.monochrome)
    chroma_format_idc = CHROMA_FORMAT_400;
  else if (cm->seq_params.subsampling_x == 0 &&
           cm->seq_params.subsampling_y == 1)
    chroma_format_idc = CHROMA_FORMAT_444;
  else if (cm->seq_params.subsampling_x == 1 &&
           cm->seq_params.subsampling_y == 0)
    chroma_format_idc = CHROMA_FORMAT_422;
  avm_wb_write_uvlc(&wb, chroma_format_idc);

  for (int fgm_pos = 0; fgm_pos < 1; fgm_pos++) {
    // Scaling functions parameters
    int fgmNumChannels = cm->seq_params.monochrome ? 1 : 3;

    if (fgmNumChannels > 1) {
      avm_wb_write_bit(&wb, fgm->fgm_scale_from_channel0_flag);
    } else {
      assert(!fgm->fgm_scale_from_channel0_flag);
    }

    int fgmNumScalingChannels =
        fgm->fgm_scale_from_channel0_flag ? 1 : fgmNumChannels;

    for (int c = 0; c < fgmNumScalingChannels; c++) {
      avm_wb_write_literal(&wb, fgm->fgm_points[c], 4);  // max 14

      if (fgm->fgm_points[c]) {
        // search for the max
        int maxIncr = -1, maxScal = -1;
        for (int i = 0; i < fgm->fgm_points[c]; i++) {
          if (maxIncr < fgm->fgm_scaling_points[c][i][0])
            maxIncr = fgm->fgm_scaling_points[c][i][0];
          if (maxScal < fgm->fgm_scaling_points[c][i][1])
            maxScal = fgm->fgm_scaling_points[c][i][1];
        }
        // ceillog2
        int bitsIncr = maxIncr == -1 ? 0 : avm_ceil_log2(maxIncr + 1);
        int bitsScal = maxScal == -1 ? 0 : avm_ceil_log2(maxScal + 1);
        avm_wb_write_literal(&wb, bitsIncr - 1, 3);
        avm_wb_write_literal(&wb, bitsScal - 5, 2);
        for (int i = 0; i < fgm->fgm_points[c]; i++) {
          if (i == 0)
            avm_wb_write_literal(&wb, fgm->fgm_scaling_points[c][i][0],
                                 bitsIncr);
          else {
            avm_wb_write_literal(&wb,
                                 fgm->fgm_scaling_points[c][i][0] -
                                     fgm->fgm_scaling_points[c][i - 1][0],
                                 bitsIncr);
          }
          avm_wb_write_literal(&wb, fgm->fgm_scaling_points[c][i][1], bitsScal);
        }
      }
    }

    avm_wb_write_literal(&wb, fgm->scaling_shift - 8, 2);  // 8 + value

    // AR coefficients
    // Only sent if the corresponsing scaling function has
    // more than 0 points

    avm_wb_write_literal(&wb, fgm->ar_coeff_lag, 2);

    int num_pos_luma = 2 * fgm->ar_coeff_lag * (fgm->ar_coeff_lag + 1);
    int num_pos_chroma = num_pos_luma;
    if (fgm->fgm_points[0] > 0) ++num_pos_chroma;
    if (fgm->fgm_points[0]) {
      int maxAr = -1;
      for (int i = 0; i < num_pos_luma; i++) {
        if (maxAr < fgm->ar_coeffs_y[i]) maxAr = fgm->ar_coeffs_y[i];
      }
      // ceillog2
      int bitArY = maxAr == -1 ? 0 : avm_ceil_log2(maxAr + 1 + 128);
      avm_wb_write_literal(&wb, bitArY - 5, 2);
      for (int i = 0; i < num_pos_luma; i++)
        avm_wb_write_literal(&wb, fgm->ar_coeffs_y[i] + 128, bitArY);
    }
    if (fgm->fgm_points[1] || fgm->fgm_scale_from_channel0_flag) {
      int maxAr = -1;
      for (int i = 0; i < num_pos_chroma; i++) {
        if (maxAr < fgm->ar_coeffs_cb[i]) maxAr = fgm->ar_coeffs_cb[i];
      }
      // ceillog2
      int bitsArCb = maxAr == -1 ? 0 : avm_ceil_log2(maxAr + 1 + 128);
      avm_wb_write_literal(&wb, bitsArCb - 5, 2);
      for (int i = 0; i < num_pos_chroma; i++)
        avm_wb_write_literal(&wb, fgm->ar_coeffs_cb[i] + 128, bitsArCb);
    }

    if (fgm->fgm_points[2] || fgm->fgm_scale_from_channel0_flag) {
      int maxAr = -1;
      for (int i = 0; i < num_pos_chroma; i++) {
        if (maxAr < fgm->ar_coeffs_cr[i]) maxAr = fgm->ar_coeffs_cr[i];
      }
      // ceillog2
      int bitsArCr = maxAr == -1 ? 0 : avm_ceil_log2(maxAr + 1 + 128);
      avm_wb_write_literal(&wb, bitsArCr - 5, 2);
      for (int i = 0; i < num_pos_chroma; i++)
        avm_wb_write_literal(&wb, fgm->ar_coeffs_cr[i] + 128, bitsArCr);
    }

    avm_wb_write_literal(&wb, fgm->ar_coeff_shift - 6, 2);  // 8 + value

    avm_wb_write_literal(&wb, fgm->grain_scale_shift, 2);
    if (fgm->fgm_points[1]) {
      avm_wb_write_literal(&wb, fgm->cb_mult, 8);
      avm_wb_write_literal(&wb, fgm->cb_luma_mult, 8);
      avm_wb_write_literal(&wb, fgm->cb_offset, 9);
    }
    if (fgm->fgm_points[2]) {
      avm_wb_write_literal(&wb, fgm->cr_mult, 8);
      avm_wb_write_literal(&wb, fgm->cr_luma_mult, 8);
      avm_wb_write_literal(&wb, fgm->cr_offset, 9);
    }

    avm_wb_write_bit(&wb, fgm->overlap_flag);

    avm_wb_write_bit(&wb, fgm->clip_to_restricted_range);
    if (fgm->clip_to_restricted_range) avm_wb_write_bit(&wb, fgm->mc_identity);

    avm_wb_write_bit(&wb, fgm->block_size);
  }

  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}
#endif  // CONFIG_F153_FGM_OBU
