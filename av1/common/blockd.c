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

#include <math.h>

#include "aom_ports/system_state.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/enums.h"

#if CONFIG_AIMC
PREDICTION_MODE av1_get_joint_mode(const MB_MODE_INFO *mi) {
  if (!mi) return DC_PRED;
  if (is_inter_block(mi, SHARED_PART) || is_intrabc_block(mi, SHARED_PART))
    return DC_PRED;
  return mi->joint_y_mode_delta_angle;
}
#else
PREDICTION_MODE av1_get_block_mode(const MB_MODE_INFO *mi) {
  if (!mi) return DC_PRED;
  assert(!is_inter_block(mi, SHARED_PART) || is_intrabc_block(mi, SHARED_PART));
  return mi->mode;
}
#endif  // CONFIG_AIMC

void av1_reset_is_mi_coded_map(MACROBLOCKD *xd, int stride) {
  av1_zero(xd->is_mi_coded);
  xd->is_mi_coded_stride = stride;
}

void av1_mark_block_as_coded(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                             BLOCK_SIZE sb_size) {
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int sb_mi_size = mi_size_wide[sb_size];
  const int mi_row_offset = mi_row & (sb_mi_size - 1);
  const int mi_col_offset = mi_col & (sb_mi_size - 1);

  for (int r = 0; r < mi_size_high[bsize]; ++r)
    for (int c = 0; c < mi_size_wide[bsize]; ++c) {
      const int pos =
          (mi_row_offset + r) * xd->is_mi_coded_stride + mi_col_offset + c;
      switch (xd->tree_type) {
        case SHARED_PART:
          xd->is_mi_coded[0][pos] = 1;
          xd->is_mi_coded[1][pos] = 1;
          break;
        case LUMA_PART: xd->is_mi_coded[0][pos] = 1; break;
        case CHROMA_PART: xd->is_mi_coded[1][pos] = 1; break;
        default: assert(0 && "Invalid tree type");
      }
    }
}

void av1_mark_block_as_not_coded(MACROBLOCKD *xd, int mi_row, int mi_col,
                                 BLOCK_SIZE bsize, BLOCK_SIZE sb_size) {
  const int sb_mi_size = mi_size_wide[sb_size];
  const int mi_row_offset = mi_row & (sb_mi_size - 1);
  const int mi_col_offset = mi_col & (sb_mi_size - 1);

  for (int r = 0; r < mi_size_high[bsize]; ++r) {
    const int pos =
        (mi_row_offset + r) * xd->is_mi_coded_stride + mi_col_offset;
    uint8_t *row_ptr_luma = &xd->is_mi_coded[0][pos];
    uint8_t *row_ptr_chroma = &xd->is_mi_coded[1][pos];
    switch (xd->tree_type) {
      case SHARED_PART:
        av1_zero_array(row_ptr_luma, mi_size_wide[bsize]);
        av1_zero_array(row_ptr_chroma, mi_size_wide[bsize]);
        break;
      case LUMA_PART: av1_zero_array(row_ptr_luma, mi_size_wide[bsize]); break;
      case CHROMA_PART:
        av1_zero_array(row_ptr_chroma, mi_size_wide[bsize]);
        break;
      default: assert(0 && "Invalid tree type");
    }
  }
}

PARTITION_TREE *av1_alloc_ptree_node(PARTITION_TREE *parent, int index) {
  PARTITION_TREE *ptree = NULL;
  struct aom_internal_error_info error;

  AOM_CHECK_MEM_ERROR(&error, ptree, aom_calloc(1, sizeof(*ptree)));

  ptree->parent = parent;
  ptree->index = index;
  ptree->partition = PARTITION_INVALID;
  ptree->is_settled = 0;
  for (int i = 0; i < 4; ++i) ptree->sub_tree[i] = NULL;

  return ptree;
}

void av1_free_ptree_recursive(PARTITION_TREE *ptree) {
  if (ptree == NULL) return;

  for (int i = 0; i < 4; ++i) {
    av1_free_ptree_recursive(ptree->sub_tree[i]);
    ptree->sub_tree[i] = NULL;
  }

  aom_free(ptree);
}

void av1_reset_ptree_in_sbi(SB_INFO *sbi, TREE_TYPE tree_type) {
  const int idx = av1_get_sdp_idx(tree_type);
  if (sbi->ptree_root[idx]) av1_free_ptree_recursive(sbi->ptree_root[idx]);

  sbi->ptree_root[idx] = av1_alloc_ptree_node(NULL, 0);
}

