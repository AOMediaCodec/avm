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

#include "config/aom_config.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_dsp/bitreader_buffer.h"
#include "aom_ports/mem_ops.h"

#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/common/timing.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/obu.h"

#if CONFIG_F153_FGM_OBU
void copy_fgm_from_list(AV1_COMMON *cm, aom_film_grain_t *pars,
                        const struct film_grain_model *fgm) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  pars->bit_depth = seq_params->bit_depth;
#if CONFIG_CWG_F298_REC11
  for (int c = 0; c < 3; c++) {
    pars->fgm_points[c] = fgm->fgm_points[c];
  }

  for (int i = 0; i < 14; i++) {
    for (int j = 0; j < 2; j++) {
      pars->fgm_scaling_points_0[i][j] = fgm->fgm_scaling_points[0][i][j];
      pars->fgm_scaling_points_1[i][j] = fgm->fgm_scaling_points[1][i][j];
      pars->fgm_scaling_points_2[i][j] = fgm->fgm_scaling_points[2][i][j];

    }  // j
  }  // i

#else
  for (int i = 0; i < 14; i++) {
    pars->scaling_points_y[i][0] = fgm->scaling_points_y[i][0];
    pars->scaling_points_y[i][1] = fgm->scaling_points_y[i][1];
  }

  pars->num_y_points = fgm->num_y_points;  // value: 0..14
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 2; j++) {
      pars->scaling_points_cb[i][j] = fgm->scaling_points_cb[i][j];
      pars->scaling_points_cr[i][j] = fgm->scaling_points_cr[i][j];
    }
  }
  pars->num_cb_points = fgm->num_cb_points;
  pars->num_cr_points = fgm->num_cr_points;
#endif  // CONFIG_CWG_F298_REC11
  pars->scaling_shift = fgm->scaling_shift;
  pars->ar_coeff_lag = fgm->ar_coeff_lag;

  // 8 bit values
  for (int i = 0; i < 24; i++) pars->ar_coeffs_y[i] = fgm->ar_coeffs_y[i];
  for (int i = 0; i < 25; i++) {
    pars->ar_coeffs_cb[i] = fgm->ar_coeffs_cb[i];
    pars->ar_coeffs_cr[i] = fgm->ar_coeffs_cr[i];
  }
  pars->ar_coeff_shift = fgm->ar_coeff_shift;
  pars->cb_mult = fgm->cb_mult;
  pars->cb_luma_mult = fgm->cb_luma_mult;
  pars->cb_offset = fgm->cb_offset;
  pars->cr_mult = fgm->cr_mult;
  pars->cr_luma_mult = fgm->cr_luma_mult;
  pars->cr_offset = fgm->cr_offset;
  pars->overlap_flag = fgm->overlap_flag;
  pars->clip_to_restricted_range = fgm->clip_to_restricted_range;
#if CONFIG_FGS_IDENT
  pars->mc_identity = fgm->mc_identity;
#endif  // CONFIG_FGS_IDENT
#if CONFIG_CWG_F298_REC11
  pars->fgm_scale_from_channel0_flag = fgm->fgm_scale_from_channel0_flag;
#else
  pars->chroma_scaling_from_luma = fgm->chroma_scaling_from_luma;
#endif
  pars->grain_scale_shift = fgm->grain_scale_shift;
  pars->block_size = fgm->block_size;
}

