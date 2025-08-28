#
# Copyright (c) 2021, Alliance for Open Media. All rights reserved
#
# This source code is subject to the terms of the BSD 3-Clause Clear License and
# the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
# License was not distributed with this source code in the LICENSE file, you can
# obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance
# for Open Media Patent License 1.0 was not distributed with this source code in
# the PATENTS file, you can obtain it at aomedia.org/license/patent-license/.

include("${AOM_ROOT}/build/cmake/util.cmake")

# This file sets default values for libaom configuration variables. All libaom
# config variables are added to the CMake variable cache via the macros provided
# in util.cmake.

#
# The variables in this section of the file are detected at configuration time,
# but can be overridden via the use of CONFIG_* and ENABLE_* values also defined
# in this file.
#

set_aom_detect_var(INLINE "" "Sets INLINE value for current target.")

# CPUs.
set_aom_detect_var(AOM_ARCH_AARCH64 0 "Enables AArch64 architecture.")
set_aom_detect_var(ARCH_ARM 0 "Enables ARM architecture.")
set_aom_detect_var(ARCH_MIPS 0 "Enables MIPS architecture.")
set_aom_detect_var(ARCH_PPC 0 "Enables PPC architecture.")
set_aom_detect_var(ARCH_X86 0 "Enables X86 architecture.")
set_aom_detect_var(ARCH_X86_64 0 "Enables X86_64 architecture.")

# ARM feature flags.
set_aom_detect_var(HAVE_NEON 0 "Enables NEON intrinsics optimizations.")

# MIPS feature flags.
set_aom_detect_var(HAVE_DSPR2 0 "Enables DSPR2 optimizations.")
set_aom_detect_var(HAVE_MIPS32 0 "Enables MIPS32 optimizations.")
set_aom_detect_var(HAVE_MIPS64 0 "Enables MIPS64 optimizations. ")
set_aom_detect_var(HAVE_MSA 0 "Enables MSA optimizations.")

# PPC feature flags.
set_aom_detect_var(HAVE_VSX 0 "Enables VSX optimizations.")

# x86/x86_64 feature flags.
set_aom_detect_var(HAVE_AVX 0 "Enables AVX optimizations.")
set_aom_detect_var(HAVE_AVX2 0 "Enables AVX2 optimizations.")
set_aom_detect_var(HAVE_MMX 0 "Enables MMX optimizations. ")
set_aom_detect_var(HAVE_SSE 0 "Enables SSE optimizations.")
set_aom_detect_var(HAVE_SSE2 0 "Enables SSE2 optimizations.")
set_aom_detect_var(HAVE_SSE3 0 "Enables SSE3 optimizations.")
set_aom_detect_var(HAVE_SSE4_1 0 "Enables SSE 4.1 optimizations.")
set_aom_detect_var(HAVE_SSE4_2 0 "Enables SSE 4.2 optimizations.")
set_aom_detect_var(HAVE_SSSE3 0 "Enables SSSE3 optimizations.")

# Flags describing the build environment.
set_aom_detect_var(HAVE_FEXCEPT 0
                   "Internal flag, GNU fenv.h present for target.")
set_aom_detect_var(HAVE_PTHREAD_H 0 "Internal flag, target pthread support.")
set_aom_detect_var(HAVE_UNISTD_H 0
                   "Internal flag, unistd.h present for target.")
set_aom_detect_var(HAVE_WXWIDGETS 0 "WxWidgets present.")

#
# Variables in this section can be set from the CMake command line or from
# within the CMake GUI. The variables control libaom features.
#

# Build configuration flags.
set_aom_config_var(AOM_RTCD_FLAGS ""
                   "Arguments to pass to rtcd.pl. Separate with ';'")
set_aom_config_var(CONFIG_AV1_DECODER 1 "Enable AV1 decoder.")
set_aom_config_var(CONFIG_AV1_ENCODER 1 "Enable AV1 encoder.")
set_aom_config_var(CONFIG_BIG_ENDIAN 0 "Internal flag.")
set_aom_config_var(CONFIG_GCC 0 "Building with GCC (detect).")
set_aom_config_var(CONFIG_GCOV 0 "Enable gcov support.")
set_aom_config_var(CONFIG_GPROF 0 "Enable gprof support.")
set_aom_config_var(CONFIG_LIBYUV 1 "Enables libyuv scaling/conversion support.")
set_aom_config_var(CONFIG_LANCZOS_RESAMPLE 1 "Enables lanczos resize support.")

