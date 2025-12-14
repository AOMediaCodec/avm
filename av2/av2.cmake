#
# Copyright (c) 2021, Alliance for Open Media. All rights reserved
#
# This source code is subject to the terms of the BSD 3-Clause Clear License and
# the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
# License was not distributed with this source code in the LICENSE file, you can
# obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance
# for Open Media Patent License 1.0 was not distributed with this source code in
# the PATENTS file, you can obtain it at aomedia.org/license/patent-license/.
#
if(AVM_AV2_AV2_CMAKE_)
  return()
endif() # AVM_AV2_AV2_CMAKE_
set(AVM_AV2_AV2_CMAKE_ 1)

list(
  APPEND
  AVM_AV2_COMMON_SOURCES
  "${AVM_ROOT}/common/args_helper.h"
  "${AVM_ROOT}/common/args_helper.c"
  "${AVM_ROOT}/av2/arg_defs.h"
  "${AVM_ROOT}/av2/arg_defs.c"
  "${AVM_ROOT}/av2/av2_iface_common.h"
  "${AVM_ROOT}/av2/common/alloccommon.c"
  "${AVM_ROOT}/av2/common/alloccommon.h"
  "${AVM_ROOT}/av2/common/av2_common_int.h"
  "${AVM_ROOT}/av2/common/av2_inv_txfm2d.c"
  "${AVM_ROOT}/av2/common/av2_loopfilter.c"
  "${AVM_ROOT}/av2/common/av2_loopfilter.h"
  "${AVM_ROOT}/av2/common/av2_txfm.c"
  "${AVM_ROOT}/av2/common/av2_txfm.h"
  "${AVM_ROOT}/av2/common/blockd.c"
  "${AVM_ROOT}/av2/common/blockd.h"
  "${AVM_ROOT}/av2/common/cdef.c"
  "${AVM_ROOT}/av2/common/cdef.h"
  "${AVM_ROOT}/av2/common/gdf.c"
  "${AVM_ROOT}/av2/common/gdf.h"
  "${AVM_ROOT}/av2/common/gdf_block.c"
  "${AVM_ROOT}/av2/common/gdf_block.h"
  "${AVM_ROOT}/av2/common/cdef_block.c"
  "${AVM_ROOT}/av2/common/cdef_block.h"
  "${AVM_ROOT}/av2/common/cfl.c"
  "${AVM_ROOT}/av2/common/cfl.h"
  "${AVM_ROOT}/av2/common/common.h"
  "${AVM_ROOT}/av2/common/common_data.h"
  "${AVM_ROOT}/av2/common/convolve.c"
  "${AVM_ROOT}/av2/common/convolve.h"
  "${AVM_ROOT}/av2/common/entropy.c"
  "${AVM_ROOT}/av2/common/entropy.h"
  "${AVM_ROOT}/av2/common/entropymode.c"
  "${AVM_ROOT}/av2/common/entropymode.h"
  "${AVM_ROOT}/av2/common/entropymv.c"
  "${AVM_ROOT}/av2/common/entropymv.h"
  "${AVM_ROOT}/av2/common/enums.h"
  "${AVM_ROOT}/av2/common/filter.h"
  "${AVM_ROOT}/av2/common/frame_buffers.c"
  "${AVM_ROOT}/av2/common/frame_buffers.h"
  "${AVM_ROOT}/av2/common/idct.c"
  "${AVM_ROOT}/av2/common/idct.h"
  "${AVM_ROOT}/av2/common/level.c"
  "${AVM_ROOT}/av2/common/level.h"
  "${AVM_ROOT}/av2/common/mv.h"
  "${AVM_ROOT}/av2/common/mvref_common.c"
  "${AVM_ROOT}/av2/common/mvref_common.h"
  "${AVM_ROOT}/av2/common/obu_util.c"
  "${AVM_ROOT}/av2/common/obu_util.h"
  "${AVM_ROOT}/av2/common/odintrin.c"
  "${AVM_ROOT}/av2/common/odintrin.h"
  "${AVM_ROOT}/av2/common/predefined_qm.c"
  "${AVM_ROOT}/av2/common/pred_common.c"
  "${AVM_ROOT}/av2/common/pred_common.h"
  "${AVM_ROOT}/av2/common/quant_common.c"
  "${AVM_ROOT}/av2/common/quant_common.h"
  "${AVM_ROOT}/av2/common/reconinter.c"
  "${AVM_ROOT}/av2/common/reconinter.h"
  "${AVM_ROOT}/av2/common/reconintra.c"
  "${AVM_ROOT}/av2/common/reconintra.h"
  "${AVM_ROOT}/av2/common/resize.c"
  "${AVM_ROOT}/av2/common/resize.h"
  "${AVM_ROOT}/av2/common/restoration.c"
  "${AVM_ROOT}/av2/common/restoration.h"
  "${AVM_ROOT}/av2/common/scale.c"
  "${AVM_ROOT}/av2/common/scale.h"
  "${AVM_ROOT}/av2/common/scan.c"
  "${AVM_ROOT}/av2/common/scan.h"
  "${AVM_ROOT}/av2/common/secondary_tx.h"
  "${AVM_ROOT}/av2/common/seg_common.c"
  "${AVM_ROOT}/av2/common/seg_common.h"
  "${AVM_ROOT}/av2/common/thread_common.c"
  "${AVM_ROOT}/av2/common/thread_common.h"
  "${AVM_ROOT}/av2/common/tile_common.c"
  "${AVM_ROOT}/av2/common/tile_common.h"
  "${AVM_ROOT}/av2/common/timing.c"
  "${AVM_ROOT}/av2/common/timing.h"
  "${AVM_ROOT}/av2/common/tip.c"
  "${AVM_ROOT}/av2/common/tip.h"
  "${AVM_ROOT}/av2/common/txb_common.c"
  "${AVM_ROOT}/av2/common/txb_common.h"
  "${AVM_ROOT}/av2/common/warped_motion.c"
  "${AVM_ROOT}/av2/common/warped_motion.h"
  "${AVM_ROOT}/av2/common/hr_coding.h"
  "${AVM_ROOT}/av2/common/hr_coding.c"
  "${AVM_ROOT}/av2/common/cost.c"
  "${AVM_ROOT}/av2/common/cost.h"
  "${AVM_ROOT}/av2/common/entropy_inits_coeffs.h"
  "${AVM_ROOT}/av2/common/entropy_inits_modes.h"
  "${AVM_ROOT}/av2/common/entropy_inits_mv.h"
  "${AVM_ROOT}/av2/common/entropy_sideinfo.h")