void av1_set_entropy_contexts(const MACROBLOCKD *xd,
                              struct macroblockd_plane *pd, int plane,
                              BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                              int has_eob, int aoff, int loff) {
  ENTROPY_CONTEXT *const a = pd->above_entropy_context + aoff;
  ENTROPY_CONTEXT *const l = pd->left_entropy_context + loff;
  const int txs_wide = tx_size_wide_unit[tx_size];
  const int txs_high = tx_size_high_unit[tx_size];

  // above
  if (has_eob && xd->mb_to_right_edge < 0) {
    const int blocks_wide = max_block_wide(xd, plane_bsize, plane);
    const int above_contexts = AOMMIN(txs_wide, blocks_wide - aoff);
    memset(a, has_eob, sizeof(*a) * above_contexts);
    memset(a + above_contexts, 0, sizeof(*a) * (txs_wide - above_contexts));
  } else {
    memset(a, has_eob, sizeof(*a) * txs_wide);
  }

  // left
  if (has_eob && xd->mb_to_bottom_edge < 0) {
    const int blocks_high = max_block_high(xd, plane_bsize, plane);
    const int left_contexts = AOMMIN(txs_high, blocks_high - loff);
    memset(l, has_eob, sizeof(*l) * left_contexts);
    memset(l + left_contexts, 0, sizeof(*l) * (txs_high - left_contexts));
  } else {
    memset(l, has_eob, sizeof(*l) * txs_high);
  }
}

void av1_reset_entropy_context(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                               const int num_planes) {
#if CONFIG_EXT_RECUR_PARTITIONS
  // TODO(chiyotsai): This part is needed to avoid encoder/decoder mismatch.
  // Investigate why this is the case. It seems like on the decoder side, the
  // decoder is failing to clear the context after encoding a skip_txfm chroma
  // block.
  const int plane_start = (xd->tree_type == CHROMA_PART);
  int plane_end = 0;
  switch (xd->tree_type) {
    case LUMA_PART: plane_end = 1; break;
    case CHROMA_PART: plane_end = num_planes; break;
    case SHARED_PART:
      plane_end = 1 + (num_planes - 1) * xd->is_chroma_ref;
      break;
    default: assert(0);
  }
  for (int i = plane_start; i < plane_end; ++i) {
#else
  const int nplanes = 1 + (num_planes - 1) * xd->is_chroma_ref;
  for (int i = 0; i < nplanes; i++) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    struct macroblockd_plane *const pd = &xd->plane[i];
    const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
        xd, xd->mi[0], i, pd->subsampling_x, pd->subsampling_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
    assert(plane_bsize ==
           get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    (void)bsize;
    const int txs_wide = mi_size_wide[plane_bsize];
    const int txs_high = mi_size_high[plane_bsize];
    memset(pd->above_entropy_context, 0, sizeof(ENTROPY_CONTEXT) * txs_wide);
    memset(pd->left_entropy_context, 0, sizeof(ENTROPY_CONTEXT) * txs_high);
  }
}

void av1_reset_loop_filter_delta(MACROBLOCKD *xd, int num_planes) {
  xd->delta_lf_from_base = 0;
  const int frame_lf_count =
      num_planes > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
  for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) xd->delta_lf[lf_id] = 0;
}

// Resets the LR decoding state before decoding each coded tile and
// associated LR coefficients
void av1_reset_loop_restoration(MACROBLOCKD *xd, int plane_start, int plane_end,
                                const int *num_filter_classes) {
  for (int p = plane_start; p < plane_end; ++p) {
    av1_reset_wiener_bank(&xd->wiener_info[p], p != AOM_PLANE_Y);
    av1_reset_sgrproj_bank(&xd->sgrproj_info[p]);
    av1_reset_wienerns_bank(&xd->wienerns_info[p], xd->current_base_qindex,
                            num_filter_classes[p], p != AOM_PLANE_Y);
  }
}

// Initialize bank
void av1_reset_wiener_bank(WienerInfoBank *bank, int chroma) {
  set_default_wiener(&bank->filter[0], chroma);
  bank->bank_size = 0;
  bank->bank_ptr = 0;
}

// Add a new filter to bank
void av1_add_to_wiener_bank(WienerInfoBank *bank, const WienerInfo *info) {
  if (bank->bank_size < LR_BANK_SIZE) {
    bank->bank_ptr = bank->bank_size;
    memcpy(&bank->filter[bank->bank_ptr], info, sizeof(*info));
    bank->bank_size++;
  } else {
    bank->bank_ptr = (bank->bank_ptr + 1) % LR_BANK_SIZE;
    memcpy(&bank->filter[bank->bank_ptr], info, sizeof(*info));
  }
}

// Get a reference to a filter given the index
WienerInfo *av1_ref_from_wiener_bank(WienerInfoBank *bank, int ndx) {
  if (bank->bank_size == 0) {
    return &bank->filter[0];
  } else {
    assert(ndx < bank->bank_size);
    const int ptr =
        bank->bank_ptr - ndx + (bank->bank_ptr < ndx ? LR_BANK_SIZE : 0);
    return &bank->filter[ptr];
  }
}

// Get a const reference to a filter given the index
const WienerInfo *av1_constref_from_wiener_bank(const WienerInfoBank *bank,
                                                int ndx) {
  if (bank->bank_size == 0) {
    return &bank->filter[0];
  } else {
    assert(ndx < bank->bank_size);
    const int ptr =
        bank->bank_ptr - ndx + (bank->bank_ptr < ndx ? LR_BANK_SIZE : 0);
    return &bank->filter[ptr];
  }
}