set_aom_config_var(CONFIG_MULTITHREAD 1 "Multithread support.")
set_aom_config_var(CONFIG_OS_SUPPORT 0 "Internal flag.")
set_aom_config_var(CONFIG_PIC 0 "Build with PIC enabled.")
set_aom_config_var(CONFIG_RUNTIME_CPU_DETECT 1 "Runtime CPU detection support.")
set_aom_config_var(CONFIG_SHARED 0 "Build shared libs.")
set_aom_config_var(CONFIG_WEBM_IO 1 "Enables WebM support.")

# Debugging flags.
set_aom_config_var(CONFIG_DEBUG 0 "Enable debug-only code.")
set_aom_config_var(CONFIG_MISMATCH_DEBUG 0 "Mismatch debugging flag.")
set_aom_config_var(CONFIG_EXCLUDE_SIMD_MISMATCH 1
                   "Exclude mismatch in SIMD functions for testing/debugging.")

# AV1 feature flags.
set_aom_config_var(CONFIG_ACCOUNTING 0 "Enables bit accounting.")
set_aom_config_var(CONFIG_ANALYZER 0 "Enables bit stream analyzer.")
set_aom_config_var(CONFIG_EXTRACT_PROTO 0
                   "Enables protobuf-based inspection tool.")
set_aom_config_var(CONFIG_COEFFICIENT_RANGE_CHECKING 0
                   "Coefficient range check.")
set_aom_config_var(CONFIG_DENOISE 1
                   "Denoise/noise modeling support in encoder.")
set_aom_config_var(CONFIG_INSPECTION 0 "Enables bitstream inspection.")
set_aom_config_var(CONFIG_INTERNAL_STATS 0 "Enables internal encoder stats.")
set_aom_config_var(CONFIG_MAX_DECODE_PROFILE 2
                   "Max profile to support decoding.")
set_aom_config_var(CONFIG_NORMAL_TILE_MODE 0 "Only enables normal tile mode.")
set_aom_config_var(CONFIG_SIZE_LIMIT 0 "Limit max decode width/height.")
set_aom_config_var(CONFIG_SPATIAL_RESAMPLING 1 "Spatial resampling.")
set_aom_config_var(DECODE_HEIGHT_LIMIT 0 "Set limit for decode height.")
set_aom_config_var(DECODE_WIDTH_LIMIT 0 "Set limit for decode width.")
set_aom_config_var(CONFIG_TUNE_VMAF 0 "Enable encoding tuning for VMAF.")
set_aom_config_var(CONFIG_USE_VMAF_RC 0 "Use libvmaf_rc tune for VMAF_NEG.")

# AV1 experiment flags.
set_aom_config_var(CONFIG_SPEED_STATS 0 "AV1 experiment flag.")
set_aom_config_var(CONFIG_COLLECT_RD_STATS 0 "AV1 experiment flag.")
set_aom_config_var(CONFIG_DIST_8X8 0 "AV1 experiment flag.")
set_aom_config_var(CONFIG_ENTROPY_STATS 0 "AV1 experiment flag.")
set_aom_config_var(CONFIG_INTER_STATS_ONLY 0 "AV1 experiment flag.")
set_aom_config_var(CONFIG_BITSTREAM_DEBUG 0
                   "AV1 experiment flag for bitstream debugging.")
set_aom_config_var(CONFIG_RD_DEBUG 0 "AV1 experiment flag.")
set_aom_config_var(CONFIG_SHARP_SETTINGS 0 "AV1 experiment flag.")
set_aom_config_var(CONFIG_DISABLE_FULL_PIXEL_SPLIT_8X8 1
                   "Disable full_pixel_motion_search_based_split on BLOCK_8X8.")
