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
if(AOM_AV1_AV1_CMAKE_)
  return()
endif() # AOM_AV1_AV1_CMAKE_
set(AOM_AV1_AV1_CMAKE_ 1)

list(
  APPEND
  AOM_AV1_COMMON_SOURCES
  "${AOM_ROOT}/common/args_helper.h"
  "${AOM_ROOT}/common/args_helper.c"
  "${AOM_ROOT}/av1/arg_defs.h"
  "${AOM_ROOT}/av1/arg_defs.c"
  "${AOM_ROOT}/av1/av1_iface_common.h"
  "${AOM_ROOT}/av1/common/alloccommon.c"
  "${AOM_ROOT}/av1/common/alloccommon.h"
  "${AOM_ROOT}/av1/common/av1_common_int.h"
  "${AOM_ROOT}/av1/common/av1_inv_txfm2d.c"
  "${AOM_ROOT}/av1/common/av1_loopfilter.c"
  "${AOM_ROOT}/av1/common/av1_loopfilter.h"
  "${AOM_ROOT}/av1/common/av1_txfm.c"
  "${AOM_ROOT}/av1/common/av1_txfm.h"
  "${AOM_ROOT}/av1/common/blockd.c"
  "${AOM_ROOT}/av1/common/blockd.h"
  "${AOM_ROOT}/av1/common/cdef.c"
  "${AOM_ROOT}/av1/common/cdef.h"
  "${AOM_ROOT}/av1/common/gdf.c"
  "${AOM_ROOT}/av1/common/gdf.h"
  "${AOM_ROOT}/av1/common/gdf_block.c"
  "${AOM_ROOT}/av1/common/gdf_block.h"
  "${AOM_ROOT}/av1/common/cdef_block.c"
  "${AOM_ROOT}/av1/common/cdef_block.h"
  "${AOM_ROOT}/av1/common/cfl.c"
  "${AOM_ROOT}/av1/common/cfl.h"
  "${AOM_ROOT}/av1/common/common.h"
  "${AOM_ROOT}/av1/common/common_data.h"
  "${AOM_ROOT}/av1/common/convolve.c"
  "${AOM_ROOT}/av1/common/convolve.h"
  "${AOM_ROOT}/av1/common/debugmodes.c"
  "${AOM_ROOT}/av1/common/entropy.c"
  "${AOM_ROOT}/av1/common/entropy.h"
  "${AOM_ROOT}/av1/common/entropymode.c"
  "${AOM_ROOT}/av1/common/entropymode.h"
  "${AOM_ROOT}/av1/common/entropymv.c"
  "${AOM_ROOT}/av1/common/entropymv.h"
  "${AOM_ROOT}/av1/common/enums.h"
  "${AOM_ROOT}/av1/common/filter.h"
  "${AOM_ROOT}/av1/common/frame_buffers.c"
  "${AOM_ROOT}/av1/common/frame_buffers.h"
  "${AOM_ROOT}/av1/common/idct.c"
  "${AOM_ROOT}/av1/common/idct.h"
  "${AOM_ROOT}/av1/common/mv.h"
  "${AOM_ROOT}/av1/common/mvref_common.c"
  "${AOM_ROOT}/av1/common/mvref_common.h"
  "${AOM_ROOT}/av1/common/obu_util.c"
  "${AOM_ROOT}/av1/common/obu_util.h"
  "${AOM_ROOT}/av1/common/odintrin.c"
  "${AOM_ROOT}/av1/common/odintrin.h"
  "${AOM_ROOT}/av1/common/pred_common.c"
  "${AOM_ROOT}/av1/common/pred_common.h"
  "${AOM_ROOT}/av1/common/quant_common.c"
  "${AOM_ROOT}/av1/common/quant_common.h"
  "${AOM_ROOT}/av1/common/reconinter.c"
  "${AOM_ROOT}/av1/common/reconinter.h"
  "${AOM_ROOT}/av1/common/reconintra.c"
  "${AOM_ROOT}/av1/common/reconintra.h"
  "${AOM_ROOT}/av1/common/resize.c"
  "${AOM_ROOT}/av1/common/resize.h"
  "${AOM_ROOT}/av1/common/restoration.c"
  "${AOM_ROOT}/av1/common/restoration.h"
  "${AOM_ROOT}/av1/common/scale.c"
  "${AOM_ROOT}/av1/common/scale.h"
  "${AOM_ROOT}/av1/common/scan.c"
  "${AOM_ROOT}/av1/common/scan.h"
  "${AOM_ROOT}/av1/common/secondary_tx.h"
  "${AOM_ROOT}/av1/common/seg_common.c"
  "${AOM_ROOT}/av1/common/seg_common.h"
  "${AOM_ROOT}/av1/common/thread_common.c"
  "${AOM_ROOT}/av1/common/thread_common.h"
  "${AOM_ROOT}/av1/common/tile_common.c"
  "${AOM_ROOT}/av1/common/tile_common.h"
  "${AOM_ROOT}/av1/common/timing.c"
  "${AOM_ROOT}/av1/common/timing.h"
  "${AOM_ROOT}/av1/common/tip.c"
  "${AOM_ROOT}/av1/common/tip.h"
  "${AOM_ROOT}/av1/common/txb_common.c"
  "${AOM_ROOT}/av1/common/txb_common.h"
  "${AOM_ROOT}/av1/common/warped_motion.c"
  "${AOM_ROOT}/av1/common/warped_motion.h"
  "${AOM_ROOT}/av1/common/hr_coding.h"
  "${AOM_ROOT}/av1/common/hr_coding.c"
  "${AOM_ROOT}/av1/common/cost.c"
  "${AOM_ROOT}/av1/common/cost.h"
  "${AOM_ROOT}/av1/common/entropy_inits_coeffs.h"
  "${AOM_ROOT}/av1/common/entropy_inits_modes.h"
  "${AOM_ROOT}/av1/common/entropy_inits_mv.h"
  "${AOM_ROOT}/av1/common/entropy_sideinfo.h")