static void read_film_grain_model(struct film_grain_model *fgm, int chroma_idc,
                                  struct aom_read_bit_buffer *rb,
                                  struct aom_internal_error_info *error_info) {
  int monochrome = chroma_idc == CHROMA_FORMAT_400;
  int subsampling_x = chroma_idc == CHROMA_FORMAT_444 ? 0 : 1;
  int subsampling_y =
      (chroma_idc == CHROMA_FORMAT_422 || chroma_idc == CHROMA_FORMAT_444) ? 0
                                                                           : 1;

  // Scaling functions parameters
#if CONFIG_CWG_F298_REC11
  int fgmNumChannels = monochrome ? 1 : 3;

  if (fgmNumChannels > 1) {
    fgm->fgm_scale_from_channel0_flag = aom_rb_read_bit(rb);
  } else {
    fgm->fgm_scale_from_channel0_flag = 0;
  }

  int fgmNumScalingChannels =
      fgm->fgm_scale_from_channel0_flag ? 1 : fgmNumChannels;
  for (int c = 0; c < fgmNumScalingChannels; c++) {
    fgm->fgm_points[c] = aom_rb_read_literal(rb, 4);  // max 14
    if (fgm->fgm_points[c] > 14) {
      aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                         "Number of points for film grain %s scaling "
                         "function exceeds the maximum value.",
                         c == 0   ? "y"
                         : c == 1 ? "cb"
                                  : "cr");
    }
    if (fgm->fgm_points[c]) {
      int point_value_increment_bits_minus1 = aom_rb_read_literal(rb, 3);
      int point_scaling_bits_minus5 = aom_rb_read_literal(rb, 2);
      int bitsIncr = point_value_increment_bits_minus1 + 1;
      int bitsScal = point_scaling_bits_minus5 + 5;
      for (int i = 0; i < fgm->fgm_points[c]; i++) {
        int fgm_value_increment = aom_rb_read_literal(rb, bitsIncr);
        if (i == 0)
          fgm->fgm_scaling_points[c][i][0] = fgm_value_increment;
        else
          fgm->fgm_scaling_points[c][i][0] =
              fgm->fgm_scaling_points[c][i - 1][0] + fgm_value_increment;

        if (i && fgm->fgm_scaling_points[c][i - 1][0] >=
                     fgm->fgm_scaling_points[c][i][0]) {
          aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                             "First coordinate of the %s scaling function "
                             "points shall be increasing.",
                             c == 0   ? "y"
                             : c == 1 ? "cb"
                                      : "cr");
        }
        fgm->fgm_scaling_points[c][i][1] = aom_rb_read_literal(rb, bitsScal);
      }
    }
  }  // c < fgmNumScalingChannels

  // Initialize unsignaled values to zero
  for (int c = fgmNumScalingChannels; c < 3; c++) fgm->fgm_points[c] = 0;

  if ((subsampling_x == 1) && (subsampling_y == 1) &&
      (((fgm->fgm_points[1] == 0) && (fgm->fgm_points[2] != 0)) ||
       ((fgm->fgm_points[1] != 0) && (fgm->fgm_points[2] == 0)))) {
    aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                       "In YCbCr 4:2:0, film grain shall be applied "
                       "to both chroma components or neither.");
  }