set_aom_config_var(CONFIG_COLLECT_PARTITION_STATS 0
                   "Collect stats on partition decisions.")
set_aom_config_var(CONFIG_COLLECT_COMPONENT_TIMING 0
                   "Collect encoding component timing information.")
set_aom_config_var(CONFIG_AV1_TEMPORAL_DENOISING 0
                   "Build with temporal denoising support.")
set_aom_config_var(CONFIG_NN_V2 0 "Fully-connected neural nets ver.2.")

# Multilayer coding
set_aom_config_var(
  CONFIG_MULTILAYER_CORE 1
  "Core changes in the codebase for multilayer coding support.")

set_aom_config_var(CONFIG_MULTILAYER_CORE_HLS 1
                   "Dependency structure signaling in sequence header.")

# CWG-F221
set_aom_config_var(CONFIG_MULTILAYER_HLS 1 "HLS for multilayer coding.")

set_aom_config_var(CONFIG_PARAKIT_COLLECT_DATA 0
                   "enables data collection for ParaKit training.")

set_aom_config_var(CONFIG_EXT_FRAME_BUFFER_POOL 1 "Buffer pool extension.")

# FG8 context simplification tests
set_aom_config_var(CONFIG_EOB_PT_CTX_REDUCTION 1 "FG8-Test4.")
set_aom_config_var(CONFIG_COEFF_BR_LF_UV_BYPASS 1
                   "AV2 experiment flag to bypass UV LF syntax. FG8-Test9.")
set_aom_config_var(CONFIG_COEFF_BR_PH_BYPASS 1
                   "AV2 experiment flag to bypass BR PH syntax. FG8-Test10.")
set_aom_config_var(CONFIG_BY_PASS_V_SIGN 1 "FG8-Test11.")
set_aom_config_var(CONFIG_CTX_BYPASS_CB_DC_SIGN 1
                   "bypass coding FG8-Test8-chroma-only")

set_aom_config_var(CONFIG_PLT_DIR_CTX 1 "by-pass PLT dir coding. FG8-Test24")
set_aom_config_var(CONFIG_BYPASS_INTRABC_DRL_IDX 1 "bypass coding FG8-Test25")
set_aom_config_var(CONFIG_MERGE_PARA_CTX 1
                   "by-pass merge para coding. FG8-Test26")
set_aom_config_var(CONFIG_CTX_Y_SECOND_MODE 1 "bypass coding FG8-Test27")

# AV2 experiment flags.
set_aom_config_var(
  CONFIG_F106_OBU_TILEGROUP 1
  "Consolidate FRAME, FRAM_EHEADER, TILE_GROUP OBUs into TILE_GROUP OBU.")
set_aom_config_var(CONFIG_F106_OBU_SWITCH 1 "Use SWITCH_OBU.")
set_aom_config_var(CONFIG_F106_OBU_SEF 1 "Use SEF OBU.")
set_aom_config_var(CONFIG_F106_OBU_TIP 1 "Use TIP_OBU.")
set_aom_config_var(CONFIG_F253_REMOVE_OUTPUTFLAG 1
                   "Remove enable_frame_output_order.")
# CWG-E242
set_aom_config_var(CONFIG_CWG_E242_SIGNAL_TILE_INFO 1
                   "Signal tile information at sequence header.")

set_aom_config_var(CONFIG_CWG_E242_BITDEPTH 1 "Signal Bitdepth using a LUT.")

set_aom_config_var(CONFIG_SEQ_MAX_DRL_BITS 1
                   "AV2 sequence level max_drl_bits information")
set_aom_config_var(CONFIG_MINIMUM_LR_UNIT_SIZE_64x64 0
                   "Support minimum LR Unit size ")
set_aom_config_var(CONFIG_LR_FRAMEFILTERS_IN_HEADER 0
                   "AV2 expt to move frame filters to frame header.")
set_aom_config_var(CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES 1
                   "AV2 control to allow loopfilters across tiles or not")
set_aom_config_var(CONFIG_DIP_EXT_PRUNING 1 "AV2 DIP TFLite pruning.")