list(APPEND AOM_AV1_COMMON_SOURCES "${AOM_ROOT}/av1/common/intra_matrix.c"
     "${AOM_ROOT}/av1/common/intra_matrix.h"
     "${AOM_ROOT}/av1/common/intra_dip.cc" "${AOM_ROOT}/av1/common/intra_dip.h")
list(APPEND AOM_AV1_COMMON_INTRIN_AVX2
     "${AOM_ROOT}/av1/common/x86/intra_matrix_avx2.c")

if(CONFIG_AV1_ENCODER)
  list(APPEND AOM_AV1_COMMON_SOURCES "${AOM_ROOT}/av1/encoder/erp_ml.c"
       "${AOM_ROOT}/av1/encoder/erp_ml.h")

  if(CONFIG_TENSORFLOW_LITE)
    list(APPEND AOM_AV1_COMMON_SOURCES
         "${AOM_ROOT}/av1/tflite_models/op_registrations.cc"
         "${AOM_ROOT}/av1/tflite_models/op_registrations.h")
  endif()
endif()

list(APPEND AOM_AV1_COMMON_SOURCES "${AOM_ROOT}/av1/common/ccso.c"
     "${AOM_ROOT}/av1/common/ccso.h")
list(APPEND AOM_AV1_ENCODER_SOURCES "${AOM_ROOT}/av1/encoder/pickccso.c"
     "${AOM_ROOT}/av1/encoder/pickccso.h")
list(APPEND AOM_AV1_COMMON_SOURCES "${AOM_ROOT}/av1/common/bru.c"
     "${AOM_ROOT}/av1/common/bru.h")

if(CONFIG_BAND_METADATA)
  list(APPEND AOM_AV1_COMMON_SOURCES
       "${AOM_ROOT}/av1/common/banding_metadata.c"
       "${AOM_ROOT}/av1/common/banding_metadata.h")
endif()

list(APPEND AOM_AV1_ENCODER_SOURCES "${AOM_ROOT}/av1/encoder/trellis_quant.c"
     "${AOM_ROOT}/av1/encoder/trellis_quant.h")
list(APPEND AOM_AV1_ENCODER_INTRIN_AVX2
     "${AOM_ROOT}/av1/encoder/x86/trellis_quant_avx2.c")

list(
  APPEND
  AOM_AV1_DECODER_SOURCES
  "${AOM_ROOT}/av1/av1_dx_iface.c"
  "${AOM_ROOT}/av1/decoder/decodeframe.c"
  "${AOM_ROOT}/av1/decoder/decodeframe.h"
  "${AOM_ROOT}/av1/decoder/decodemv.c"
  "${AOM_ROOT}/av1/decoder/decodemv.h"
  "${AOM_ROOT}/av1/decoder/decoder.c"
  "${AOM_ROOT}/av1/decoder/decoder.h"
  "${AOM_ROOT}/av1/decoder/decodetxb.c"
  "${AOM_ROOT}/av1/decoder/decodetxb.h"
  "${AOM_ROOT}/av1/decoder/detokenize.c"
  "${AOM_ROOT}/av1/decoder/detokenize.h"
  "${AOM_ROOT}/av1/decoder/dthread.h"
  "${AOM_ROOT}/av1/decoder/obu.h"
  "${AOM_ROOT}/av1/decoder/obu.c")