list(APPEND AVM_AV2_COMMON_SOURCES "${AVM_ROOT}/av2/common/intra_matrix.c"
     "${AVM_ROOT}/av2/common/intra_matrix.h"
     "${AVM_ROOT}/av2/common/intra_dip.cc" "${AVM_ROOT}/av2/common/intra_dip.h")
list(APPEND AVM_AV2_COMMON_INTRIN_AVX2
     "${AVM_ROOT}/av2/common/x86/intra_matrix_avx2.c")

if(CONFIG_AV2_ENCODER)
  list(APPEND AVM_AV2_COMMON_SOURCES "${AVM_ROOT}/av2/encoder/erp_ml.c"
       "${AVM_ROOT}/av2/encoder/erp_ml.h")

  if(CONFIG_TENSORFLOW_LITE)
    list(APPEND AVM_AV2_COMMON_SOURCES
         "${AVM_ROOT}/av2/tflite_models/op_registrations.cc"
         "${AVM_ROOT}/av2/tflite_models/op_registrations.h")
  endif()
endif()

list(APPEND AVM_AV2_COMMON_SOURCES "${AVM_ROOT}/av2/common/ccso.c"
     "${AVM_ROOT}/av2/common/ccso.h")
list(APPEND AVM_AV2_ENCODER_SOURCES "${AVM_ROOT}/av2/encoder/pickccso.c"
     "${AVM_ROOT}/av2/encoder/pickccso.h")
list(APPEND AVM_AV2_COMMON_SOURCES "${AVM_ROOT}/av2/common/bru.c"
     "${AVM_ROOT}/av2/common/bru.h")

list(APPEND AVM_AV2_COMMON_SOURCES "${AVM_ROOT}/av2/common/banding_metadata.c"
     "${AVM_ROOT}/av2/common/banding_metadata.h")

list(APPEND AVM_AV2_ENCODER_SOURCES "${AVM_ROOT}/av2/encoder/trellis_quant.c"
     "${AVM_ROOT}/av2/encoder/trellis_quant.h")
list(APPEND AVM_AV2_ENCODER_INTRIN_AVX2
     "${AVM_ROOT}/av2/encoder/x86/trellis_quant_avx2.c")