// Directly replace a filter in the bank at given index
void av1_upd_to_wiener_bank(WienerInfoBank *bank, int ndx,
                            const WienerInfo *info) {
  memcpy(av1_ref_from_wiener_bank(bank, ndx), info, sizeof(*info));
}

// Initialize bank
void av1_reset_sgrproj_bank(SgrprojInfoBank *bank) {
  set_default_sgrproj(&bank->filter[0]);
  bank->bank_size = 0;
  bank->bank_ptr = 0;
}

// Add a new filter to bank
void av1_add_to_sgrproj_bank(SgrprojInfoBank *bank, const SgrprojInfo *info) {
  if (bank->bank_size < LR_BANK_SIZE) {
    bank->bank_ptr = bank->bank_size;
    memcpy(&bank->filter[bank->bank_ptr], info, sizeof(*info));
    bank->bank_size++;
  } else {
    bank->bank_ptr = (bank->bank_ptr + 1) % LR_BANK_SIZE;
    memcpy(&bank->filter[bank->bank_ptr], info, sizeof(*info));
  }
}

// Get a reference to a filter given the index
SgrprojInfo *av1_ref_from_sgrproj_bank(SgrprojInfoBank *bank, int ndx) {
  if (bank->bank_size == 0) {
    return &bank->filter[0];
  } else {
    assert(ndx < bank->bank_size);
    const int ptr =
        bank->bank_ptr - ndx + (bank->bank_ptr < ndx ? LR_BANK_SIZE : 0);
    return &bank->filter[ptr];
  }
}

// Get a const reference to a filter given the index
const SgrprojInfo *av1_constref_from_sgrproj_bank(const SgrprojInfoBank *bank,
                                                  int ndx) {
  if (bank->bank_size == 0) {
    return &bank->filter[0];
  } else {
    assert(ndx < bank->bank_size);
    const int ptr =
        bank->bank_ptr - ndx + (bank->bank_ptr < ndx ? LR_BANK_SIZE : 0);
    return &bank->filter[ptr];
  }
}

// Directly replace a filter in the bank at given index
void av1_upd_to_sgrproj_bank(SgrprojInfoBank *bank, int ndx,
                             const SgrprojInfo *info) {
  memcpy(av1_ref_from_sgrproj_bank(bank, ndx), info, sizeof(*info));
}

// Initialize bank
void av1_reset_wienerns_bank(WienerNonsepInfoBank *bank, int qindex,
                             int num_classes, int chroma) {
  for (int i = 0; i < LR_BANK_SIZE; ++i) {
    set_default_wienerns(&bank->filter[i], qindex, num_classes, chroma);
  }

  for (int c_id = 0; c_id < num_classes; ++c_id) {
    bank->bank_size_for_class[c_id] = 0;
    bank->bank_ptr_for_class[c_id] = 0;
  }
}

// Add a new filter to bank
void av1_add_to_wienerns_bank(WienerNonsepInfoBank *bank,
                              const WienerNonsepInfo *info,
                              int wiener_class_id) {
  int c_id_begin = wiener_class_id;
  int c_id_end = wiener_class_id + 1;
  if (wiener_class_id == ALL_WIENERNS_CLASSES) {
    c_id_begin = 0;
    c_id_end = info->num_classes;
  }
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    if (bank->bank_size_for_class[c_id] < LR_BANK_SIZE) {
      bank->bank_ptr_for_class[c_id] = bank->bank_size_for_class[c_id];
      bank->bank_size_for_class[c_id]++;
    } else {
      bank->bank_ptr_for_class[c_id] =
          (bank->bank_ptr_for_class[c_id] + 1) % LR_BANK_SIZE;
    }
    copy_nsfilter_taps_for_class(&bank->filter[bank->bank_ptr_for_class[c_id]],
                                 info, c_id);
  }
}

// Returns the filter that is at slot ndx from last. When ndx is zero the last
// filter added is returned. When ndx is one the filter added before the last
// and so on.
WienerNonsepInfo *av1_ref_from_wienerns_bank(WienerNonsepInfoBank *bank,
                                             int ndx, int wiener_class_id) {
  assert(wiener_class_id != ALL_WIENERNS_CLASSES);
  if (bank->bank_size_for_class[wiener_class_id] == 0) {
    assert(ndx == 0);
    return &bank->filter[0];
  } else {
    assert(ndx < bank->bank_size_for_class[wiener_class_id]);
    const int ptr =
        bank->bank_ptr_for_class[wiener_class_id] - ndx +
        (bank->bank_ptr_for_class[wiener_class_id] < ndx ? LR_BANK_SIZE : 0);
    return &bank->filter[ptr];
  }
}