if(CONFIG_MULTILAYER_HLS)
  list(APPEND AOM_AV1_DECODER_SOURCES "${AOM_ROOT}/av1/decoder/obu_atlas.c"
       "${AOM_ROOT}/av1/decoder/obu_lcr.c" "${AOM_ROOT}/av1/decoder/obu_ops.c")
endif()

if(CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING)
  list(APPEND AOM_AV1_DECODER_SOURCES "${AOM_ROOT}/av1/decoder/obu_buf.c")
endif()

list(
  APPEND
  AOM_AV1_ENCODER_SOURCES
  "${AOM_ROOT}/av1/av1_cx_iface.c"
  "${AOM_ROOT}/av1/encoder/aq_complexity.c"
  "${AOM_ROOT}/av1/encoder/aq_complexity.h"
  "${AOM_ROOT}/av1/encoder/aq_cyclicrefresh.c"
  "${AOM_ROOT}/av1/encoder/aq_cyclicrefresh.h"
  "${AOM_ROOT}/av1/encoder/aq_variance.c"
  "${AOM_ROOT}/av1/encoder/aq_variance.h"
  "${AOM_ROOT}/av1/encoder/enc_enums.h"
  "${AOM_ROOT}/av1/encoder/av1_fwd_txfm2d.c"
  "${AOM_ROOT}/av1/encoder/av1_quantize.c"
  "${AOM_ROOT}/av1/encoder/av1_quantize.h"
  "${AOM_ROOT}/av1/encoder/bitstream.c"
  "${AOM_ROOT}/av1/encoder/bitstream.h"
  "${AOM_ROOT}/av1/encoder/block.h"
  "${AOM_ROOT}/av1/encoder/cnn.c"
  "${AOM_ROOT}/av1/encoder/cnn.h"
  "${AOM_ROOT}/av1/encoder/compound_type.c"
  "${AOM_ROOT}/av1/encoder/compound_type.h"
  "${AOM_ROOT}/av1/encoder/context_tree.c"
  "${AOM_ROOT}/av1/encoder/context_tree.h"
  "${AOM_ROOT}/av1/encoder/encodeframe.c"
  "${AOM_ROOT}/av1/encoder/encodeframe.h"
  "${AOM_ROOT}/av1/encoder/encodeframe_utils.c"
  "${AOM_ROOT}/av1/encoder/encodeframe_utils.h"
  "${AOM_ROOT}/av1/encoder/encodemb.c"
  "${AOM_ROOT}/av1/encoder/encodemb.h"
  "${AOM_ROOT}/av1/encoder/encodemv.c"
  "${AOM_ROOT}/av1/encoder/encodemv.h"
  "${AOM_ROOT}/av1/encoder/encode_strategy.c"
  "${AOM_ROOT}/av1/encoder/encode_strategy.h"
  "${AOM_ROOT}/av1/encoder/encoder.c"
  "${AOM_ROOT}/av1/encoder/encoder.h"
  "${AOM_ROOT}/av1/encoder/encoder_alloc.h"
  "${AOM_ROOT}/av1/encoder/encoder_utils.c"
  "${AOM_ROOT}/av1/encoder/encoder_utils.h"
  "${AOM_ROOT}/av1/encoder/encodetxb.c"
  "${AOM_ROOT}/av1/encoder/encodetxb.h"
  "${AOM_ROOT}/av1/encoder/ethread.c"
  "${AOM_ROOT}/av1/encoder/ethread.h"
  "${AOM_ROOT}/av1/encoder/extend.c"
  "${AOM_ROOT}/av1/encoder/extend.h"
  "${AOM_ROOT}/av1/encoder/firstpass.c"
  "${AOM_ROOT}/av1/encoder/firstpass.h"
  "${AOM_ROOT}/av1/encoder/global_motion.c"
  "${AOM_ROOT}/av1/encoder/global_motion.h"
  "${AOM_ROOT}/av1/encoder/global_motion_facade.c"
  "${AOM_ROOT}/av1/encoder/global_motion_facade.h"
  "${AOM_ROOT}/av1/encoder/gop_structure.c"
  "${AOM_ROOT}/av1/encoder/gop_structure.h"
  "${AOM_ROOT}/av1/encoder/grain_test_vectors.h"
  "${AOM_ROOT}/av1/encoder/hash.c"
  "${AOM_ROOT}/av1/encoder/hash.h"
  "${AOM_ROOT}/av1/encoder/hash_motion.c"
  "${AOM_ROOT}/av1/encoder/hash_motion.h"
  "${AOM_ROOT}/av1/encoder/hybrid_fwd_txfm.c"
  "${AOM_ROOT}/av1/encoder/hybrid_fwd_txfm.h"
  "${AOM_ROOT}/av1/encoder/interp_search.c"
  "${AOM_ROOT}/av1/encoder/interp_search.h"
  "${AOM_ROOT}/av1/encoder/level.c"
  "${AOM_ROOT}/av1/encoder/level.h"
  "${AOM_ROOT}/av1/encoder/lookahead.c"
  "${AOM_ROOT}/av1/encoder/lookahead.h"
  "${AOM_ROOT}/av1/encoder/mcomp.c"
  "${AOM_ROOT}/av1/encoder/mcomp.h"
  "${AOM_ROOT}/av1/encoder/ml.c"
  "${AOM_ROOT}/av1/encoder/ml.h"
  "${AOM_ROOT}/av1/encoder/model_rd.h"
  "${AOM_ROOT}/av1/encoder/motion_search_facade.c"
  "${AOM_ROOT}/av1/encoder/motion_search_facade.h"
  "${AOM_ROOT}/av1/encoder/mv_prec.c"
  "${AOM_ROOT}/av1/encoder/mv_prec.h"
  "${AOM_ROOT}/av1/encoder/palette.c"
  "${AOM_ROOT}/av1/encoder/palette.h"
  "${AOM_ROOT}/av1/encoder/partition_search.h"
  "${AOM_ROOT}/av1/encoder/partition_search.c"
  "${AOM_ROOT}/av1/encoder/partition_strategy.h"
  "${AOM_ROOT}/av1/encoder/partition_strategy.c"
  "${AOM_ROOT}/av1/encoder/pass2_strategy.h"
  "${AOM_ROOT}/av1/encoder/pass2_strategy.c"
  "${AOM_ROOT}/av1/encoder/pickcdef.c"
  "${AOM_ROOT}/av1/encoder/pickcdef.h"
  "${AOM_ROOT}/av1/encoder/picklpf.c"
  "${AOM_ROOT}/av1/encoder/picklpf.h"
  "${AOM_ROOT}/av1/encoder/pickrst.c"
  "${AOM_ROOT}/av1/encoder/pickrst.h"
  "${AOM_ROOT}/av1/encoder/ratectrl.c"
  "${AOM_ROOT}/av1/encoder/ratectrl.h"
  "${AOM_ROOT}/av1/encoder/rc_utils.h"
  "${AOM_ROOT}/av1/encoder/rd.c"
  "${AOM_ROOT}/av1/encoder/rd.h"
  "${AOM_ROOT}/av1/encoder/rdopt.c"
  "${AOM_ROOT}/av1/encoder/rdopt.h"
  "${AOM_ROOT}/av1/encoder/rdopt_utils.h"
  "${AOM_ROOT}/av1/encoder/reconinter_enc.c"
  "${AOM_ROOT}/av1/encoder/reconinter_enc.h"
  "${AOM_ROOT}/av1/encoder/segmentation.c"
  "${AOM_ROOT}/av1/encoder/segmentation.h"
  "${AOM_ROOT}/av1/encoder/speed_features.c"
  "${AOM_ROOT}/av1/encoder/speed_features.h"
  "${AOM_ROOT}/av1/encoder/subgop.c"
  "${AOM_ROOT}/av1/encoder/subgop.h"
  "${AOM_ROOT}/av1/encoder/superres_scale.c"
  "${AOM_ROOT}/av1/encoder/superres_scale.h"
  "${AOM_ROOT}/av1/encoder/temporal_filter.c"
  "${AOM_ROOT}/av1/encoder/temporal_filter.h"
  "${AOM_ROOT}/av1/encoder/tokenize.c"
  "${AOM_ROOT}/av1/encoder/tokenize.h"
  "${AOM_ROOT}/av1/encoder/tpl_model.c"
  "${AOM_ROOT}/av1/encoder/tpl_model.h"
  "${AOM_ROOT}/av1/encoder/tx_search.c"
  "${AOM_ROOT}/av1/encoder/tx_search.h"
  "${AOM_ROOT}/av1/encoder/intra_mode_search.c"
  "${AOM_ROOT}/av1/encoder/intra_mode_search.h"
  "${AOM_ROOT}/av1/encoder/intra_mode_search_utils.h"
  "${AOM_ROOT}/av1/encoder/wedge_utils.c"
  "${AOM_ROOT}/av1/encoder/av1_noise_estimate.c"
  "${AOM_ROOT}/av1/encoder/av1_noise_estimate.h"
  "${AOM_ROOT}/third_party/fastfeat/fast.c"
  "${AOM_ROOT}/third_party/fastfeat/fast.h"
  "${AOM_ROOT}/third_party/fastfeat/fast_9.c"
  "${AOM_ROOT}/third_party/fastfeat/nonmax.c"
  "${AOM_ROOT}/third_party/vector/vector.c"
  "${AOM_ROOT}/third_party/vector/vector.h"
  "${AOM_ROOT}/av1/encoder/dwt.c"
  "${AOM_ROOT}/av1/encoder/dwt.h"
  "${AOM_ROOT}/common/md5_utils.c"
  "${AOM_ROOT}/common/md5_utils.h"
  "${AOM_ROOT}/common/rawenc.c"
  "${AOM_ROOT}/common/rawenc.h")

