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
if(AVM_AVM_DSP_AVM_DSP_CMAKE_)
  return()
endif() # AVM_AVM_DSP_AVM_DSP_CMAKE_
set(AVM_AVM_DSP_AVM_DSP_CMAKE_ 1)

list(
  APPEND
  AVM_DSP_COMMON_SOURCES
  "${AVM_ROOT}/avm_dsp/avm_convolve.c"
  "${AVM_ROOT}/avm_dsp/avm_dsp_common.h"
  "${AVM_ROOT}/avm_dsp/avm_filter.h"
  "${AVM_ROOT}/avm_dsp/avm_simd.h"
  "${AVM_ROOT}/avm_dsp/avm_simd_inline.h"
  "${AVM_ROOT}/avm_dsp/bitreader_buffer.c"
  "${AVM_ROOT}/avm_dsp/bitreader_buffer.h"
  "${AVM_ROOT}/avm_dsp/bitwriter_buffer.c"
  "${AVM_ROOT}/avm_dsp/bitwriter_buffer.h"
  "${AVM_ROOT}/avm_dsp/blend.h"
  "${AVM_ROOT}/avm_dsp/blend_a64_hmask.c"
  "${AVM_ROOT}/avm_dsp/blend_a64_mask.c"
  "${AVM_ROOT}/avm_dsp/blend_a64_vmask.c"
  "${AVM_ROOT}/avm_dsp/entcode.c"
  "${AVM_ROOT}/avm_dsp/entcode.h"
  "${AVM_ROOT}/avm_dsp/fft.c"
  "${AVM_ROOT}/avm_dsp/fft_common.h"
  "${AVM_ROOT}/avm_dsp/grain_synthesis.c"
  "${AVM_ROOT}/avm_dsp/grain_synthesis.h"
  "${AVM_ROOT}/avm_dsp/intrapred.c"
  "${AVM_ROOT}/avm_dsp/intrapred_common.h"
  "${AVM_ROOT}/avm_dsp/loopfilter.h"
  "${AVM_ROOT}/avm_dsp/loopfilter.c"
  "${AVM_ROOT}/avm_dsp/prob.h"
  "${AVM_ROOT}/avm_dsp/recenter.h"
  "${AVM_ROOT}/avm_dsp/simd/v128_intrinsics.h"
  "${AVM_ROOT}/avm_dsp/simd/v128_intrinsics_c.h"
  "${AVM_ROOT}/avm_dsp/simd/v256_intrinsics.h"
  "${AVM_ROOT}/avm_dsp/simd/v256_intrinsics_c.h"
  "${AVM_ROOT}/avm_dsp/simd/v64_intrinsics.h"
  "${AVM_ROOT}/avm_dsp/simd/v64_intrinsics_c.h"
  "${AVM_ROOT}/avm_dsp/sad.c"
  "${AVM_ROOT}/avm_dsp/subtract.c"
  "${AVM_ROOT}/avm_dsp/txfm_common.h"
  "${AVM_ROOT}/avm_dsp/x86/convolve_common_intrin.h"
  "${AVM_ROOT}/avm_dsp/avg.c")

list(
  APPEND
  AVM_DSP_COMMON_ASM_SSE2
  "${AVM_ROOT}/avm_dsp/x86/avm_high_subpixel_8t_sse2.asm"
  "${AVM_ROOT}/avm_dsp/x86/avm_high_subpixel_bilinear_sse2.asm"
  "${AVM_ROOT}/avm_dsp/x86/avm_subpixel_8t_sse2.asm"
  "${AVM_ROOT}/avm_dsp/x86/avm_subpixel_bilinear_sse2.asm"
  "${AVM_ROOT}/avm_dsp/x86/highbd_intrapred_asm_sse2.asm"
  "${AVM_ROOT}/avm_dsp/x86/highbd_sad_sse2.asm"
  "${AVM_ROOT}/avm_dsp/x86/inv_wht_sse2.asm")