set_aom_config_var(CONFIG_CWG_F243_REMOVE_ENABLE_ORDER_HINT 1
                   "Remove enable order hint flag.")

# CWG-F349
set_aom_config_var(CONFIG_CWG_F349_SIGNAL_TILE_INFO 1
                   "Improved tile information at sequence header.")
# CWG-F215
set_aom_config_var(CWG_F215_CONFIG_REMOVE_FRAME_ID 1
                   "Remove frame_id signalling")
# CWG-E242 Chroma Format IDC
set_aom_config_var(CONFIG_CWG_E242_CHROMA_FORMAT_IDC 1 "Chroma format idc.")

set_aom_config_var(CONFIG_ACROSS_SCALE_REF_OPT 1 NUMBER
                   "Reference framework optimization based on resolutions.")

set_aom_config_var(CONFIG_ACROSS_SCALE_WARP 1 NUMBER
                   "AV2 experiment flag to enable across scale warp modes.")

set_aom_config_var(CONFIG_CWG_F298_REC11 1 NUMBER
                   "Scaling function improvements for film grain synthesis.")

# Source of throughput analysis : CWG-B065
set_aom_config_var(CONFIG_THROUGHPUT_ANALYSIS 0
                   "AV2 experiment flag to measure throughput.")
set_aom_config_var(
  CONFIG_WARP_EXTEND_SIMPLIFICATION 1
  "Reduce the number of neighbors used in warp extend mode to four")
set_aom_config_var(CONFIG_MULTI_FRAME_HEADER 1 "Enable multi-frame header.")
set_aom_config_var(CONFIG_CWG_E242_MFH_ID_UVLC 1
                   "Signaling multi-frame header ID in UVLC")
set_aom_config_var(CONFIG_REMOVAL_REDUNDANT_FRAME_HEADER 1
                   "Remove redundant frame header OBU from OBU types")
set_aom_config_var(CONFIG_MULTI_STREAM 1 "AV2 enable multi-streams.")

# Configurable DPB extension
set_aom_config_var(CONFIG_CWG_F168_DPB_HLS 1
                   "signaling changes for DPB extension")
set_aom_config_var(CONFIG_CWG_F260_REFRESH_FLAG 1
                   "refresh frame flag signaling in CWG-F260 method 2")

set_aom_config_var(CONFIG_TU64_TRAVERSED_ORDER 1
                   "Coding order of TU 64x64 traversed as 128x128 blocks")

set_aom_config_var(
  CONFIG_COMPOUND_WARP_CAUSAL 1
  "AV2 experiment flag to enable compound new_newmv warp_causal mode")
set_aom_config_var(CONFIG_WARP_CAUSAL_PARSING_DEPENDENCY_REDUCTION 1
                   "reduce warp causal parsing dependency")
set_aom_config_var(CONFIG_CWG_193_WARP_CAUSAL_THRESHOLD_REMOVAL 1
                   "Removes threshold based selection stratergy in warp causal")
set_aom_config_var(CONFIG_REDUCE_CCTX_CTX 1 "Reduce CCTX contexts")

set_aom_config_var(CONFIG_WRL_PRUNE_FOUR_PARAMETERS 1
                   "Enable WRL only prune four non-translational parameters")

set_aom_config_var(CONFIG_LOCAL_INTRABC_ALIGN_RNG 1
                   "CWG F177 align inter/intra local intraBC search range")
set_aom_config_var(CONFIG_GLOBAL_INTRABC_DELAY_OPT 1
                   "Enable alignment of global intraBC boundary")
set_aom_config_var(CONFIG_LOCAL_INTRABC_BAWP 1
                   "CWG-F165 Enable intraBAWP in local intraBC")

set_aom_config_var(CONFIG_REDUCED_REF_FRAME_MVS_MODE 1
                   "Use reduced reference frame mvs for temporal mv prediction")

set_aom_config_var(CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING 1
                   "Buffer removal timing OBU")

set_aom_config_var(
  CONFIG_QM_DEBUG 0
  "Enable debug information for extension to AV1 quantization matrices.")

set_aom_config_var(
  CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL 1
  "Enhanced parsing dependency removal and simplification of skip mode.")