// Get a const reference to a filter given the index
const WienerNonsepInfo *av1_constref_from_wienerns_bank(
    const WienerNonsepInfoBank *bank, int ndx, int wiener_class_id) {
  assert(wiener_class_id != ALL_WIENERNS_CLASSES);
  if (bank->bank_size_for_class[wiener_class_id] == 0) {
    return &bank->filter[0];
  } else {
    assert(ndx < bank->bank_size_for_class[wiener_class_id]);
    const int ptr =
        bank->bank_ptr_for_class[wiener_class_id] - ndx +
        (bank->bank_ptr_for_class[wiener_class_id] < ndx ? LR_BANK_SIZE : 0);
    return &bank->filter[ptr];
  }
}

// Directly replace a filter in the bank at given index
void av1_upd_to_wienerns_bank(WienerNonsepInfoBank *bank, int ndx,
                              const WienerNonsepInfo *info,
                              int wiener_class_id) {
  copy_nsfilter_taps_for_class(
      av1_ref_from_wienerns_bank(bank, ndx, wiener_class_id), info,
      wiener_class_id);
}

int16_t *nsfilter_taps(WienerNonsepInfo *nsinfo, int wiener_class_id) {
  assert(wiener_class_id >= 0 && wiener_class_id < nsinfo->num_classes);
  return nsinfo->allfiltertaps + wiener_class_id * WIENERNS_TAPS_MAX;
}

const int16_t *const_nsfilter_taps(const WienerNonsepInfo *nsinfo,
                                   int wiener_class_id) {
  assert(wiener_class_id >= 0 && wiener_class_id < nsinfo->num_classes);
  return nsinfo->allfiltertaps + wiener_class_id * WIENERNS_TAPS_MAX;
}

void copy_nsfilter_taps_for_class(WienerNonsepInfo *to_info,
                                  const WienerNonsepInfo *from_info,
                                  int wiener_class_id) {
  assert(wiener_class_id >= 0 && wiener_class_id < to_info->num_classes);
  assert(wiener_class_id >= 0 && wiener_class_id < from_info->num_classes);
  const int offset = wiener_class_id * WIENERNS_TAPS_MAX;
  memcpy(to_info->allfiltertaps + offset, from_info->allfiltertaps + offset,
         WIENERNS_TAPS_MAX * sizeof(*to_info->allfiltertaps));
  to_info->bank_ref_for_class[wiener_class_id] =
      from_info->bank_ref_for_class[wiener_class_id];
}

void copy_nsfilter_taps(WienerNonsepInfo *to_info,
                        const WienerNonsepInfo *from_info) {
  assert(to_info->num_classes == from_info->num_classes);
  memcpy(to_info->allfiltertaps, from_info->allfiltertaps,
         sizeof(to_info->allfiltertaps));
  memcpy(to_info->bank_ref_for_class, from_info->bank_ref_for_class,
         sizeof(to_info->bank_ref_for_class));
}

void av1_setup_block_planes(MACROBLOCKD *xd, int ss_x, int ss_y,
                            const int num_planes) {
  int i;

  for (i = 0; i < num_planes; i++) {
    xd->plane[i].plane_type = get_plane_type(i);
    xd->plane[i].subsampling_x = i ? ss_x : 0;
    xd->plane[i].subsampling_y = i ? ss_y : 0;
  }
  for (i = num_planes; i < MAX_MB_PLANE; i++) {
    xd->plane[i].subsampling_x = 1;
    xd->plane[i].subsampling_y = 1;
  }
}

#if CONFIG_COMBINE_PC_NS_WIENER

int max_dictionary_size(int nopcw) {
  const int max_num_predictors =
      num_dictionary_slots(WIENERNS_MAX_CLASSES, nopcw);
  return max_num_predictors * MAX_NUM_DICTIONARY_TAPS;
}

void allocate_frame_filter_dictionary(AV1_COMMON *cm) {
  // Use max_dictionary_size(0) below instead of max_dictionary_size(nopcw)
  // to always allocate for the largest possible dictionary size.
  // If nopcw is solely based on sequence level parameters, this
  // should not be strictly necessary, since reallocation should
  // happen with every new sequence parameter set. However the current
  // reference decoder does not appear to reallocate when the sequence
  // level parameters change. Hence this change is needed.
  const int nopcw = disable_pcwiener_filters_in_framefilters(&cm->seq_params);
  (void)nopcw;
  cm->frame_filter_dictionary =
      aom_calloc(max_dictionary_size(0), sizeof(*cm->frame_filter_dictionary));
  cm->translated_pcwiener_filters =
      aom_calloc(NUM_PC_WIENER_FILTERS * MAX_NUM_DICTIONARY_TAPS,
                 sizeof(*cm->translated_pcwiener_filters));
  cm->translation_done = 0;
  cm->frame_filter_dictionary_stride = MAX_NUM_DICTIONARY_TAPS;
  cm->num_ref_filters = aom_calloc(1, sizeof(*cm->num_ref_filters));
}

void free_frame_filter_dictionary(AV1_COMMON *cm) {
  aom_free(cm->frame_filter_dictionary);
  aom_free(cm->translated_pcwiener_filters);
  aom_free(cm->num_ref_filters);
  cm->frame_filter_dictionary = NULL;
  cm->translated_pcwiener_filters = NULL;
  cm->num_ref_filters = NULL;
  cm->translation_done = 0;
  cm->frame_filter_dictionary_stride = 0;
}