if(CONFIG_TUNE_VMAF)
  list(APPEND AOM_AV1_ENCODER_SOURCES "${AOM_ROOT}/av1/encoder/tune_vmaf.c"
       "${AOM_ROOT}/av1/encoder/tune_vmaf.h")
endif()

if(CONFIG_ML_PART_SPLIT)
  list(
    APPEND
    AOM_AV1_ENCODER_SOURCES
    "${AOM_ROOT}/av1/encoder/partition_ml.c"
    "${AOM_ROOT}/av1/encoder/partition_ml.h"
    "${AOM_ROOT}/av1/encoder/part_split_prune_tflite.cc"
    "${AOM_ROOT}/av1/encoder/part_split_prune_tflite.h"
    "${AOM_ROOT}/av1/encoder/simple_intrapred_tflite_model_128x128.h"
    "${AOM_ROOT}/av1/encoder/simple_intrapred_tflite_model_64x64.h"
    "${AOM_ROOT}/av1/encoder/simple_intrapred_tflite_model_32x32.h"
    "${AOM_ROOT}/av1/encoder/simple_intrapred_tflite_model_16x16.h"
    "${AOM_ROOT}/av1/encoder/sms_part_split_prune_tflite_model.h")
endif()

if(CONFIG_DIP_EXT_PRUNING)
  list(
    APPEND
    AOM_AV1_ENCODER_SOURCES
    "${AOM_ROOT}/av1/encoder/intra_dip_mode_prune_tflite.h"
    "${AOM_ROOT}/av1/encoder/intra_dip_mode_prune_tflite.cc"
    "${AOM_ROOT}/av1/encoder/intra_dip_mode_prune_weights.cc")