set_aom_config_var(CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION 1
                   "Enable to reduce context model line buffer size")

set_aom_config_var(CONFIG_MV_RANGE_EXTENSION 1
                   "Enable to extend the range of MV")
set_aom_config_var(CONFIG_FRAME_HALF_PRECISION 1
                   "Enable frame level half precision")

set_aom_config_var(CONFIG_WRL_CORNER_MVS 1
                   "Improve the WRL from the 3 corner MVs")

set_aom_config_var(CONFIG_WARPMV_WARP_CAUSAL_REMOVAL 1
                   "Remove the WARP_CAUSAL in WARPMV")

set_aom_config_var(CONFIG_BLENDING_SIMPLIFICATION 1
                   "CWG F137 simplification for blending mode")

set_aom_config_var(CONFIG_MHCCP_BLK_SIZE 1
                   "Add block size constraint for MHCCP")

set_aom_config_var(CONFIG_CWG_F307_CFL_SEQ_FLAG 1 "Sequence level flag for CfL")

set_aom_config_var(CONFIG_RU_SIZE_RESTRICTION 1
                   "RU size shall be larger than or equal to sb size")

set_aom_config_var(CONFIG_WRL_NO_PRUNING 1 "No pruning when construct WRL")

set_aom_config_var(CONFIG_REDUCED_TX_SET_EXT 1
                   "Extension to reduced transform sets.")
set_aom_config_var(CONFIG_FIX_OPFL_AUTO 1
                   "Fix optical flow auto mode, option 3")

set_aom_config_var(
  CONFIG_MV_VALUE_CLIP 1
  "Clip MV value to ensure it is within the constrained range.")

set_aom_config_var(
  CONFIG_INTER_BAWP_CONSTRAINT 1
  "Inter BAWP template cannot outside the valid reference range")

# This is an encode-only change.
set_aom_config_var(CONFIG_FAST_INTER_RDO 1 "Fast inter mode selection")

set_aom_config_var(CONFIG_FIX_RESIZE_PSNR 1
                   "Fix PSNR computation of resize mode in coded resolution")

set_aom_config_var(CONFIG_SCC_DETERMINATION 1
                   "Enable the screen content tools determination improvement.")

set_aom_config_var(CONFIG_F107_GRADIENT_SIMPLIFY 1
                   "Use smaller OPFL gradient units and remove bit checks.")
set_aom_config_var(CONFIG_ASYM_DF 1 "Enable asymmetric DF")
set_aom_config_var(CONFIG_DERIVED_MVD_SIGN 1 "Enable MVD sign derivations")
set_aom_config_var(CONFIG_VQ_MVD_CODING 1 "Enable VQ based MVD coding")

set_aom_config_var(CONFIG_ML_PART_SPLIT 1
                   "Partition SPLIT pruning/forcing as predicted by ML.")

set_aom_config_var(CONFIG_NEW_PART_CTX 1 "New partition context models")

set_aom_config_var(CONFIG_EXT_SEG 1
                   "Extend the maximum number of segments to 16, CWG-F069.")

set_aom_config_var(CONFIG_GDF_IMPROVEMENT 1
                   "Enable guided detail filter improvement.")

set_aom_config_var(CONFIG_REDUCE_SYMBOL_SIZE 1
                   "Symbol size reduction from 16 to 8.")

set_aom_config_var(CONFIG_CCSO_CLEANUP 1
                   "Clean up CCSO quant_sz and signaling.")

set_aom_config_var(CONFIG_DRL_SIZE_LIMIT 1 "F206 limit drl size to six.")
set_aom_config_var(CONFIG_DRL_PR_LIM 1 "Limit the number of DRL pruning.")
set_aom_config_var(CONFIG_COEFF_PARSING 1
                   "Parsing dependency removal for coefficient related syntax.")

set_aom_config_var(CONFIG_FGS_BLOCK_SIZE 1 "Choice of FGS blocks size.")
set_aom_config_var(CONFIG_MOTION_MODE_FRAME_HEADERS_OPT 1
                   "Frame header optimzation of motion modes.")