// TODO: Refactor so that this gets called only once during encoding/decoding.
// Useful when using pre-trained filters (with different config and precision)
// to predict transmitetd filters to reduce side-information.
void translate_pcwiener_filters_to_wienerns(AV1_COMMON *cm) {
  if (cm->translation_done) {
    return;
  }
  const int base_qindex = cm->quant_params.base_qindex;
  const int is_uv = 0;
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(base_qindex, is_uv);
  assert(nsfilter_params->ncoeffs <= MAX_NUM_DICTIONARY_TAPS);
  const int num_feat = nsfilter_params->ncoeffs;

  /* Assuming the pc-wiener tap configuration is the same as the
   * simd tap configuration, the translation below can be deprecated.
   * int tap_translator[WIENERNS_TAPS_MAX];
   * const int num_taps = wienerns_to_pcwiener_tap_config_translator(
   *     &nsfilter_params->nsfilter_config, tap_translator, WIENERNS_TAPS_MAX);
   * (void)num_taps;
   * assert(num_taps == num_feat);
   */
  const int set_index =
      0;  // get_filter_set_index(base_qindex + qindex_offset);

  const int num_pc_wiener_filters = NUM_PC_WIENER_FILTERS;
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;
  const int16_t(*pcwiener_filters_luma)[NUM_PC_WIENER_TAPS_LUMA] =
      get_filter_set(set_index);
  const int precision_diff =
      PC_WIENER_PREC_FILTER - nsfilter_params->nsfilter_config.prec_bits;
  assert(precision_diff >= 0);

  for (int pc_wiener_cnt = 0; pc_wiener_cnt < num_pc_wiener_filters;
       ++pc_wiener_cnt) {
    int filter_index = pc_wiener_cnt;
    const int16_t *pcwiener_filter = pcwiener_filters_luma[filter_index];

    const int dict_index = pc_wiener_cnt;

    assert(cm->translated_pcwiener_filters != NULL);
    for (int i = 0; i < AOMMIN(num_feat, NUM_PC_WIENER_TAPS_LUMA - 1); ++i) {
      const int16_t scaled_tap = ROUND_POWER_OF_TWO_SIGNED(
          pcwiener_filter[i], precision_diff);  // Assuming no translation
      // pcwiener_filter[tap_translator[i]], precision_diff); // deprecated
      cm->translated_pcwiener_filters[dict_index * MAX_NUM_DICTIONARY_TAPS +
                                      i] =
          clip_to_wienerns_range(scaled_tap,
                                 wienerns_coeffs[i][WIENERNS_MIN_ID],
                                 (1 << wienerns_coeffs[i][WIENERNS_BIT_ID]));
    }
  }
  cm->translation_done = 1;
}

static inline int num_sampled_pc_wiener_filters(int plane, int num_ref_filters,
                                                int num_classes, int nopcw) {
  if (plane != AOM_PLANE_Y) return 0;
  if (nopcw) return 0;
  return AOMMIN(
      AOMMAX(max_num_base_filters(num_classes, 0) - num_ref_filters, 0),
      NUM_PC_WIENER_FILTERS);
}

void set_group_counts(int plane, int num_classes, int num_ref_frames,
                      int *group_counts, int nopcw) {
  int total_slots = num_dictionary_slots(num_classes, nopcw);
  (void)total_slots;
  group_counts[0] = num_classes;
  total_slots -= group_counts[0];
  assert(total_slots >= 0);
  group_counts[1] = num_ref_frames;
  total_slots -= group_counts[1];
  assert(total_slots >= 0);
  group_counts[2] =
      num_sampled_pc_wiener_filters(plane, num_ref_frames, num_classes, nopcw);
}