list(
  APPEND
  AVM_AV2_DECODER_SOURCES
  "${AVM_ROOT}/av2/av2_dx_iface.c"
  "${AVM_ROOT}/av2/decoder/decodeframe.c"
  "${AVM_ROOT}/av2/decoder/decodeframe.h"
  "${AVM_ROOT}/av2/decoder/decodemv.c"
  "${AVM_ROOT}/av2/decoder/decodemv.h"
  "${AVM_ROOT}/av2/decoder/decoder.c"
  "${AVM_ROOT}/av2/decoder/decoder.h"
  "${AVM_ROOT}/av2/decoder/decodetxb.c"
  "${AVM_ROOT}/av2/decoder/decodetxb.h"
  "${AVM_ROOT}/av2/decoder/detokenize.c"
  "${AVM_ROOT}/av2/decoder/detokenize.h"
  "${AVM_ROOT}/av2/decoder/dthread.h"
  "${AVM_ROOT}/av2/decoder/obu.h"
  "${AVM_ROOT}/av2/decoder/obu_qm.c"
  "${AVM_ROOT}/av2/decoder/obu_fgm.c"
  "${AVM_ROOT}/av2/decoder/obu.c"
  "${AVM_ROOT}/av2/decoder/obu_ci.c")

list(APPEND AVM_AV2_DECODER_SOURCES "${AVM_ROOT}/av2/decoder/obu_atlas.c"
     "${AVM_ROOT}/av2/decoder/obu_lcr.c" "${AVM_ROOT}/av2/decoder/obu_ops.c")

list(APPEND AVM_AV2_DECODER_SOURCES "${AVM_ROOT}/av2/decoder/obu_buf.c")