list(
  APPEND
  AVM_DSP_COMMON_INTRIN_SSE2
  "${AVM_ROOT}/avm_dsp/x86/avm_convolve_copy_sse2.c"
  "${AVM_ROOT}/avm_dsp/x86/avm_asm_stubs.c"
  "${AVM_ROOT}/avm_dsp/x86/convolve.h"
  "${AVM_ROOT}/avm_dsp/x86/convolve_sse2.h"
  "${AVM_ROOT}/avm_dsp/x86/fft_sse2.c"
  "${AVM_ROOT}/avm_dsp/x86/highbd_convolve_sse2.c"
  "${AVM_ROOT}/avm_dsp/x86/highbd_intrapred_sse2.c"
  "${AVM_ROOT}/avm_dsp/x86/highbd_subtract_sse2.c"
  "${AVM_ROOT}/avm_dsp/x86/intrapred_x86.h"
  "${AVM_ROOT}/avm_dsp/x86/lpf_common_sse2.h"
  "${AVM_ROOT}/avm_dsp/x86/mem_sse2.h"
  "${AVM_ROOT}/avm_dsp/x86/transpose_sse2.h"
  "${AVM_ROOT}/avm_dsp/x86/txfm_common_sse2.h"
  "${AVM_ROOT}/avm_dsp/x86/sum_squares_sse2.h"
  "${AVM_ROOT}/avm_dsp/x86/avg_intrin_sse2.c"
  "${AVM_ROOT}/avm_dsp/x86/bitdepth_conversion_sse2.h")

list(APPEND AVM_DSP_COMMON_ASM_SSSE3
     "${AVM_ROOT}/avm_dsp/x86/avm_subpixel_8t_ssse3.asm"
     "${AVM_ROOT}/avm_dsp/x86/avm_subpixel_bilinear_ssse3.asm")

list(APPEND AVM_DSP_COMMON_INTRIN_SSSE3
     "${AVM_ROOT}/avm_dsp/x86/highbd_convolve_ssse3.c")

list(
  APPEND
  AVM_DSP_COMMON_INTRIN_SSE4_1
  "${AVM_ROOT}/avm_dsp/x86/blend_mask_sse4.h"
  "${AVM_ROOT}/avm_dsp/x86/blend_a64_hmask_sse4.c"
  "${AVM_ROOT}/avm_dsp/x86/blend_a64_mask_sse4.c"
  "${AVM_ROOT}/avm_dsp/x86/blend_a64_vmask_sse4.c"
  "${AVM_ROOT}/avm_dsp/x86/loopfilter_sse4.c")

list(
  APPEND
  AVM_DSP_COMMON_INTRIN_AVX2
  "${AVM_ROOT}/avm_dsp/x86/avm_convolve_copy_avx2.c"
  "${AVM_ROOT}/avm_dsp/x86/common_avx2.h"
  "${AVM_ROOT}/avm_dsp/x86/convolve_avx2.h"
  "${AVM_ROOT}/avm_dsp/x86/fft_avx2.c"
  "${AVM_ROOT}/avm_dsp/x86/highbd_convolve_avx2.c"
  "${AVM_ROOT}/avm_dsp/x86/intrapred_avx2.c"
  "${AVM_ROOT}/avm_dsp/x86/blend_a64_mask_avx2.c"
  "${AVM_ROOT}/avm_dsp/x86/avg_intrin_avx2.c"
  "${AVM_ROOT}/avm_dsp/x86/bitdepth_conversion_avx2.h")

list(APPEND AVM_DSP_DECODER_INTRIN_AVX2 "${AVM_ROOT}/avm_dsp/x86/entdec_avx2.c")

list(
  APPEND
  AVM_DSP_COMMON_INTRIN_NEON
  "${AVM_ROOT}/avm_dsp/arm/avm_convolve_copy_neon.c"
  "${AVM_ROOT}/avm_dsp/arm/fwd_txfm_neon.c"
  "${AVM_ROOT}/avm_dsp/arm/loopfilter_neon.c"
  "${AVM_ROOT}/avm_dsp/arm/intrapred_neon.c"
  "${AVM_ROOT}/avm_dsp/arm/subtract_neon.c"
  "${AVM_ROOT}/avm_dsp/arm/blend_a64_mask_neon.c")