int set_frame_filter_dictionary(int plane, const AV1_COMMON *cm,
                                int num_classes,
                                int16_t *frame_filter_dictionary,
                                int dict_stride) {
  assert(frame_filter_dictionary != NULL);
  assert(dict_stride > 0);
  const int base_qindex = cm->quant_params.base_qindex;
  const int is_uv = plane > 0;
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(base_qindex, is_uv);

  const int nopcw = disable_pcwiener_filters_in_framefilters(&cm->seq_params);
  if (!nopcw) assert(nsfilter_params->ncoeffs <= MAX_NUM_DICTIONARY_TAPS);
  const int num_feat = nsfilter_params->ncoeffs;

  memset(frame_filter_dictionary, 0,
         max_dictionary_size(nopcw) * sizeof(*frame_filter_dictionary));
  const int max_predictors = num_dictionary_slots(num_classes, nopcw);

  // Filters prior to ref_filter_offset are all zeros via calloc. --------------
  const int ref_filter_offset = reference_filters_begin(num_classes);
  // ---------------------------------------------------------------------------

  // Copy available reference filters to the dictionary. -----------------------
  int num_ref_filters = 0;
#if CONFIG_TEMP_LR
  const int min_pc_wiener = plane == AOM_PLANE_Y ? (nopcw ? 0 : 16) : 0;
  assert(min_pc_wiener <= NUM_PC_WIENER_FILTERS);
  const int allowed_num_base_filters =
      max_num_base_filters(num_classes, nopcw) - min_pc_wiener;
  assert(allowed_num_base_filters >= 0);
  assert(allowed_num_base_filters < max_predictors);
  const int num_ref_frames =
      (frame_is_intra_only(cm) || cm->features.error_resilient_mode)
          ? 0
          : cm->ref_frames_info.num_total_refs;
  for (int ref_idx = 0; ref_idx < num_ref_frames; ref_idx++) {
    const RefCntBuffer *ref_frame_buf = get_ref_frame_buf(cm, ref_idx);
    if (ref_frame_buf == NULL) {
      assert(0);
      continue;
    }
    int planes_to_check[2] = { plane, -1 };
    int num_planes_to_check = 1;
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
    const int mix_planes = 1;
    if (plane != AOM_PLANE_Y && mix_planes) {
      num_planes_to_check = 2;
      planes_to_check[1] = (plane == AOM_PLANE_U) ? AOM_PLANE_V : AOM_PLANE_U;
    }
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD
    for (int chk = 0; chk < num_planes_to_check; ++chk) {
      const int p = planes_to_check[chk];
      RestorationInfo rsi = ref_frame_buf->rst_info[p];
      if (rsi.frame_filters_on) {
        for (int c_id = 0; c_id < rsi.num_filter_classes; ++c_id) {
          if (num_ref_filters >= allowed_num_base_filters) break;

          int16_t *match_filter =
              frame_filter_dictionary +
              (num_ref_filters + ref_filter_offset) * dict_stride;
          const int16_t *wienerns_filter =
              const_nsfilter_taps(&rsi.frame_filters, c_id);
          for (int i = 0; i < num_feat; ++i) {
            match_filter[i] = wienerns_filter[i];
          }
          ++num_ref_filters;
        }
      }
    }
  }
#endif  // CONFIG_TEMP_LR
  // ---------------------------------------------------------------------------

  // Sample from the pc-wiener filters for the remaining allowed slots. --------
  const int shuffled_index[] = { 16, 7,  58, 21, 12, 61, 26, 38, 18, 30, 50,
                                 45, 23, 49, 43, 62, 42, 54, 27, 36, 17, 44,
                                 32, 34, 4,  24, 52, 31, 37, 11, 33, 19, 35,
                                 6,  22, 53, 63, 25, 41, 47, 1,  59, 0,  28,
                                 40, 55, 48, 8,  5,  51, 9,  46, 56, 60, 15,
                                 2,  13, 14, 57, 29, 3,  20, 39, 10 };
  const int num_pc_wiener_filters =
      num_sampled_pc_wiener_filters(plane, num_ref_filters, num_classes, nopcw);
  assert(num_pc_wiener_filters >= 0 &&
         num_pc_wiener_filters <= NUM_PC_WIENER_FILTERS);

  assert(cm->translated_pcwiener_filters != NULL);
  assert(cm->translation_done);
  for (int pc_wiener_cnt = 0; pc_wiener_cnt < num_pc_wiener_filters;
       ++pc_wiener_cnt) {
    int filter_index = shuffled_index[pc_wiener_cnt];
    assert(filter_index < NUM_PC_WIENER_FILTERS);
    if (filter_index >= NUM_PC_WIENER_FILTERS) {
      filter_index = NUM_PC_WIENER_FILTERS - 1;
    }

    const int16_t *pcwiener_filter = cm->translated_pcwiener_filters +
                                     filter_index * MAX_NUM_DICTIONARY_TAPS;

    const int dict_index = ref_filter_offset + num_ref_filters + pc_wiener_cnt;
    assert(dict_index < max_predictors);
    if (dict_index >= max_predictors) {
      break;
    }

    int16_t *match_filter = frame_filter_dictionary + dict_index * dict_stride;
    for (int i = 0; i < num_feat; ++i) {
      match_filter[i] = pcwiener_filter[i];
    }
  }
  // ---------------------------------------------------------------------------

  // One or more match filters are all zeros via calloc. -----------------------
  // ---------------------------------------------------------------------------
  return num_ref_filters;
}

void add_filter_to_dictionary(const WienerNonsepInfo *filter, int class_id,
                              const WienernsFilterParameters *nsfilter_params,
                              int16_t *frame_filter_dictionary, int dict_stride,
                              int nopcw) {
  (void)nopcw;
  assert(frame_filter_dictionary != NULL);
  assert(dict_stride > 0);
  if (class_id == filter->num_classes - 1) return;
  const int filter_index = prev_filters_begin(filter->num_classes) + class_id;
  assert(filter_index < num_dictionary_slots(filter->num_classes, nopcw));
  int16_t *match_filter = frame_filter_dictionary + filter_index * dict_stride;
  const int16_t *wienerns_filter = const_nsfilter_taps(filter, class_id);
  const int num_feat = nsfilter_params->ncoeffs;
  for (int i = 0; i < num_feat; ++i) {
    match_filter[i] = wienerns_filter[i];
  }
}