list(
  APPEND
  AVM_AV2_ENCODER_SOURCES
  "${AVM_ROOT}/av2/av2_cx_iface.c"
  "${AVM_ROOT}/av2/encoder/aq_complexity.c"
  "${AVM_ROOT}/av2/encoder/aq_complexity.h"
  "${AVM_ROOT}/av2/encoder/aq_cyclicrefresh.c"
  "${AVM_ROOT}/av2/encoder/aq_cyclicrefresh.h"
  "${AVM_ROOT}/av2/encoder/aq_variance.c"
  "${AVM_ROOT}/av2/encoder/aq_variance.h"
  "${AVM_ROOT}/av2/encoder/enc_enums.h"
  "${AVM_ROOT}/av2/encoder/av2_fwd_txfm2d.c"
  "${AVM_ROOT}/av2/encoder/av2_quantize.c"
  "${AVM_ROOT}/av2/encoder/av2_quantize.h"
  "${AVM_ROOT}/av2/encoder/bitstream.c"
  "${AVM_ROOT}/av2/encoder/bitstream_qm.c"
  "${AVM_ROOT}/av2/encoder/bitstream_fgm.c"
  "${AVM_ROOT}/av2/encoder/bitstream_ci.c"
  "${AVM_ROOT}/av2/encoder/bitstream.h"
  "${AVM_ROOT}/av2/encoder/block.h"
  "${AVM_ROOT}/av2/encoder/cnn.c"
  "${AVM_ROOT}/av2/encoder/cnn.h"
  "${AVM_ROOT}/av2/encoder/compound_type.c"
  "${AVM_ROOT}/av2/encoder/compound_type.h"
  "${AVM_ROOT}/av2/encoder/context_tree.c"
  "${AVM_ROOT}/av2/encoder/context_tree.h"
  "${AVM_ROOT}/av2/encoder/encodeframe.c"
  "${AVM_ROOT}/av2/encoder/encodeframe.h"
  "${AVM_ROOT}/av2/encoder/encodeframe_utils.c"
  "${AVM_ROOT}/av2/encoder/encodeframe_utils.h"
  "${AVM_ROOT}/av2/encoder/encodemb.c"
  "${AVM_ROOT}/av2/encoder/encodemb.h"
  "${AVM_ROOT}/av2/encoder/encodemv.c"
  "${AVM_ROOT}/av2/encoder/encodemv.h"
  "${AVM_ROOT}/av2/encoder/encode_strategy.c"
  "${AVM_ROOT}/av2/encoder/encode_strategy.h"
  "${AVM_ROOT}/av2/encoder/encoder.c"
  "${AVM_ROOT}/av2/encoder/encoder.h"
  "${AVM_ROOT}/av2/encoder/encoder_alloc.h"
  "${AVM_ROOT}/av2/encoder/encoder_utils.c"
  "${AVM_ROOT}/av2/encoder/encoder_utils.h"
  "${AVM_ROOT}/av2/encoder/encodetxb.c"
  "${AVM_ROOT}/av2/encoder/encodetxb.h"
  "${AVM_ROOT}/av2/encoder/ethread.c"
  "${AVM_ROOT}/av2/encoder/ethread.h"
  "${AVM_ROOT}/av2/encoder/extend.c"
  "${AVM_ROOT}/av2/encoder/extend.h"
  "${AVM_ROOT}/av2/encoder/firstpass.c"
  "${AVM_ROOT}/av2/encoder/firstpass.h"
  "${AVM_ROOT}/av2/encoder/global_motion.c"
  "${AVM_ROOT}/av2/encoder/global_motion.h"
  "${AVM_ROOT}/av2/encoder/global_motion_facade.c"
  "${AVM_ROOT}/av2/encoder/global_motion_facade.h"
  "${AVM_ROOT}/av2/encoder/gop_structure.c"
  "${AVM_ROOT}/av2/encoder/gop_structure.h"
  "${AVM_ROOT}/av2/encoder/grain_test_vectors.h"
  "${AVM_ROOT}/av2/encoder/hash.c"
  "${AVM_ROOT}/av2/encoder/hash.h"
  "${AVM_ROOT}/av2/encoder/hash_motion.c"
  "${AVM_ROOT}/av2/encoder/hash_motion.h"
  "${AVM_ROOT}/av2/encoder/hybrid_fwd_txfm.c"
  "${AVM_ROOT}/av2/encoder/hybrid_fwd_txfm.h"
  "${AVM_ROOT}/av2/encoder/interp_search.c"
  "${AVM_ROOT}/av2/encoder/interp_search.h"
  "${AVM_ROOT}/av2/encoder/lookahead.c"
  "${AVM_ROOT}/av2/encoder/lookahead.h"
  "${AVM_ROOT}/av2/encoder/mcomp.c"
  "${AVM_ROOT}/av2/encoder/mcomp.h"
  "${AVM_ROOT}/av2/encoder/ml.c"
  "${AVM_ROOT}/av2/encoder/ml.h"
  "${AVM_ROOT}/av2/encoder/model_rd.h"
  "${AVM_ROOT}/av2/encoder/motion_search_facade.c"
  "${AVM_ROOT}/av2/encoder/motion_search_facade.h"
  "${AVM_ROOT}/av2/encoder/mv_prec.c"
  "${AVM_ROOT}/av2/encoder/mv_prec.h"
  "${AVM_ROOT}/av2/encoder/palette.c"
  "${AVM_ROOT}/av2/encoder/palette.h"
  "${AVM_ROOT}/av2/encoder/partition_search.h"
  "${AVM_ROOT}/av2/encoder/partition_search.c"
  "${AVM_ROOT}/av2/encoder/partition_strategy.h"
  "${AVM_ROOT}/av2/encoder/partition_strategy.c"
  "${AVM_ROOT}/av2/encoder/pass2_strategy.h"
  "${AVM_ROOT}/av2/encoder/pass2_strategy.c"
  "${AVM_ROOT}/av2/encoder/pickcdef.c"
  "${AVM_ROOT}/av2/encoder/pickcdef.h"
  "${AVM_ROOT}/av2/encoder/picklpf.c"
  "${AVM_ROOT}/av2/encoder/picklpf.h"
  "${AVM_ROOT}/av2/encoder/pickrst.c"
  "${AVM_ROOT}/av2/encoder/pickrst.h"
  "${AVM_ROOT}/av2/encoder/ratectrl.c"
  "${AVM_ROOT}/av2/encoder/ratectrl.h"
  "${AVM_ROOT}/av2/encoder/rc_utils.h"
  "${AVM_ROOT}/av2/encoder/rd.c"
  "${AVM_ROOT}/av2/encoder/rd.h"
  "${AVM_ROOT}/av2/encoder/rdopt.c"
  "${AVM_ROOT}/av2/encoder/rdopt.h"
  "${AVM_ROOT}/av2/encoder/rdopt_utils.h"
  "${AVM_ROOT}/av2/encoder/reconinter_enc.c"
  "${AVM_ROOT}/av2/encoder/reconinter_enc.h"
  "${AVM_ROOT}/av2/encoder/scale.c"
  "${AVM_ROOT}/av2/encoder/scale.h"
  "${AVM_ROOT}/av2/encoder/segmentation.c"
  "${AVM_ROOT}/av2/encoder/segmentation.h"
  "${AVM_ROOT}/av2/encoder/speed_features.c"
  "${AVM_ROOT}/av2/encoder/speed_features.h"
  "${AVM_ROOT}/av2/encoder/subgop.c"
  "${AVM_ROOT}/av2/encoder/subgop.h"
  "${AVM_ROOT}/av2/encoder/temporal_filter.c"
  "${AVM_ROOT}/av2/encoder/temporal_filter.h"
  "${AVM_ROOT}/av2/encoder/tokenize.c"
  "${AVM_ROOT}/av2/encoder/tokenize.h"
  "${AVM_ROOT}/av2/encoder/tpl_model.c"
  "${AVM_ROOT}/av2/encoder/tpl_model.h"
  "${AVM_ROOT}/av2/encoder/tx_search.c"
  "${AVM_ROOT}/av2/encoder/tx_search.h"
  "${AVM_ROOT}/av2/encoder/intra_mode_search.c"
  "${AVM_ROOT}/av2/encoder/intra_mode_search.h"
  "${AVM_ROOT}/av2/encoder/intra_mode_search_utils.h"
  "${AVM_ROOT}/av2/encoder/wedge_utils.c"
  "${AVM_ROOT}/av2/encoder/av2_noise_estimate.c"
  "${AVM_ROOT}/av2/encoder/av2_noise_estimate.h"
  "${AVM_ROOT}/third_party/fastfeat/fast.c"
  "${AVM_ROOT}/third_party/fastfeat/fast.h"
  "${AVM_ROOT}/third_party/fastfeat/fast_9.c"
  "${AVM_ROOT}/third_party/fastfeat/nonmax.c"
  "${AVM_ROOT}/third_party/vector/vector.c"
  "${AVM_ROOT}/third_party/vector/vector.h"
  "${AVM_ROOT}/av2/encoder/dwt.c"
  "${AVM_ROOT}/av2/encoder/dwt.h"
  "${AVM_ROOT}/common/md5_utils.c"
  "${AVM_ROOT}/common/md5_utils.h"
  "${AVM_ROOT}/common/rawenc.c"
  "${AVM_ROOT}/common/rawenc.h")