list(
  APPEND
  AVM_DSP_COMMON_INTRIN_DSPR2
  "${AVM_ROOT}/avm_dsp/mips/avm_convolve_copy_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/common_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/common_dspr2.h"
  "${AVM_ROOT}/avm_dsp/mips/convolve2_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/convolve2_horiz_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/convolve2_vert_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/convolve8_horiz_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/convolve8_vert_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/convolve_common_dspr2.h"
  "${AVM_ROOT}/avm_dsp/mips/intrapred16_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/intrapred4_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/intrapred8_dspr2.c"
  "${AVM_ROOT}/avm_dsp/mips/inv_txfm_dspr2.h")

list(
  APPEND
  AVM_DSP_COMMON_INTRIN_MSA
  "${AVM_ROOT}/avm_dsp/mips/avm_convolve8_horiz_msa.c"
  "${AVM_ROOT}/avm_dsp/mips/avm_convolve8_vert_msa.c"
  "${AVM_ROOT}/avm_dsp/mips/avm_convolve_copy_msa.c"
  "${AVM_ROOT}/avm_dsp/mips/avm_convolve_msa.h"
  "${AVM_ROOT}/avm_dsp/mips/macros_msa.h")

if(CONFIG_AV2_DECODER)
  list(
    APPEND
    AVM_DSP_DECODER_SOURCES
    "${AVM_ROOT}/avm_dsp/binary_codes_reader.c"
    "${AVM_ROOT}/avm_dsp/binary_codes_reader.h"
    "${AVM_ROOT}/avm_dsp/bitreader.c"
    "${AVM_ROOT}/avm_dsp/bitreader.h"
    "${AVM_ROOT}/avm_dsp/entdec.c"
    "${AVM_ROOT}/avm_dsp/entdec.h")
endif()