endif()

if(CONFIG_MULTILAYER_HLS)
  list(
    APPEND AOM_AV1_ENCODER_SOURCES "${AOM_ROOT}/av1/encoder/bitstream_atlas.c"
    "${AOM_ROOT}/av1/encoder/bitstream_lcr.c"
    "${AOM_ROOT}/av1/encoder/bitstream_ops.c")
endif()

if(CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING)
  list(APPEND AOM_AV1_ENCODER_SOURCES "${AOM_ROOT}/av1/encoder/bitstream_buf.c")
endif()

list(APPEND AOM_AV1_COMMON_INTRIN_SSE2
     "${AOM_ROOT}/av1/common/cdef_block_sse2.c"
     "${AOM_ROOT}/av1/common/x86/cfl_sse2.c")

list(
  APPEND
  AOM_AV1_COMMON_INTRIN_SSSE3
  "${AOM_ROOT}/av1/common/cdef_block_ssse3.c"
  "${AOM_ROOT}/av1/common/x86/cfl_ssse3.c"
  "${AOM_ROOT}/av1/common/x86/highbd_convolve_2d_ssse3.c"
  "${AOM_ROOT}/av1/common/x86/highbd_wiener_convolve_ssse3.c"
  "${AOM_ROOT}/av1/common/x86/reconinter_ssse3.c")

list(
  APPEND
  AOM_AV1_COMMON_INTRIN_SSE4_1
  "${AOM_ROOT}/av1/common/cdef_block_sse4.c"
  "${AOM_ROOT}/av1/common/x86/av1_convolve_horiz_rs_sse4.c"
  "${AOM_ROOT}/av1/common/x86/av1_convolve_scale_sse4.c"
  "${AOM_ROOT}/av1/common/x86/cfl_sse4.c"
  "${AOM_ROOT}/av1/common/x86/highbd_convolve_2d_sse4.c"
  "${AOM_ROOT}/av1/common/x86/highbd_inv_txfm_sse4.c"
  "${AOM_ROOT}/av1/common/x86/highbd_jnt_convolve_sse4.c"
  "${AOM_ROOT}/av1/common/x86/highbd_warp_plane_sse4.c"
  "${AOM_ROOT}/av1/common/x86/intra_edge_sse4.c"
  "${AOM_ROOT}/av1/common/x86/optflow_refine_sse4.c"
  "${AOM_ROOT}/av1/common/x86/reconinter_sse4.c")