#else                                                   // CONFIG_CWG_F298_REC11
  fgm->num_y_points = aom_rb_read_literal(rb, 4);  // max 14
  if (fgm->num_y_points > 14) {
    aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                       "Number of points for film grain %s scaling "
                       "function exceeds the maximum value.",
                       "y");
  }
  if (fgm->num_y_points) {
    int point_y_value_increment_bits_minus1 = aom_rb_read_literal(rb, 3);
    int point_y_scaling_bits_minus5 = aom_rb_read_literal(rb, 2);
    int bitsIncr = point_y_value_increment_bits_minus1 + 1;
    int bitsScal = point_y_scaling_bits_minus5 + 5;
    for (int i = 0; i < fgm->num_y_points; i++) {
      fgm->scaling_points_y[i][0] = aom_rb_read_literal(rb, bitsIncr);
      if (i && fgm->scaling_points_y[i - 1][0] >= fgm->scaling_points_y[i][0]) {
        aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                           "First coordinate of the %s scaling function "
                           "points shall be increasing.",
                           "y");
      }
      fgm->scaling_points_y[i][1] = aom_rb_read_literal(rb, bitsScal);
    }
  }

  if (!monochrome)
    fgm->chroma_scaling_from_luma = aom_rb_read_bit(rb);
  else
    fgm->chroma_scaling_from_luma = 0;

  if (monochrome || fgm->chroma_scaling_from_luma ||
      ((subsampling_x == 1) && (subsampling_y == 1) &&
       (fgm->num_y_points == 0))) {
    fgm->num_cb_points = 0;
    fgm->num_cr_points = 0;
  } else {
    fgm->num_cb_points = aom_rb_read_literal(rb, 4);  // max 10
    if (fgm->num_cb_points > 10) {
      aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                         "Number of points for film grain %s scaling "
                         "function exceeds the maximum value.",
                         "cb");
    }
    if (fgm->num_cb_points) {
      int point_cb_value_increment_bits_minus1 = aom_rb_read_literal(rb, 3);
      int point_cb_scaling_bits_minus5 = aom_rb_read_literal(rb, 2);
      int bitsIncr = point_cb_value_increment_bits_minus1 + 1;
      int bitsScal = point_cb_scaling_bits_minus5 + 5;
      for (int i = 0; i < fgm->num_cb_points; i++) {
        fgm->scaling_points_cb[i][0] = aom_rb_read_literal(rb, bitsIncr);
        if (i &&
            fgm->scaling_points_cb[i - 1][0] >= fgm->scaling_points_cb[i][0]) {
          aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                             "First coordinate of the %s scaling function "
                             "points shall be increasing.",
                             "cb");
        }
        fgm->scaling_points_cb[i][1] = aom_rb_read_literal(rb, bitsScal);
      }
    }

    fgm->num_cr_points = aom_rb_read_literal(rb, 4);  // max 10
    if (fgm->num_cr_points > 10) {
      aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                         "Number of points for film grain %s scaling "
                         "function exceeds the maximum value.",
                         "cr");
    }
    if (fgm->num_cr_points) {
      int point_cr_value_increment_bits_minus1 = aom_rb_read_literal(rb, 3);
      int point_cr_scaling_bits_minus5 = aom_rb_read_literal(rb, 2);
      int bitsIncr = point_cr_value_increment_bits_minus1 + 1;
      int bitsScal = point_cr_scaling_bits_minus5 + 5;
      for (int i = 0; i < fgm->num_cr_points; i++) {
        fgm->scaling_points_cr[i][0] = aom_rb_read_literal(rb, bitsIncr);
        if (i &&
            fgm->scaling_points_cr[i - 1][0] >= fgm->scaling_points_cr[i][0]) {
          aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                             "First coordinate of the %s scaling function "
                             "points shall be increasing.",
                             "cr");
        }
        fgm->scaling_points_cr[i][1] = aom_rb_read_literal(rb, bitsScal);
      }
    }

    if ((subsampling_x == 1) && (subsampling_y == 1) &&
        (((fgm->num_cb_points == 0) && (fgm->num_cr_points != 0)) ||
         ((fgm->num_cb_points != 0) && (fgm->num_cr_points == 0)))) {
      aom_internal_error(error_info, AOM_CODEC_UNSUP_BITSTREAM,
                         "In YCbCr 4:2:0, film grain shall be applied "
                         "to both chroma components or neither.");
    }
  }
#endif                                                  // CONFIG_CWG_F298_REC11
  fgm->scaling_shift = aom_rb_read_literal(rb, 2) + 8;  // 8 + value

  // AR coefficients
  // Only sent if the corresponsing scaling function has
  // more than 0 points

  fgm->ar_coeff_lag = aom_rb_read_literal(rb, 2);

  int num_pos_luma = 2 * fgm->ar_coeff_lag * (fgm->ar_coeff_lag + 1);
  int num_pos_chroma = num_pos_luma;

#if CONFIG_CWG_F298_REC11
  if (fgm->fgm_points[0])
#else
  if (fgm->num_y_points)
#endif
  {
    ++num_pos_chroma;
    int bits_per_ar_coeff_y_minus5 = aom_rb_read_literal(rb, 2);
    int BitsArY = bits_per_ar_coeff_y_minus5 + 5;
    for (int i = 0; i < num_pos_luma; i++)
      fgm->ar_coeffs_y[i] = aom_rb_read_literal(rb, BitsArY) - 128;
  }

#if CONFIG_CWG_F298_REC11
  if (fgm->fgm_points[1] || fgm->fgm_scale_from_channel0_flag)
#else
  if (fgm->num_cb_points || fgm->chroma_scaling_from_luma)
#endif
  {
    int bits_per_ar_coeff_cb_minus5 = aom_rb_read_literal(rb, 2);
    int BitsArCb = bits_per_ar_coeff_cb_minus5 + 5;
    for (int i = 0; i < num_pos_chroma; i++)
      fgm->ar_coeffs_cb[i] = aom_rb_read_literal(rb, BitsArCb) - 128;
  }

#if CONFIG_CWG_F298_REC11
  if (fgm->fgm_points[2] || fgm->fgm_scale_from_channel0_flag)
#else
  if (fgm->num_cr_points || fgm->chroma_scaling_from_luma)