if(CONFIG_TUNE_VMAF)
  list(APPEND AVM_AV2_ENCODER_SOURCES "${AVM_ROOT}/av2/encoder/tune_vmaf.c"
       "${AVM_ROOT}/av2/encoder/tune_vmaf.h")
endif()

if(CONFIG_ML_PART_SPLIT)
  list(
    APPEND
    AVM_AV2_ENCODER_SOURCES
    "${AVM_ROOT}/av2/encoder/partition_ml.c"
    "${AVM_ROOT}/av2/encoder/partition_ml.h"
    "${AVM_ROOT}/av2/encoder/part_split_prune_tflite.cc"
    "${AVM_ROOT}/av2/encoder/part_split_prune_tflite.h"
    "${AVM_ROOT}/av2/encoder/simple_intrapred_tflite_model_128x128.h"
    "${AVM_ROOT}/av2/encoder/simple_intrapred_tflite_model_64x64.h"
    "${AVM_ROOT}/av2/encoder/simple_intrapred_tflite_model_32x32.h"
    "${AVM_ROOT}/av2/encoder/simple_intrapred_tflite_model_16x16.h"
    "${AVM_ROOT}/av2/encoder/sms_part_split_prune_tflite_model.h")
endif()

if(CONFIG_DIP_EXT_PRUNING)
  list(
    APPEND
    AVM_AV2_ENCODER_SOURCES
    "${AVM_ROOT}/av2/encoder/intra_dip_mode_prune_tflite.h"
    "${AVM_ROOT}/av2/encoder/intra_dip_mode_prune_tflite.cc"
    "${AVM_ROOT}/av2/encoder/intra_dip_mode_prune_weights.cc")
endif()

list(APPEND AVM_AV2_ENCODER_SOURCES "${AVM_ROOT}/av2/encoder/bitstream_atlas.c"
     "${AVM_ROOT}/av2/encoder/bitstream_lcr.c"
     "${AVM_ROOT}/av2/encoder/bitstream_ops.c")

list(APPEND AVM_AV2_ENCODER_SOURCES "${AVM_ROOT}/av2/encoder/bitstream_buf.c")

list(APPEND AVM_AV2_COMMON_INTRIN_SSE2
     "${AVM_ROOT}/av2/common/cdef_block_sse2.c"
     "${AVM_ROOT}/av2/common/x86/cfl_sse2.c")