list(
  APPEND
  AOM_AV1_COMMON_INTRIN_AVX2
  "${AOM_ROOT}/av1/common/cdef_block_avx2.c"
  "${AOM_ROOT}/av1/common/x86/affine_optflow_refine_avx2.c"
  "${AOM_ROOT}/av1/common/x86/bawp_avx2.c"
  "${AOM_ROOT}/av1/common/x86/cfl_avx2.c"
  "${AOM_ROOT}/av1/common/x86/highbd_ccso_avx2.c"
  "${AOM_ROOT}/av1/common/x86/highbd_convolve_2d_avx2.c"
  "${AOM_ROOT}/av1/common/x86/highbd_inv_txfm_avx2.c"
  "${AOM_ROOT}/av1/common/x86/highbd_jnt_convolve_avx2.c"
  "${AOM_ROOT}/av1/common/x86/highbd_wiener_convolve_avx2.c"
  "${AOM_ROOT}/av1/common/x86/highbd_warp_affine_avx2.c"
  "${AOM_ROOT}/av1/common/x86/reconinter_avx2.c")

list(APPEND AOM_AV1_COMMON_INTRIN_AVX2
     "${AOM_ROOT}/av1/common/gdf_block_avx2.c")

list(APPEND AOM_AV1_ENCODER_ASM_SSE2 "${AOM_ROOT}/av1/encoder/x86/dct_sse2.asm")

list(
  APPEND
  AOM_AV1_ENCODER_INTRIN_SSE2
  "${AOM_ROOT}/av1/encoder/x86/av1_quantize_sse2.c"
  "${AOM_ROOT}/av1/encoder/x86/encodetxb_sse2.c"
  "${AOM_ROOT}/av1/encoder/x86/highbd_block_error_intrin_sse2.c"
  "${AOM_ROOT}/av1/encoder/x86/highbd_temporal_filter_sse2.c"
  "${AOM_ROOT}/av1/encoder/x86/wedge_utils_sse2.c")

list(APPEND AOM_AV1_ENCODER_INTRIN_SSE3 "${AOM_ROOT}/av1/encoder/x86/ml_sse3.c")

list(APPEND AOM_AV1_ENCODER_ASM_SSSE3_X86_64
     "${AOM_ROOT}/av1/encoder/x86/av1_quantize_ssse3_x86_64.asm")

list(
  APPEND
  AOM_AV1_ENCODER_INTRIN_SSE4_1
  "${AOM_ROOT}/av1/encoder/x86/av1_highbd_quantize_sse4.c"
  "${AOM_ROOT}/av1/encoder/x86/encodetxb_sse4.c"
  "${AOM_ROOT}/av1/encoder/x86/highbd_fwd_txfm_sse4.c"
  "${AOM_ROOT}/av1/encoder/x86/rdopt_sse4.c"
  "${AOM_ROOT}/av1/encoder/x86/pickrst_sse4.c")

list(
  APPEND
  AOM_AV1_ENCODER_INTRIN_AVX2
  "${AOM_ROOT}/av1/encoder/x86/av1_quantize_avx2.c"
  "${AOM_ROOT}/av1/encoder/x86/av1_highbd_quantize_avx2.c"
  "${AOM_ROOT}/av1/encoder/x86/highbd_block_error_intrin_avx2.c"
  "${AOM_ROOT}/av1/encoder/x86/av1_fwd_txfm_avx2.h"
  "${AOM_ROOT}/av1/encoder/x86/av1_fwd_txfm2d_avx2.c"
  "${AOM_ROOT}/av1/encoder/x86/highbd_fwd_txfm_avx2.c"
  "${AOM_ROOT}/av1/encoder/x86/wedge_utils_avx2.c"
  "${AOM_ROOT}/av1/encoder/x86/encodetxb_avx2.c"
  "${AOM_ROOT}/av1/encoder/x86/rdopt_avx2.c"
  "${AOM_ROOT}/av1/encoder/x86/pickrst_avx2.c")