#endif
  {
    int bits_per_ar_coeff_cr_minus5 = aom_rb_read_literal(rb, 2);
    int BitsArCr = bits_per_ar_coeff_cr_minus5 + 5;
    for (int i = 0; i < num_pos_chroma; i++)
      fgm->ar_coeffs_cr[i] = aom_rb_read_literal(rb, BitsArCr) - 128;
  }

  fgm->ar_coeff_shift = aom_rb_read_literal(rb, 2) + 6;  // 6 + value

  fgm->grain_scale_shift = aom_rb_read_literal(rb, 2);
#if CONFIG_CWG_F298_REC11
  if (fgm->fgm_points[1] > 0)
#else
  if (fgm->num_cb_points)
#endif
  {
    fgm->cb_mult = aom_rb_read_literal(rb, 8);
    fgm->cb_luma_mult = aom_rb_read_literal(rb, 8);
    fgm->cb_offset = aom_rb_read_literal(rb, 9);
  }
#if CONFIG_CWG_F298_REC11
  if (fgm->fgm_points[2] > 0)
#else
  if (fgm->num_cr_points)
#endif
  {
    fgm->cr_mult = aom_rb_read_literal(rb, 8);
    fgm->cr_luma_mult = aom_rb_read_literal(rb, 8);
    fgm->cr_offset = aom_rb_read_literal(rb, 9);
  }

  fgm->overlap_flag = aom_rb_read_bit(rb);

  fgm->clip_to_restricted_range = aom_rb_read_bit(rb);

#if CONFIG_FGS_IDENT
  if (fgm->clip_to_restricted_range)
    fgm->mc_identity = aom_rb_read_bit(rb);
  else
    fgm->mc_identity = 0;
#endif  // CONFIG_FGS_IDENT

  fgm->block_size = aom_rb_read_bit(rb);
}

// acc_fgm_id_bitmap is an in/out parameter. The caller should set
// *acc_fgm_id_bitmap to 0 before the first call to read_fgm_obu(). Each
// read_fgm_obu() call updates *acc_fgm_id_bitmap by bitwise-ORing the
// fgm_bit_map from the FGM OBU with *acc_fgm_id_bitmap.
uint32_t read_fgm_obu(AV1Decoder *pbi, const int obu_tlayer_id,
                      const int obu_mlayer_id, uint32_t *acc_fgm_id_bitmap,
                      int fgm_seq_id_in_tu, struct aom_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;
  int fgm_bit_map = aom_rb_read_literal(rb, MAX_FGM_NUM);
  if (*acc_fgm_id_bitmap & (uint32_t)fgm_bit_map) {
    aom_internal_error(
        &pbi->common.error, AOM_CODEC_INVALID_PARAM,
        "fgm_bit_map(%d) overlaps the accumulated fgm_bit_map(%d)", fgm_bit_map,
        acc_fgm_id_bitmap);
  } else {
    *acc_fgm_id_bitmap |= fgm_bit_map;
  }
  int fgm_chroma_idc = aom_rb_read_uvlc(rb);
  if (fgm_chroma_idc >= NUM_CHROMA_FORMATS) {
    aom_internal_error(&pbi->common.error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Invalid fgm_chroma_idc [%d].", fgm_chroma_idc);
  }
  for (int j = 0; j < MAX_FGM_NUM; j++) {
    // This process overwrites the position(pbi->fg_list[fgm_id]) if the fgm_id
    // is the same.
    if (fgm_bit_map & (1 << j)) {
      int fgm_id = j;
      memset(&pbi->fgm_list[fgm_id], 0, sizeof(pbi->fgm_list[fgm_id]));
      pbi->fgm_list[fgm_id].fgm_seq_id_in_tu = fgm_seq_id_in_tu;
      pbi->fgm_list[fgm_id].fgm_id = fgm_id;
      pbi->fgm_list[fgm_id].fgm_mlayer_id = obu_mlayer_id;
      pbi->fgm_list[fgm_id].fgm_tlayer_id = obu_tlayer_id;
      pbi->fgm_list[fgm_id].fgm_chroma_idc = fgm_chroma_idc;
      read_film_grain_model(&pbi->fgm_list[fgm_id], fgm_chroma_idc, rb,
                            &pbi->common.error);
    }  // if
  }  // j
  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
#endif  // CONFIG_F153_FGM_OBU