list(
  APPEND
  AVM_AV2_COMMON_INTRIN_SSSE3
  "${AVM_ROOT}/av2/common/cdef_block_ssse3.c"
  "${AVM_ROOT}/av2/common/x86/cfl_ssse3.c"
  "${AVM_ROOT}/av2/common/x86/highbd_convolve_2d_ssse3.c"
  "${AVM_ROOT}/av2/common/x86/highbd_wiener_convolve_ssse3.c"
  "${AVM_ROOT}/av2/common/x86/reconinter_ssse3.c")

list(
  APPEND
  AVM_AV2_COMMON_INTRIN_SSE4_1
  "${AVM_ROOT}/av2/common/cdef_block_sse4.c"
  "${AVM_ROOT}/av2/common/x86/av2_convolve_horiz_rs_sse4.c"
  "${AVM_ROOT}/av2/common/x86/av2_convolve_scale_sse4.c"
  "${AVM_ROOT}/av2/common/x86/cfl_sse4.c"
  "${AVM_ROOT}/av2/common/x86/highbd_convolve_2d_sse4.c"
  "${AVM_ROOT}/av2/common/x86/highbd_inv_txfm_sse4.c"
  "${AVM_ROOT}/av2/common/x86/highbd_jnt_convolve_sse4.c"
  "${AVM_ROOT}/av2/common/x86/highbd_warp_plane_sse4.c"
  "${AVM_ROOT}/av2/common/x86/intra_edge_sse4.c"
  "${AVM_ROOT}/av2/common/x86/optflow_refine_sse4.c"
  "${AVM_ROOT}/av2/common/x86/reconinter_sse4.c")

list(
  APPEND
  AVM_AV2_COMMON_INTRIN_AVX2
  "${AVM_ROOT}/av2/common/cdef_block_avx2.c"
  "${AVM_ROOT}/av2/common/x86/affine_optflow_refine_avx2.c"
  "${AVM_ROOT}/av2/common/x86/bawp_avx2.c"
  "${AVM_ROOT}/av2/common/x86/cfl_avx2.c"
  "${AVM_ROOT}/av2/common/x86/highbd_ccso_avx2.c"
  "${AVM_ROOT}/av2/common/x86/highbd_convolve_2d_avx2.c"
  "${AVM_ROOT}/av2/common/x86/highbd_inv_txfm_avx2.c"
  "${AVM_ROOT}/av2/common/x86/highbd_jnt_convolve_avx2.c"
  "${AVM_ROOT}/av2/common/x86/highbd_wiener_convolve_avx2.c"
  "${AVM_ROOT}/av2/common/x86/highbd_warp_affine_avx2.c"
  "${AVM_ROOT}/av2/common/x86/reconinter_avx2.c")

list(APPEND AVM_AV2_COMMON_INTRIN_AVX2
     "${AVM_ROOT}/av2/common/gdf_block_avx2.c")

list(APPEND AVM_AV2_ENCODER_ASM_SSE2 "${AVM_ROOT}/av2/encoder/x86/dct_sse2.asm")

list(
  APPEND
  AVM_AV2_ENCODER_INTRIN_SSE2
  "${AVM_ROOT}/av2/encoder/x86/encodetxb_sse2.c"
  "${AVM_ROOT}/av2/encoder/x86/highbd_block_error_intrin_sse2.c"
  "${AVM_ROOT}/av2/encoder/x86/highbd_temporal_filter_sse2.c"
  "${AVM_ROOT}/av2/encoder/x86/wedge_utils_sse2.c")

list(APPEND AVM_AV2_ENCODER_INTRIN_SSE3 "${AVM_ROOT}/av2/encoder/x86/ml_sse3.c")

list(
  APPEND
  AVM_AV2_ENCODER_INTRIN_SSE4_1
  "${AVM_ROOT}/av2/encoder/x86/av2_highbd_quantize_sse4.c"
  "${AVM_ROOT}/av2/encoder/x86/encodetxb_sse4.c"
  "${AVM_ROOT}/av2/encoder/x86/highbd_fwd_txfm_sse4.c"
  "${AVM_ROOT}/av2/encoder/x86/rdopt_sse4.c"
  "${AVM_ROOT}/av2/encoder/x86/pickrst_sse4.c")

list(
  APPEND
  AVM_AV2_ENCODER_INTRIN_AVX2
  "${AVM_ROOT}/av2/encoder/x86/av2_highbd_quantize_avx2.c"
  "${AVM_ROOT}/av2/encoder/x86/highbd_block_error_intrin_avx2.c"
  "${AVM_ROOT}/av2/encoder/x86/av2_fwd_txfm2d_avx2.c"
  "${AVM_ROOT}/av2/encoder/x86/highbd_fwd_txfm_avx2.c"
  "${AVM_ROOT}/av2/encoder/x86/wedge_utils_avx2.c"
  "${AVM_ROOT}/av2/encoder/x86/encodetxb_avx2.c"
  "${AVM_ROOT}/av2/encoder/x86/rdopt_avx2.c"
  "${AVM_ROOT}/av2/encoder/x86/pickrst_avx2.c")