list(
  APPEND
  AOM_AV1_ENCODER_INTRIN_NEON
  "${AOM_ROOT}/av1/encoder/arm/neon/quantize_neon.c"
  "${AOM_ROOT}/av1/encoder/arm/neon/ml_neon.c"
  "${AOM_ROOT}/av1/encoder/arm/neon/rdopt_neon.c"
  "${AOM_ROOT}/av1/encoder/arm/neon/encodetxb_neon.c"
  "${AOM_ROOT}/av1/encoder/arm/neon/hybrid_fwd_txfm_neon.c")

list(APPEND AOM_AV1_ENCODER_INTRIN_MSA
     "${AOM_ROOT}/av1/encoder/mips/msa/fdct4x4_msa.c"
     "${AOM_ROOT}/av1/encoder/mips/msa/temporal_filter_msa.c")

list(
  APPEND
  AOM_AV1_COMMON_INTRIN_NEON
  "${AOM_ROOT}/av1/common/arm/cfl_neon.c"
  "${AOM_ROOT}/av1/common/arm/convolve_neon.c"
  "${AOM_ROOT}/av1/common/arm/convolve_neon.h"
  "${AOM_ROOT}/av1/common/arm/reconinter_neon.c"
  "${AOM_ROOT}/av1/common/cdef_block_neon.c")

list(APPEND AOM_AV1_ENCODER_INTRIN_SSE4_2
     "${AOM_ROOT}/av1/encoder/x86/hash_sse42.c")

list(APPEND AOM_AV1_COMMON_INTRIN_VSX "${AOM_ROOT}/av1/common/ppc/cfl_ppc.c")

if(CONFIG_ACCOUNTING)
  list(APPEND AOM_AV1_DECODER_SOURCES "${AOM_ROOT}/av1/decoder/accounting.c"
       "${AOM_ROOT}/av1/decoder/accounting.h")
endif()

if(CONFIG_INSPECTION)
  list(APPEND AOM_AV1_DECODER_SOURCES "${AOM_ROOT}/av1/decoder/inspection.c"
       "${AOM_ROOT}/av1/decoder/inspection.h")
endif()

if(CONFIG_INTERNAL_STATS)
  list(APPEND AOM_AV1_ENCODER_SOURCES "${AOM_ROOT}/av1/encoder/blockiness.c")
endif()