if(CONFIG_AV2_ENCODER)
  list(
    APPEND
    AVM_DSP_ENCODER_SOURCES
    "${AVM_ROOT}/avm_dsp/binary_codes_writer.c"
    "${AVM_ROOT}/avm_dsp/binary_codes_writer.h"
    "${AVM_ROOT}/avm_dsp/bitwriter.c"
    "${AVM_ROOT}/avm_dsp/bitwriter.h"
    "${AVM_ROOT}/avm_dsp/blk_sse_sum.c"
    "${AVM_ROOT}/avm_dsp/entenc.c"
    "${AVM_ROOT}/avm_dsp/entenc.h"
    "${AVM_ROOT}/avm_dsp/fwd_txfm.c"
    "${AVM_ROOT}/avm_dsp/grain_table.c"
    "${AVM_ROOT}/avm_dsp/grain_table.h"
    "${AVM_ROOT}/avm_dsp/noise_model.c"
    "${AVM_ROOT}/avm_dsp/noise_model.h"
    "${AVM_ROOT}/avm_dsp/noise_util.c"
    "${AVM_ROOT}/avm_dsp/noise_util.h"
    "${AVM_ROOT}/avm_dsp/psnr.c"
    "${AVM_ROOT}/avm_dsp/psnr.h"
    "${AVM_ROOT}/avm_dsp/quantize.c"
    "${AVM_ROOT}/avm_dsp/quantize.h"
    "${AVM_ROOT}/avm_dsp/sad4d.c"
    "${AVM_ROOT}/avm_dsp/sse.c"
    "${AVM_ROOT}/avm_dsp/sad_av2.c"
    "${AVM_ROOT}/avm_dsp/sum_squares.c"
    "${AVM_ROOT}/avm_dsp/variance.c"
    "${AVM_ROOT}/avm_dsp/variance.h")

  # Flow estimation library
  list(
    APPEND
    AVM_DSP_ENCODER_SOURCES
    "${AVM_ROOT}/avm_dsp/pyramid.c"
    "${AVM_ROOT}/avm_dsp/flow_estimation/corner_detect.c"
    "${AVM_ROOT}/avm_dsp/flow_estimation/corner_match.c"
    "${AVM_ROOT}/avm_dsp/flow_estimation/disflow.c"
    "${AVM_ROOT}/avm_dsp/flow_estimation/flow_estimation.c"
    "${AVM_ROOT}/avm_dsp/flow_estimation/ransac.c")

  list(APPEND AVM_DSP_ENCODER_INTRIN_SSE4_1
       "${AVM_ROOT}/avm_dsp/flow_estimation/x86/corner_match_sse4.c"
       "${AVM_ROOT}/avm_dsp/flow_estimation/x86/disflow_sse4.c")

  list(APPEND AVM_DSP_ENCODER_INTRIN_AVX2
       "${AVM_ROOT}/avm_dsp/flow_estimation/x86/corner_match_avx2.c"
       "${AVM_ROOT}/avm_dsp/flow_estimation/x86/disflow_avx2.c")

  list(APPEND AVM_DSP_ENCODER_INTRIN_NEON
       "${AVM_ROOT}/avm_dsp/flow_estimation/arm/disflow_neon.c")

  list(
    APPEND
    AVM_DSP_ENCODER_ASM_SSE2
    "${AVM_ROOT}/avm_dsp/x86/highbd_sad4d_sse2.asm"
    "${AVM_ROOT}/avm_dsp/x86/highbd_subpel_variance_impl_sse2.asm"
    "${AVM_ROOT}/avm_dsp/x86/highbd_variance_impl_sse2.asm")

  list(
    APPEND
    AVM_DSP_ENCODER_INTRIN_SSE2
    "${AVM_ROOT}/avm_dsp/x86/fwd_txfm_impl_sse2.h"
    "${AVM_ROOT}/avm_dsp/x86/fwd_txfm_sse2.c"
    "${AVM_ROOT}/avm_dsp/x86/fwd_txfm_sse2.h"
    "${AVM_ROOT}/avm_dsp/x86/highbd_quantize_intrin_sse2.c"
    "${AVM_ROOT}/avm_dsp/x86/highbd_variance_sse2.c"
    "${AVM_ROOT}/avm_dsp/x86/highbd_adaptive_quantize_sse2.c"
    "${AVM_ROOT}/avm_dsp/x86/quantize_x86.h"
    "${AVM_ROOT}/avm_dsp/x86/blk_sse_sum_sse2.c"
    "${AVM_ROOT}/avm_dsp/x86/sum_squares_sse2.c"
    "${AVM_ROOT}/avm_dsp/x86/variance_sse2.c")

  list(APPEND AVM_DSP_ENCODER_ASM_SSSE3_X86_64
       "${AVM_ROOT}/avm_dsp/x86/quantize_ssse3_x86_64.asm")

  list(
    APPEND
    AVM_DSP_ENCODER_INTRIN_AVX2
    "${AVM_ROOT}/avm_dsp/x86/masked_sad_intrin_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/highbd_quantize_intrin_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/highbd_adaptive_quantize_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/sad4d_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/sad_highbd_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/sad_impl_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/variance_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/highbd_variance_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/sse_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/variance_impl_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/blk_sse_sum_avx2.c"
    "${AVM_ROOT}/avm_dsp/x86/sum_squares_avx2.c")

  list(
    APPEND
    AVM_DSP_ENCODER_INTRIN_SSSE3
    "${AVM_ROOT}/avm_dsp/x86/masked_sad_intrin_ssse3.h"
    "${AVM_ROOT}/avm_dsp/x86/masked_sad_intrin_ssse3.c"
    "${AVM_ROOT}/avm_dsp/x86/masked_sad4d_ssse3.c"
    "${AVM_ROOT}/avm_dsp/x86/masked_variance_intrin_ssse3.h"
    "${AVM_ROOT}/avm_dsp/x86/masked_variance_intrin_ssse3.c"
    "${AVM_ROOT}/avm_dsp/x86/variance_impl_ssse3.c"
    "${AVM_ROOT}/avm_dsp/x86/jnt_sad_ssse3.c")

  list(APPEND AVM_DSP_ENCODER_INTRIN_SSE4_1
       "${AVM_ROOT}/avm_dsp/x86/highbd_variance_sse4.c"
       "${AVM_ROOT}/avm_dsp/x86/sse_sse4.c")

  list(APPEND AVM_DSP_ENCODER_INTRIN_NEON "${AVM_ROOT}/avm_dsp/arm/avg_neon.c"
       "${AVM_ROOT}/avm_dsp/arm/sse_neon.c"
       "${AVM_ROOT}/avm_dsp/arm/sum_squares_neon.c")

  list(
    APPEND
    AVM_DSP_ENCODER_INTRIN_MSA
    "${AVM_ROOT}/avm_dsp/mips/sad_msa.c"
    "${AVM_ROOT}/avm_dsp/mips/subtract_msa.c"
    "${AVM_ROOT}/avm_dsp/mips/variance_msa.c"
    "${AVM_ROOT}/avm_dsp/mips/sub_pixel_variance_msa.c")

  if(CONFIG_INTERNAL_STATS)
    list(APPEND AVM_DSP_ENCODER_SOURCES "${AVM_ROOT}/avm_dsp/fastssim.c"
         "${AVM_ROOT}/avm_dsp/psnrhvs.c" "${AVM_ROOT}/avm_dsp/ssim.c"
         "${AVM_ROOT}/avm_dsp/ssim.h")
  endif()

  if(CONFIG_TUNE_VMAF)
    list(APPEND AVM_DSP_ENCODER_SOURCES "${AVM_ROOT}/avm_dsp/vmaf.c"
         "${AVM_ROOT}/avm_dsp/vmaf.h")
  endif()
