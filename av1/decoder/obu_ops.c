#include <assert.h>

#include "config/aom_config.h"
#include "aom_dsp/bitreader_buffer.h"
#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/obu.h"

void read_mlayer_info(int obuXLId, int opsID, int opIndex, int xLId,
                      struct OPSMLayerInfo *ops_mlayer_info,
                      struct aom_read_bit_buffer *rb) {
  ops_mlayer_info->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] =
      aom_rb_read_literal(rb, 8);

  ops_mlayer_info->OPMLayerCount[obuXLId][opsID][opIndex][xLId] = 0;
  int mCount = 0;
  for (int j = 0; j < 8; j++) {
    if ((ops_mlayer_info->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] &
         (1 << j))) {
      ops_mlayer_info->OPMLayerCount[obuXLId][xLId][mCount][xLId] = j;
      /* map of temporal embedded layers in this OP */
      ops_mlayer_info->ops_tlayer_map[obuXLId][opsID][opIndex][xLId][j] =
          aom_rb_read_literal(rb, 8);
      int tCount = 0;
      for (int k = 0; k < 8; k++) {
        if ((ops_mlayer_info->ops_tlayer_map[obuXLId][opsID][opIndex][xLId][j] &
             (1 << k))) {
          ops_mlayer_info->OpsTlayerID[obuXLId][opsID][opIndex][xLId][tCount] =
              k;
          tCount++;
        }
      }
      ops_mlayer_info->OPTLayerCount[obuXLId][opsID][opIndex][xLId][j] = tCount;
      mCount++;
    }
  }
  ops_mlayer_info->OPMLayerCount[obuXLId][opsID][opIndex][xLId] = mCount;
}

// TODO: (@hegilmez) to be specified by CWG-F270 (needs alignment)
void read_ops_color_info(int obu_xlayer_id, int ops_id, int ops_idx) {
  (void)obu_xlayer_id;
  (void)ops_id;
  (void)ops_idx;
#if !CONFIG_MULTILAYER_HLS_REMOVE_LOGS
  printf("read_ops_read_color_info(): not defined yet.\n");
#endif  // !CONFIG_MULTILAYER_HLS_REMOVE_LOGS
}

uint32_t read_operating_point_set_obsp(struct AV1Decoder *pbi,
                                       int obu_xlayer_id,
                                       struct aom_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;

  int ops_reset_flag = aom_rb_read_bit(rb);
  int ops_id = aom_rb_read_literal(rb, 4);

  struct OperatingPointSet *ops_params = NULL;
  int ops_pos = -1;
  for (int i = 0; i < pbi->dec_ops_counter; i++) {
    if (pbi->ops_list[i].ops_id[obu_xlayer_id] == ops_id) {
      ops_pos = i;
      break;
    }
  }  // i
  if (ops_pos != -1)
    ops_params = &pbi->ops_list[ops_pos];
  else
    ops_params = &pbi->ops_list[pbi->dec_ops_counter];
  pbi->dec_ops_counter++;

  ObuExtension obu_ext = ops_params->ops_extension;

  ops_params->ops_reset_flag[obu_xlayer_id] = ops_reset_flag;
  ops_params->ops_id[obu_xlayer_id] = ops_id;
  ops_params->ops_cnt[obu_xlayer_id][ops_id] = aom_rb_read_literal(rb, 3);

  if (ops_params->ops_cnt[obu_xlayer_id][ops_id] > 0) {
    ops_params->ops_priority[obu_xlayer_id][ops_id] =
        aom_rb_read_literal(rb, 4);
    ops_params->ops_intent[obu_xlayer_id][ops_id] = aom_rb_read_literal(rb, 4);
    ops_params->ops_intent_present_flag[obu_xlayer_id][ops_id] =
        aom_rb_read_bit(rb);
    ops_params->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id] =
        aom_rb_read_bit(rb);
    ops_params->ops_color_info_present_flag[obu_xlayer_id][ops_id] =
        aom_rb_read_bit(rb);

    if (obu_xlayer_id == 31) {
      ops_params->ops_mlayer_info_present_flag[obu_xlayer_id][ops_id] =
          aom_rb_read_bit(rb);
      ops_params->ops_reserved_2bits[obu_xlayer_id][ops_id] =
          aom_rb_read_literal(rb, 2);
    } else {
      ops_params->ops_mlayer_info_present_flag[obu_xlayer_id][ops_id] = 1;
      ops_params->ops_reserved_3bits[obu_xlayer_id][ops_id] =
          aom_rb_read_literal(rb, 3);
    }
    for (int i = 0; i < ops_params->ops_cnt[obu_xlayer_id][ops_id]; i++) {
      ops_params->ops_data_size[obu_xlayer_id][ops_id][i] = read_data_size(rb);
      if (ops_params->ops_intent_present_flag[obu_xlayer_id][ops_id])
        ops_params->ops_intent_op[obu_xlayer_id][ops_id][i] =
            aom_rb_read_literal(rb, 4);

      if (ops_params->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]) {
        ops_params->ops_operational_profile_id[obu_xlayer_id][ops_id][i] =
            aom_rb_read_literal(rb, 6);
        ops_params->ops_operational_level_id[obu_xlayer_id][ops_id][i] =
            aom_rb_read_literal(rb, 5);
        ops_params->ops_operational_tier_id[obu_xlayer_id][ops_id][i] =
            aom_rb_read_bit(rb);
      }

      if (ops_params->ops_color_info_present_flag[obu_xlayer_id][ops_id])
        read_ops_color_info(obu_xlayer_id, ops_id, i);

      if (obu_xlayer_id == 31) {
        ops_params->ops_xlayer_map[obu_xlayer_id][ops_id][i] =
            aom_rb_read_literal(rb, 32);
        int k = 0;
        for (int j = 0; j < 31; j++) {
          if ((ops_params->ops_xlayer_map[obu_xlayer_id][ops_id][i] & (1 << j)))
            ops_params->OpsxLayerId[obu_xlayer_id][ops_id][i][k] = j;
          if (ops_params->ops_mlayer_info_present_flag[obu_xlayer_id][ops_id])
            read_mlayer_info(obu_xlayer_id, ops_id, i, j,
                             ops_params->ops_mlayer_info, rb);
        }
        ops_params->XCount[obu_xlayer_id][ops_id][i] = k;
      } else {
        ops_params->XCount[obu_xlayer_id][ops_id][i] = 1;
        ops_params->OpsxLayerId[obu_xlayer_id][ops_id][i][0] = obu_xlayer_id;
        if (ops_params->ops_mlayer_info_present_flag[obu_xlayer_id][ops_id])
          read_mlayer_info(obu_xlayer_id, ops_id, i, obu_xlayer_id,
                           ops_params->ops_mlayer_info, rb);
      }
      ops_params->ops_padding_bits = aom_rb_read_literal(rb, 3);
      obu_ext.extension_data =
          aom_rb_read_literal(rb, 1);  // TODO: (@hegilmez) - le(n)
    }
  }
  obu_ext.extension_present_flag = aom_rb_read_bit(rb);
  if (obu_ext.extension_present_flag) read_obu_extension_bits(&obu_ext, rb);

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