#endif  // CONFIG_COMBINE_PC_NS_WIENER

void av1_alloc_txk_skip_array(CommonModeInfoParams *mi_params, AV1_COMMON *cm) {
  // Allocate based on the MIN_TX_SIZE, which is a 4x4 block.
  (void)cm;
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    int w = mi_params->mi_cols << MI_SIZE_LOG2;
    int h = mi_params->mi_rows << MI_SIZE_LOG2;
    w = ((w + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    h = ((h + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    int stride = (w + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    int rows = (h + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    mi_params->tx_skip[plane] = aom_calloc(rows * stride, sizeof(uint8_t));
    mi_params->tx_skip_buf_size[plane] = rows * stride;
    mi_params->tx_skip_stride[plane] = stride;
  }
#ifndef NDEBUG
  av1_reset_txk_skip_array(cm);
#endif  // NDEBUG
}

void av1_set_txk_skip_array_stride(CommonModeInfoParams *mi_params,
                                   AV1_COMMON *cm) {
  (void)cm;
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    int w = mi_params->mi_cols << MI_SIZE_LOG2;
    int h = mi_params->mi_rows << MI_SIZE_LOG2;
    w = ((w + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    h = ((h + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    int stride = (w + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    int rows = (h + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    if (rows * stride > (int)mi_params->tx_skip_buf_size[plane]) {
      aom_free(mi_params->tx_skip[plane]);
      mi_params->tx_skip[plane] = aom_calloc(rows * stride, sizeof(uint8_t));
      mi_params->tx_skip_buf_size[plane] = rows * stride;
    }
    mi_params->tx_skip_stride[plane] = stride;
  }
}

void av1_dealloc_txk_skip_array(CommonModeInfoParams *mi_params) {
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    aom_free(mi_params->tx_skip[plane]);
    mi_params->tx_skip[plane] = NULL;
  }
}

void av1_reset_txk_skip_array(AV1_COMMON *cm) {
  // Allocate based on the MIN_TX_SIZE, which is a 4x4 block.
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    int h = cm->mi_params.mi_rows << MI_SIZE_LOG2;
    h = ((h + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    h >>= ((plane == 0) ? 0 : cm->seq_params.subsampling_y);
    int stride = cm->mi_params.tx_skip_stride[plane];
    int rows = (h + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    memset(cm->mi_params.tx_skip[plane], ILLEGAL_TXK_SKIP_VALUE, rows * stride);
  }
}

void av1_reset_txk_skip_array_using_mi_params(CommonModeInfoParams *mi_params) {
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    memset(mi_params->tx_skip[plane], ILLEGAL_TXK_SKIP_VALUE,
           mi_params->tx_skip_buf_size[plane]);
  }
}

void av1_init_txk_skip_array(const AV1_COMMON *cm, int mi_row, int mi_col,
                             BLOCK_SIZE bsize, uint8_t value,
                             TREE_TYPE tree_type,
                             const CHROMA_REF_INFO *chroma_ref_info,
                             int plane_start, int plane_end) {
  const bool is_chroma_ref = chroma_ref_info->is_chroma_ref;
  for (int plane = plane_start; plane < plane_end; plane++) {
    if (plane && !is_chroma_ref) {
      break;
    }
    const int plane_mi_row =
        plane ? chroma_ref_info->mi_row_chroma_base : mi_row;
    const int plane_mi_col =
        plane ? chroma_ref_info->mi_col_chroma_base : mi_col;
    const BLOCK_SIZE bsize_base = (tree_type == SHARED_PART && plane)
                                      ? chroma_ref_info->bsize_base
                                      : bsize;
    int stride = cm->mi_params.tx_skip_stride[plane];
    int x = (plane_mi_col << MI_SIZE_LOG2) >>
            ((plane == 0) ? 0 : cm->seq_params.subsampling_x);
    int y = (plane_mi_row << MI_SIZE_LOG2) >>
            ((plane == 0) ? 0 : cm->seq_params.subsampling_y);
    int row = y >> MIN_TX_SIZE_LOG2;
    int col = x >> MIN_TX_SIZE_LOG2;
    int blk_w = block_size_wide[bsize_base] >>
                ((plane == 0) ? 0 : cm->seq_params.subsampling_x);
    int blk_h = block_size_high[bsize_base] >>
                ((plane == 0) ? 0 : cm->seq_params.subsampling_y);
    blk_w >>= MIN_TX_SIZE_LOG2;
    blk_h >>= MIN_TX_SIZE_LOG2;

    if (plane && (blk_w == 0 || blk_h == 0) && is_chroma_ref) {
      blk_w = blk_w == 0 ? 1 : blk_w;
      blk_h = blk_h == 0 ? 1 : blk_h;
    }

    for (int r = 0; r < blk_h; r++) {
      for (int c = 0; c < blk_w; c++) {
        uint32_t idx = (row + r) * stride + col + c;
        assert(idx < cm->mi_params.tx_skip_buf_size[plane]);
        cm->mi_params.tx_skip[plane][idx] = value;
      }
    }
  }
}

void av1_update_txk_skip_array(const AV1_COMMON *cm, int mi_row, int mi_col,
                               TREE_TYPE tree_type,
                               const CHROMA_REF_INFO *chroma_ref_info,
                               int plane, int blk_row, int blk_col,
                               TX_SIZE tx_size) {
  blk_row *= 4;
  blk_col *= 4;
  mi_row = (tree_type == SHARED_PART && plane)
               ? chroma_ref_info->mi_row_chroma_base
               : mi_row;
  mi_col = (tree_type == SHARED_PART && plane)
               ? chroma_ref_info->mi_col_chroma_base
               : mi_col;
  int stride = cm->mi_params.tx_skip_stride[plane];
  int tx_w = tx_size_wide[tx_size];
  int tx_h = tx_size_high[tx_size];
  int cols = tx_w >> MIN_TX_SIZE_LOG2;
  int rows = tx_h >> MIN_TX_SIZE_LOG2;
  int x = (mi_col << MI_SIZE_LOG2) >>
          ((plane == 0) ? 0 : cm->seq_params.subsampling_x);
  int y = (mi_row << MI_SIZE_LOG2) >>
          ((plane == 0) ? 0 : cm->seq_params.subsampling_y);
  x = (x + blk_col) >> MIN_TX_SIZE_LOG2;
  y = (y + blk_row) >> MIN_TX_SIZE_LOG2;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      uint32_t idx = (y + r) * stride + x + c;
      assert(idx < cm->mi_params.tx_skip_buf_size[plane]);
      cm->mi_params.tx_skip[plane][idx] = 1;
    }
  }
}

uint8_t av1_get_txk_skip(const AV1_COMMON *cm, int mi_row, int mi_col,
                         int plane, int blk_row, int blk_col) {
  blk_row *= 4;
  blk_col *= 4;
  int stride = cm->mi_params.tx_skip_stride[plane];
  int x = (mi_col << MI_SIZE_LOG2) >>
          ((plane == 0) ? 0 : cm->seq_params.subsampling_x);
  int y = (mi_row << MI_SIZE_LOG2) >>
          ((plane == 0) ? 0 : cm->seq_params.subsampling_y);
  x = (x + blk_col) >> MIN_TX_SIZE_LOG2;
  y = (y + blk_row) >> MIN_TX_SIZE_LOG2;
  uint32_t idx = y * stride + x;
  assert(idx < cm->mi_params.tx_skip_buf_size[plane]);
  assert(cm->mi_params.tx_skip[plane][idx] != ILLEGAL_TXK_SKIP_VALUE);
  return cm->mi_params.tx_skip[plane][idx];
}

void av1_alloc_class_id_array(CommonModeInfoParams *mi_params, AV1_COMMON *cm) {
  (void)cm;
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    // Allocate for the maximum possible value of cm->superres_upscaled_width,
    // which is the coded frame width * 2, instead of just
    // cm->superres_upscaled_width
    int w = (mi_params->mi_cols << MI_SIZE_LOG2) * 2;
    int h = cm->superres_upscaled_height;
    w = ((w + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    h = ((h + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    int stride = (w + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    int rows = (h + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    mi_params->wiener_class_id[plane] =
        aom_calloc(rows * stride, sizeof(uint8_t));
    mi_params->wiener_class_id_buf_size[plane] = rows * stride;
    mi_params->wiener_class_id_stride[plane] = stride;
  }
}

void av1_set_class_id_array_stride(CommonModeInfoParams *mi_params,
                                   AV1_COMMON *cm) {
  (void)cm;
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    // Allocate for the maximum possible value of cm->superres_upscaled_width,
    // which is the coded frame width * 2, instead of just
    // cm->superres_upscaled_width
    int w = (mi_params->mi_cols << MI_SIZE_LOG2) * 2;
    int h = cm->superres_upscaled_height;
    w = ((w + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    h = ((h + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2) << MAX_SB_SIZE_LOG2;
    int stride = (w + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    int rows = (h + MIN_TX_SIZE - 1) >> MIN_TX_SIZE_LOG2;
    if (rows * stride > (int)mi_params->wiener_class_id_buf_size[plane]) {
      aom_free(mi_params->wiener_class_id[plane]);
      mi_params->wiener_class_id[plane] =
          aom_calloc(rows * stride, sizeof(uint8_t));
      mi_params->wiener_class_id_buf_size[plane] = rows * stride;
    }
    mi_params->wiener_class_id_stride[plane] = stride;
  }
}

void av1_dealloc_class_id_array(CommonModeInfoParams *mi_params) {
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    aom_free(mi_params->wiener_class_id[plane]);
    mi_params->wiener_class_id[plane] = NULL;
  }
}