# CWG-F243
set_aom_config_var(CONFIG_CWG_F243_ORDER_HINT_BITDEPTH 1
                   "Encoder-only reduce order hint bitdepth.")

set_aom_config_var(
  CONFIG_FSC_RES_HLS 1
  "add high level flag to switch fsc residual and regular residual.")

set_aom_config_var(CONFIG_ADAPTIVE_WEDGE_BOUNDARY 1
                   "Adaptive wedge boundary based on block size.")

set_aom_config_var(CONFIG_DF_DQP 0 "Adding DQP to the deblocking filter")
set_aom_config_var(CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS 1
                   "Disable loop filters for lossless segments")
set_aom_config_var(
  CONFIG_MIXED_LOSSLESS_ENCODE 0
  "Encoder only flag to configure encoder to enable mixed lossy/lossless coding"
)
set_aom_config_var(CONFIG_CWG_F317 1 "Bridge frame")

#
# Variables in this section control optional features of the build system.
#
set_aom_option_var(ENABLE_CCACHE "Enable ccache support." OFF)
set_aom_option_var(ENABLE_DECODE_PERF_TESTS "Enables decoder performance tests"
                   OFF)
set_aom_option_var(ENABLE_DISTCC "Enable distcc support." OFF)
set_aom_option_var(ENABLE_DOCS
                   "Enable documentation generation (doxygen required)." ON)
set_aom_option_var(ENABLE_ENCODE_PERF_TESTS "Enables encoder performance tests"
                   OFF)
set_aom_option_var(ENABLE_EXAMPLES "Enables build of example code." ON)
set_aom_option_var(ENABLE_GOMA "Enable goma support." OFF)
set_aom_option_var(
  ENABLE_IDE_TEST_HOSTING
  "Enables running tests within IDEs like Visual Studio and Xcode." OFF)
set_aom_option_var(ENABLE_NASM "Use nasm instead of yasm for x86 assembly." OFF)
set_aom_option_var(ENABLE_TESTDATA "Enables unit test data download targets."
                   ON)
set_aom_option_var(ENABLE_TESTS "Enables unit tests." ON)
set_aom_option_var(ENABLE_TOOLS "Enable applications in tools sub directory."
                   ON)
set_aom_option_var(ENABLE_WERROR "Converts warnings to errors at compile time."
                   OFF)

# ARM assembly/intrinsics flags.
set_aom_option_var(ENABLE_NEON "Enables NEON optimizations on ARM targets." ON)

# MIPS assembly/intrinsics flags.
set_aom_option_var(ENABLE_DSPR2 "Enables DSPR2 optimizations on MIPS targets."
                   OFF)
set_aom_option_var(ENABLE_MSA "Enables MSA optimizations on MIPS targets." OFF)

# VSX intrinsics flags.
set_aom_option_var(ENABLE_VSX "Enables VSX optimizations on PowerPC targets."
                   ON)

# x86/x86_64 assembly/intrinsics flags.
set_aom_option_var(ENABLE_MMX
                   "Enables MMX optimizations on x86/x86_64 targets." ON)
set_aom_option_var(ENABLE_SSE
                   "Enables SSE optimizations on x86/x86_64 targets." ON)
set_aom_option_var(ENABLE_SSE2
                   "Enables SSE2 optimizations on x86/x86_64 targets." ON)
set_aom_option_var(ENABLE_SSE3
                   "Enables SSE3 optimizations on x86/x86_64 targets." ON)
set_aom_option_var(ENABLE_SSSE3
                   "Enables SSSE3 optimizations on x86/x86_64 targets." ON)
set_aom_option_var(ENABLE_SSE4_1
                   "Enables SSE4_1 optimizations on x86/x86_64 targets." ON)
set_aom_option_var(ENABLE_SSE4_2
                   "Enables SSE4_2 optimizations on x86/x86_64 targets." ON)
set_aom_option_var(ENABLE_AVX
                   "Enables AVX optimizations on x86/x86_64 targets." ON)
set_aom_option_var(ENABLE_AVX2
                   "Enables AVX2 optimizations on x86/x86_64 targets." ON)