# Setup AV1 common/decoder/encoder targets. The libaom target must exist before
# this function is called.
function(setup_av1_targets)
  add_library(aom_av1_common OBJECT ${AOM_AV1_COMMON_SOURCES})
  list(APPEND AOM_LIB_TARGETS aom_av1_common)
  target_sources(aom PRIVATE $<TARGET_OBJECTS:aom_av1_common>)
  if(BUILD_SHARED_LIBS)
    target_sources(aom_static PRIVATE $<TARGET_OBJECTS:aom_av1_common>)
  endif()

  if(CONFIG_AV1_DECODER)
    add_library(aom_av1_decoder OBJECT ${AOM_AV1_DECODER_SOURCES})
    set(AOM_LIB_TARGETS ${AOM_LIB_TARGETS} aom_av1_decoder)
    target_sources(aom PRIVATE $<TARGET_OBJECTS:aom_av1_decoder>)
    if(BUILD_SHARED_LIBS)
      target_sources(aom_static PRIVATE $<TARGET_OBJECTS:aom_av1_decoder>)
    endif()
  endif()

  if(CONFIG_AV1_ENCODER)
    add_library(aom_av1_encoder OBJECT ${AOM_AV1_ENCODER_SOURCES})
    set(AOM_LIB_TARGETS ${AOM_LIB_TARGETS} aom_av1_encoder)
    target_sources(aom PRIVATE $<TARGET_OBJECTS:aom_av1_encoder>)
    if(BUILD_SHARED_LIBS)
      target_sources(aom_static PRIVATE $<TARGET_OBJECTS:aom_av1_encoder>)
    endif()
  endif()

  if(HAVE_SSE2)
    require_compiler_flag_nomsvc("-msse2" NO)
    add_intrinsics_object_library("-msse2" "sse2" "aom_av1_common"
                                  "AOM_AV1_COMMON_INTRIN_SSE2")
    if(CONFIG_AV1_DECODER)
      if(AOM_AV1_DECODER_ASM_SSE2)
        add_asm_library("aom_av1_decoder_sse2" "AOM_AV1_DECODER_ASM_SSE2")
      endif()

      if(AOM_AV1_DECODER_INTRIN_SSE2)
        add_intrinsics_object_library("-msse2" "sse2" "aom_av1_decoder"
                                      "AOM_AV1_DECODER_INTRIN_SSE2")
      endif()
    endif()

    if(CONFIG_AV1_ENCODER)
      add_asm_library("aom_av1_encoder_sse2" "AOM_AV1_ENCODER_ASM_SSE2")
      add_intrinsics_object_library("-msse2" "sse2" "aom_av1_encoder"
                                    "AOM_AV1_ENCODER_INTRIN_SSE2")
    endif()
  endif()

  if(HAVE_SSE3)
    require_compiler_flag_nomsvc("-msse3" NO)
    if(CONFIG_AV1_ENCODER)
      add_intrinsics_object_library("-msse3" "sse3" "aom_av1_encoder"
                                    "AOM_AV1_ENCODER_INTRIN_SSE3")
    endif()
  endif()

  if(HAVE_SSSE3)
    require_compiler_flag_nomsvc("-mssse3" NO)
    add_intrinsics_object_library("-mssse3" "ssse3" "aom_av1_common"
                                  "AOM_AV1_COMMON_INTRIN_SSSE3")

    if(CONFIG_AV1_DECODER)
      if(AOM_AV1_DECODER_INTRIN_SSSE3)
        add_intrinsics_object_library("-mssse3" "ssse3" "aom_av1_decoder"
                                      "AOM_AV1_DECODER_INTRIN_SSSE3")
      endif()
    endif()
  endif()

  if(HAVE_SSE4_1)
    require_compiler_flag_nomsvc("-msse4.1" NO)
    add_intrinsics_object_library("-msse4.1" "sse4" "aom_av1_common"
                                  "AOM_AV1_COMMON_INTRIN_SSE4_1")

    if(CONFIG_AV1_ENCODER)
      if("${AOM_TARGET_CPU}" STREQUAL "x86_64")
        add_asm_library("aom_av1_encoder_ssse3"
                        "AOM_AV1_ENCODER_ASM_SSSE3_X86_64")
      endif()

      if(AOM_AV1_ENCODER_INTRIN_SSE4_1)
        add_intrinsics_object_library("-msse4.1" "sse4" "aom_av1_encoder"
                                      "AOM_AV1_ENCODER_INTRIN_SSE4_1")
      endif()
    endif()
  endif()

  if(HAVE_SSE4_2)
    require_compiler_flag_nomsvc("-msse4.2" NO)
    if(CONFIG_AV1_ENCODER)
      if(AOM_AV1_ENCODER_INTRIN_SSE4_2)
        add_intrinsics_object_library("-msse4.2" "sse42" "aom_av1_encoder"
                                      "AOM_AV1_ENCODER_INTRIN_SSE4_2")
      endif()
    endif()
  endif()

  if(HAVE_AVX2)
    require_compiler_flag_nomsvc("-mavx2" NO)
    add_intrinsics_object_library("-mavx2" "avx2" "aom_av1_common"
                                  "AOM_AV1_COMMON_INTRIN_AVX2")

    if(CONFIG_AV1_ENCODER)
      add_intrinsics_object_library("-mavx2" "avx2" "aom_av1_encoder"
                                    "AOM_AV1_ENCODER_INTRIN_AVX2")
    endif()
  endif()

  if(HAVE_NEON)
    if(AOM_AV1_COMMON_INTRIN_NEON)
      add_intrinsics_object_library(
        "${AOM_NEON_INTRIN_FLAG}" "neon" "aom_av1_common"
        "AOM_AV1_COMMON_INTRIN_NEON")
    endif()

    if(CONFIG_AV1_ENCODER)
      if(AOM_AV1_ENCODER_INTRIN_NEON)
        add_intrinsics_object_library(
          "${AOM_NEON_INTRIN_FLAG}" "neon" "aom_av1_encoder"
          "AOM_AV1_ENCODER_INTRIN_NEON")
      endif()
    endif()
  endif()

  if(HAVE_VSX)
    if(AOM_AV1_COMMON_INTRIN_VSX)
      add_intrinsics_object_library("-mvsx -maltivec" "vsx" "aom_av1_common"
                                    "AOM_AV1_COMMON_INTRIN_VSX")
    endif()
  endif()

  if(HAVE_MSA)
    add_intrinsics_object_library("" "msa" "aom_av1_encoder"
                                  "AOM_AV1_ENCODER_INTRIN_MSA")
  endif()

  # Pass the new lib targets up to the parent scope instance of
  # $AOM_LIB_TARGETS.
  set(AOM_LIB_TARGETS
      ${AOM_LIB_TARGETS}
      PARENT_SCOPE)
endfunction()