list(
  APPEND
  AVM_AV2_ENCODER_INTRIN_NEON
  "${AVM_ROOT}/av2/encoder/arm/neon/ml_neon.c"
  "${AVM_ROOT}/av2/encoder/arm/neon/rdopt_neon.c"
  "${AVM_ROOT}/av2/encoder/arm/neon/encodetxb_neon.c"
  "${AVM_ROOT}/av2/encoder/arm/neon/hybrid_fwd_txfm_neon.c")

list(APPEND AVM_AV2_ENCODER_INTRIN_MSA
     "${AVM_ROOT}/av2/encoder/mips/msa/fdct4x4_msa.c"
     "${AVM_ROOT}/av2/encoder/mips/msa/temporal_filter_msa.c")

list(
  APPEND
  AVM_AV2_COMMON_INTRIN_NEON
  "${AVM_ROOT}/av2/common/arm/cfl_neon.c"
  "${AVM_ROOT}/av2/common/arm/convolve_neon.c"
  "${AVM_ROOT}/av2/common/arm/convolve_neon.h"
  "${AVM_ROOT}/av2/common/arm/reconinter_neon.c"
  "${AVM_ROOT}/av2/common/cdef_block_neon.c")

list(APPEND AVM_AV2_ENCODER_INTRIN_SSE4_2
     "${AVM_ROOT}/av2/encoder/x86/hash_sse42.c")

list(APPEND AVM_AV2_COMMON_INTRIN_VSX "${AVM_ROOT}/av2/common/ppc/cfl_ppc.c")

if(CONFIG_ACCOUNTING)
  list(APPEND AVM_AV2_DECODER_SOURCES "${AVM_ROOT}/av2/decoder/accounting.c"
       "${AVM_ROOT}/av2/decoder/accounting.h")
endif()

if(CONFIG_INSPECTION)
  list(APPEND AVM_AV2_DECODER_SOURCES "${AVM_ROOT}/av2/decoder/inspection.c"
       "${AVM_ROOT}/av2/decoder/inspection.h")
endif()

if(CONFIG_INTERNAL_STATS)
  list(APPEND AVM_AV2_ENCODER_SOURCES "${AVM_ROOT}/av2/encoder/blockiness.c")
endif()