endif()

# Creates avm_dsp build targets. Must not be called until after libavm target
# has been created.
function(setup_avm_dsp_targets)
  add_library(avm_dsp_common OBJECT ${AVM_DSP_COMMON_SOURCES})
  list(APPEND AVM_LIB_TARGETS avm_dsp_common)
  create_dummy_source_file("avm_av2" "c" "dummy_source_file")
  add_library(avm_dsp OBJECT "${dummy_source_file}")
  target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_dsp_common>)
  if(BUILD_SHARED_LIBS)
    target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_dsp_common>)
  endif()
  list(APPEND AVM_LIB_TARGETS avm_dsp)

  # Not all generators support libraries consisting only of object files. Add a
  # dummy source file to the avm_dsp target.
  add_dummy_source_file_to_target("avm_dsp" "c")

  if(CONFIG_AV2_DECODER)
    add_library(avm_dsp_decoder OBJECT ${AVM_DSP_DECODER_SOURCES})
    list(APPEND AVM_LIB_TARGETS avm_dsp_decoder)
    target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_dsp_decoder>)
    if(BUILD_SHARED_LIBS)
      target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_dsp_decoder>)
    endif()
  endif()

  if(CONFIG_AV2_ENCODER)
    add_library(avm_dsp_encoder OBJECT ${AVM_DSP_ENCODER_SOURCES})
    list(APPEND AVM_LIB_TARGETS avm_dsp_encoder)
    target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_dsp_encoder>)
    if(BUILD_SHARED_LIBS)
      target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_dsp_encoder>)
    endif()
  endif()

  if(HAVE_SSE2)
    add_asm_library("avm_dsp_common_sse2" "AVM_DSP_COMMON_ASM_SSE2")
    add_intrinsics_object_library("-msse2" "sse2" "avm_dsp_common"
                                  "AVM_DSP_COMMON_INTRIN_SSE2")

    if(CONFIG_AV2_ENCODER)
      if("${AVM_TARGET_CPU}" STREQUAL "x86_64")
        list(APPEND AVM_DSP_ENCODER_ASM_SSE2 ${AVM_DSP_ENCODER_ASM_SSE2_X86_64})
      endif()
      add_asm_library("avm_dsp_encoder_sse2" "AVM_DSP_ENCODER_ASM_SSE2")
      add_intrinsics_object_library("-msse2" "sse2" "avm_dsp_encoder"
                                    "AVM_DSP_ENCODER_INTRIN_SSE2")
    endif()
  endif()

  if(HAVE_SSSE3)
    add_asm_library("avm_dsp_common_ssse3" "AVM_DSP_COMMON_ASM_SSSE3")
    add_intrinsics_object_library("-mssse3" "ssse3" "avm_dsp_common"
                                  "AVM_DSP_COMMON_INTRIN_SSSE3")

    if(CONFIG_AV2_ENCODER)
      if("${AVM_TARGET_CPU}" STREQUAL "x86_64")
        list(APPEND AVM_DSP_ENCODER_ASM_SSSE3
             ${AVM_DSP_ENCODER_ASM_SSSE3_X86_64})
      endif()
      add_asm_library("avm_dsp_encoder_ssse3" "AVM_DSP_ENCODER_ASM_SSSE3")
      add_intrinsics_object_library("-mssse3" "ssse3" "avm_dsp_encoder"
                                    "AVM_DSP_ENCODER_INTRIN_SSSE3")
    endif()
  endif()

  if(HAVE_SSE4_1)
    add_intrinsics_object_library("-msse4.1" "sse4_1" "avm_dsp_common"
                                  "AVM_DSP_COMMON_INTRIN_SSE4_1")
    if(CONFIG_AV2_ENCODER)
      add_intrinsics_object_library("-msse4.1" "sse4_1" "avm_dsp_encoder"
                                    "AVM_DSP_ENCODER_INTRIN_SSE4_1")
    endif()
  endif()

  if(HAVE_AVX)
    if(CONFIG_AV2_ENCODER)
      add_intrinsics_object_library("-mavx" "avx" "avm_dsp_encoder"
                                    "AVM_DSP_ENCODER_INTRIN_AVX")
    endif()
  endif()

  if(HAVE_AVX2)
    add_intrinsics_object_library("-mavx2" "avx2" "avm_dsp_common"
                                  "AVM_DSP_COMMON_INTRIN_AVX2")
    if(CONFIG_AV2_DECODER)
      add_intrinsics_object_library("-mavx2" "avx2" "avm_dsp_decoder"
                                    "AVM_DSP_DECODER_INTRIN_AVX2")
    endif()
    if(CONFIG_AV2_ENCODER)
      add_intrinsics_object_library("-mavx2" "avx2" "avm_dsp_encoder"
                                    "AVM_DSP_ENCODER_INTRIN_AVX2")
    endif()
  endif()

  if(HAVE_NEON)
    add_intrinsics_object_library("${AVM_NEON_INTRIN_FLAG}" "neon"
                                  "avm_dsp_common" "AVM_DSP_COMMON_INTRIN_NEON")
    if(CONFIG_AV2_ENCODER)
      add_intrinsics_object_library(
        "${AVM_NEON_INTRIN_FLAG}" "neon" "avm_dsp_encoder"
        "AVM_DSP_ENCODER_INTRIN_NEON")
    endif()
  endif()

  if(HAVE_DSPR2)
    add_intrinsics_object_library("" "dspr2" "avm_dsp_common"
                                  "AVM_DSP_COMMON_INTRIN_DSPR2")
  endif()

  if(HAVE_MSA)
    add_intrinsics_object_library("" "msa" "avm_dsp_common"
                                  "AVM_DSP_COMMON_INTRIN_MSA")
    if(CONFIG_AV2_ENCODER)
      add_intrinsics_object_library("" "msa" "avm_dsp_encoder"
                                    "AVM_DSP_ENCODER_INTRIN_MSA")
    endif()
  endif()

  target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_dsp>)
  if(BUILD_SHARED_LIBS)
    target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_dsp>)
  endif()

  # Pass the new lib targets up to the parent scope instance of
  # $AVM_LIB_TARGETS.
  set(AVM_LIB_TARGETS
      ${AVM_LIB_TARGETS}
      PARENT_SCOPE)
endfunction()