# Setup AV2 common/decoder/encoder targets. The libavm target must exist before
# this function is called.
function(setup_av2_targets)
  add_library(avm_av2_common OBJECT ${AVM_AV2_COMMON_SOURCES})
  list(APPEND AVM_LIB_TARGETS avm_av2_common)
  target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_av2_common>)
  if(BUILD_SHARED_LIBS)
    target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_av2_common>)
  endif()

  if(CONFIG_AV2_DECODER)
    add_library(avm_av2_decoder OBJECT ${AVM_AV2_DECODER_SOURCES})
    set(AVM_LIB_TARGETS ${AVM_LIB_TARGETS} avm_av2_decoder)
    target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_av2_decoder>)
    if(BUILD_SHARED_LIBS)
      target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_av2_decoder>)
    endif()
  endif()

  if(CONFIG_AV2_ENCODER)
    add_library(avm_av2_encoder OBJECT ${AVM_AV2_ENCODER_SOURCES})
    set(AVM_LIB_TARGETS ${AVM_LIB_TARGETS} avm_av2_encoder)
    target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_av2_encoder>)
    if(BUILD_SHARED_LIBS)
      target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_av2_encoder>)
    endif()
  endif()

  if(HAVE_SSE2)
    require_compiler_flag_nomsvc("-msse2" NO)
    add_intrinsics_object_library("-msse2" "sse2" "avm_av2_common"
                                  "AVM_AV2_COMMON_INTRIN_SSE2")
    if(CONFIG_AV2_DECODER)
      if(AVM_AV2_DECODER_ASM_SSE2)
        add_asm_library("avm_av2_decoder_sse2" "AVM_AV2_DECODER_ASM_SSE2")
      endif()

      if(AVM_AV2_DECODER_INTRIN_SSE2)
        add_intrinsics_object_library("-msse2" "sse2" "avm_av2_decoder"
                                      "AVM_AV2_DECODER_INTRIN_SSE2")
      endif()
    endif()

    if(CONFIG_AV2_ENCODER)
      add_asm_library("avm_av2_encoder_sse2" "AVM_AV2_ENCODER_ASM_SSE2")
      add_intrinsics_object_library("-msse2" "sse2" "avm_av2_encoder"
                                    "AVM_AV2_ENCODER_INTRIN_SSE2")
    endif()
  endif()

  if(HAVE_SSE3)
    require_compiler_flag_nomsvc("-msse3" NO)
    if(CONFIG_AV2_ENCODER)
      add_intrinsics_object_library("-msse3" "sse3" "avm_av2_encoder"
                                    "AVM_AV2_ENCODER_INTRIN_SSE3")
    endif()
  endif()

  if(HAVE_SSSE3)
    require_compiler_flag_nomsvc("-mssse3" NO)
    add_intrinsics_object_library("-mssse3" "ssse3" "avm_av2_common"
                                  "AVM_AV2_COMMON_INTRIN_SSSE3")

    if(CONFIG_AV2_DECODER)
      if(AVM_AV2_DECODER_INTRIN_SSSE3)
        add_intrinsics_object_library("-mssse3" "ssse3" "avm_av2_decoder"
                                      "AVM_AV2_DECODER_INTRIN_SSSE3")
      endif()
    endif()
  endif()

  if(HAVE_SSE4_1)
    require_compiler_flag_nomsvc("-msse4.1" NO)
    add_intrinsics_object_library("-msse4.1" "sse4" "avm_av2_common"
                                  "AVM_AV2_COMMON_INTRIN_SSE4_1")

    if(CONFIG_AV2_ENCODER)
      if("${AVM_TARGET_CPU}" STREQUAL "x86_64")
        add_asm_library("avm_av2_encoder_ssse3"
                        "AVM_AV2_ENCODER_ASM_SSSE3_X86_64")
      endif()

      if(AVM_AV2_ENCODER_INTRIN_SSE4_1)
        add_intrinsics_object_library("-msse4.1" "sse4" "avm_av2_encoder"
                                      "AVM_AV2_ENCODER_INTRIN_SSE4_1")
      endif()
    endif()
  endif()

  if(HAVE_SSE4_2)
    require_compiler_flag_nomsvc("-msse4.2" NO)
    if(CONFIG_AV2_ENCODER)
      if(AVM_AV2_ENCODER_INTRIN_SSE4_2)
        add_intrinsics_object_library("-msse4.2" "sse42" "avm_av2_encoder"
                                      "AVM_AV2_ENCODER_INTRIN_SSE4_2")
      endif()
    endif()
  endif()

  if(HAVE_AVX2)
    require_compiler_flag_nomsvc("-mavx2" NO)
    add_intrinsics_object_library("-mavx2" "avx2" "avm_av2_common"
                                  "AVM_AV2_COMMON_INTRIN_AVX2")

    if(CONFIG_AV2_ENCODER)
      add_intrinsics_object_library("-mavx2" "avx2" "avm_av2_encoder"
                                    "AVM_AV2_ENCODER_INTRIN_AVX2")
    endif()
  endif()

  if(HAVE_NEON)
    if(AVM_AV2_COMMON_INTRIN_NEON)
      add_intrinsics_object_library(
        "${AVM_NEON_INTRIN_FLAG}" "neon" "avm_av2_common"
        "AVM_AV2_COMMON_INTRIN_NEON")
    endif()

    if(CONFIG_AV2_ENCODER)
      if(AVM_AV2_ENCODER_INTRIN_NEON)
        add_intrinsics_object_library(
          "${AVM_NEON_INTRIN_FLAG}" "neon" "avm_av2_encoder"
          "AVM_AV2_ENCODER_INTRIN_NEON")
      endif()
    endif()
  endif()

  if(HAVE_VSX)
    if(AVM_AV2_COMMON_INTRIN_VSX)
      add_intrinsics_object_library("-mvsx -maltivec" "vsx" "avm_av2_common"
                                    "AVM_AV2_COMMON_INTRIN_VSX")
    endif()
  endif()

  if(HAVE_MSA)
    add_intrinsics_object_library("" "msa" "avm_av2_encoder"
                                  "AVM_AV2_ENCODER_INTRIN_MSA")
  endif()

  # Pass the new lib targets up to the parent scope instance of
  # $AVM_LIB_TARGETS.
  set(AVM_LIB_TARGETS
      ${AVM_LIB_TARGETS}
      PARENT_SCOPE)
endfunction()
