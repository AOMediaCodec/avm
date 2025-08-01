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

set_aom_config_var(CONFIG_ENABLE_IBC_NAT 1 "Enables IBC for natural content")
set_aom_config_var(CONFIG_ENABLE_INLOOP_FILTER_GIBC 1
                   "Enables In-loop filters for key-frames with GIBC enabled")
set_aom_config_var(CONFIG_IBC_SUBPEL_PRECISION 1
                   "Allow sub-pel precision for intraBC block vector.")

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

set_aom_config_var(CONFIG_PARAKIT_COLLECT_DATA 0
                   "enables data collection for ParaKit training.")

set_aom_config_var(CONFIG_CTX_V_AC_SIGN 1 "FG8-Test1.")
set_aom_config_var(CONFIG_CTX_MV_SHELL_OFFSET_OTHER 1 "FG8-Test3.")

# AV2 experiment flags.
set_aom_config_var(
  CONFIG_WIENERNS_9x9 1
  "AV2 non-separable 16-tap Wiener filter with enlarged 9x9 diamond shape")
set_aom_config_var(CONFIG_COEFF_HR_LR1 1
                   "AV2 enable coding 1 LR in coefficient coding.")
set_aom_config_var(CONFIG_PARA_BD_REDUCE 1 "AV2 bitdepth reduction for PARA.")
set_aom_config_var(CONFIG_BYPASS_IMPROVEMENT 1
                   "AV2 enable entropy bypass improvement.")
set_aom_config_var(CONFIG_CDF_SCALE 1
                   "AV2 enable entropy cdf scaling improvement.")
set_aom_config_var(CONFIG_EOB_POS_LUMA 1 "EOB position coding for luma.")
set_aom_config_var(CONFIG_INTRA_SDP_SIMPLIFICATION 1 NUMBER
                   "Simplify intra sdp logic")
set_aom_config_var(CONFIG_DIP_EXT_PRUNING 1 "AV2 DIP TFLite pruning.")
set_aom_config_var(CONFIG_ERP_TFLITE 0 NUMBER "Build ERP with TFLite")
set_aom_config_var(CONFIG_TCQ_FOR_ALL_FRAMES 1 "Adjust base QP for TCQ")
set_aom_config_var(CONFIG_DISABLE_4X4_INTER 1 "Disable 4x4 inter blocks")

set_aom_config_var(
  CONFIG_16_FULL_SEARCH_DMVR
  1
  "AV2 remove 2-stage search and early termination in neighbors for DMVR with 16-neighbor full search"
)

set_aom_config_var(CONFIG_24_FULL_SEARCH_DMVR 1
                   "AV2 DMVR with 24-neighbor full search")

set_aom_config_var(CONFIG_DMVR_OFF_IN_TIP_DIRECT 1
                   "Disable DMVR in TIP-direct mode")

set_aom_config_var(CONFIG_ORIP_DC_DISABLED 0
                   "AV2 experiment flag to disable ORIP for DC mode.")
set_aom_config_var(
  CONFIG_CONTEXT_DERIVATION 1
  "AV2 experiment flag to enable modified context derivation : CWG-B065.")
# End: CWG-C016
set_aom_config_var(
  CONFIG_COMBINE_PC_NS_WIENER 1 NUMBER
  "AV2 pixel-classified, frame-level, nonsep Wiener filter experiment flag")

set_aom_config_var(
  CONFIG_TEMP_LR 1
  "AV2 experiment flag to use temporal LR when frame-level filter is on")

set_aom_config_var(
  CONFIG_COMBINE_PC_NS_WIENER_ADD 1 NUMBER
  "Frame-level, nonsep Wiener filter for chroma experiment flag")

set_aom_config_var(
  CONFIG_RETRAIN_PC_WIENER 1 NUMBER
  "Precision Adjustment and Retraining for RESTORE_PC_WIENER flag")

set_aom_config_var(
  CONFIG_IMPROVED_DS_CC_WIENER 1
  "AV2 improved luma downsampling for cross-plane non-sep wiener filter")

set_aom_config_var(
  CONFIG_REMOVE_SIX_TAP_DS_CROSS_LR
  1
  "Replace the six-tap luma downsampling filter with a two-tap filter for cross-plane non-sep wiener filter"
)

# CWG-D178
set_aom_config_var(CONFIG_LOSSLESS_DPCM 1
                   "AV2 enable DPCM and FSC for lossless coding mode")
set_aom_config_var(
  CONFIG_IMPROVE_LOSSLESS_TXM
  1
  "AV2 enable 4x4 IDTX for inter blocks and 8x8 IDTX for all blocks in lossless mode (luma only)"
)
set_aom_config_var(CONFIG_LOSSLESS_CHROMA_IDTX 1
                   "AV2 enable IDTX for chroma blocks in lossless mode")
set_aom_config_var(
  CONFIG_ACROSS_SCALE_TPL_MVS 0 NUMBER
  "AV2 experiment flag to enable across scale temporal mv projection")

set_aom_config_var(CONFIG_ACROSS_SCALE_WARP 1 NUMBER
                   "AV2 experiment flag to enable across scale warp modes.")

set_aom_config_var(CONFIG_ACROSS_SCALE_REFINEMV 0 NUMBER
                   "AV2 experiment flag to enable across scale refinemv modes.")

# CWG-E210
set_aom_config_var(CONFIG_RECT_CTX 1
                   "AV2 enable Context Reduction for Rectangle Partition Type")

# Source of throughput analysis : CWG-B065
set_aom_config_var(CONFIG_THROUGHPUT_ANALYSIS 0
                   "AV2 experiment flag to measure throughput.")
set_aom_config_var(CONFIG_IBC_SR_EXT 2 "Enables IntraBC search range extension")
set_aom_config_var(
  CONFIG_WARP_EXTEND_SIMPLIFICATION 1
  "Reduce the number of neighbors used in warp extend mode to four")
set_aom_config_var(CONFIG_IBC_BV_IMPROVEMENT 1
                   "Enables BV improvements for IBC")
set_aom_config_var(CONFIG_IBC_MAX_DRL 1
                   "Enables Max DRL index signaling for IntraBC")
set_aom_config_var(
  CONFIG_PALETTE_IMPROVEMENTS
  1
  "AV2 experiment flag for palette parsing independency and improved palette color map coding."
)
set_aom_config_var(
  CONFIG_PALETTE_LINE_COPY
  1
  "AV2 experiment flag to enable palette line copy and transverse coding, CWG-D114."
)
set_aom_config_var(CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT 1
                   "Enable enhanced frame output order derivation")
set_aom_config_var(CONFIG_REF_LIST_DERIVATION_FOR_TEMPORAL_SCALABILITY 1
                   "Enable temporal scalability")
set_aom_config_var(CONFIG_DISPLAY_ORDER_HINT_FIX 1
                   "Bug fix on display order hints of key frames")

set_aom_config_var(CONFIG_BAWP_ACROSS_SCALES 0 NUMBER
                   "Enable BAWP across scales prediction")
set_aom_config_var(CONFIG_REFINEMV 1 "Enable refinemv modes")

set_aom_config_var(CONFIG_EXPLICIT_TEMPORAL_DIST_CALC 1
                   "Enable to explicit temporal distance calculation")
set_aom_config_var(CONFIG_IMPROVED_GLOBAL_MOTION 1
                   "New global motion syntax for AV2")

# CWG-F082: Extended DPB mode for AV2 in RTC
set_aom_config_var(CONFIG_EXTRA_DPB 1 "Use extra dpb")

set_aom_config_var(CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING 1
                   "TX partition type signalling bugfix")
set_aom_config_var(CONFIG_INTERINTRA_IMPROVEMENT 1
                   "Enable additional inter-intra block sizes")
set_aom_config_var(CONFIG_REFRESH_FLAG 0
                   "Experiment flag to Signal refresh frame flag with 3 bits")

set_aom_config_var(CONFIG_D149_CTX_MODELING_OPT 1
                   "Enable to optimize block size dependent context modeling")
set_aom_config_var(
  CONFIG_COMPOUND_WARP_CAUSAL 1
  "AV2 experiment flag to enable compound new_newmv warp_causal mode")
set_aom_config_var(CONFIG_CWG_193_WARP_CAUSAL_THRESHOLD_REMOVAL 1
                   "Removes threshold based selection stratergy in warp causal")
set_aom_config_var(CONFIG_BANK_IMPROVE 1
                   "Enable to improve refmv bank and warp parameter bank")
set_aom_config_var(CONFIG_PARTITION_CONTEXT_REDUCE 1
                   "Enable to reduce partition contexts")
set_aom_config_var(CONFIG_OPT_INTER_MODE_CTX 1
                   "Improvement of all inter mode related contexts")
set_aom_config_var(CONFIG_REDUCE_CCTX_CTX 1 "Reduce CCTX contexts")

set_aom_config_var(CONFIG_WRL_PRUNE_FOUR_PARAMETERS 1
                   "Enable WRL only prune four non-translational parameters")

set_aom_config_var(CONFIG_OPFL_CTX_OPT 1
                   "Enable optimization of the CDFs for use optflow flag")

set_aom_config_var(CONFIG_MVD_CDF_REDUCTION 1
                   "Enable reduction of the CDFs for MVD related sybmols")
set_aom_config_var(CONFIG_LOCAL_INTRABC_ALIGN_RNG 1
                   "CWG F177 align inter/intra local intraBC search range")
set_aom_config_var(CONFIG_GLOBAL_INTRABC_DELAY_OPT 1
                   "Enable alignment of global intraBC boundary")

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

set_aom_config_var(CONFIG_WRL_CORNER_MVS 1
                   "Improve the WRL from the 3 corner MVs")

set_aom_config_var(CONFIG_IMPROVED_CFL 1
                   "Enable improved CfL mode from CWG-C044")
set_aom_config_var(
  CONFIG_CFL_64x64 1
  "Enable cross-component prediction modes up to luma size 64x64")
set_aom_config_var(CONFIG_CFL_SIMPLIFICATION 1 "Fix issues of CfL")

set_aom_config_var(CONFIG_BLEND_MODE 1
                   "Enable improved intra blend mode from CWG-D046")
set_aom_config_var(CONFIG_BLENDING_SIMPLIFICATION 1
                   "CWG F137 simplification for blending mode")

set_aom_config_var(CONFIG_C076_INTER_MOD_CTX 1
                   "AV2 experiment flag to simplify inter mode contexts")

set_aom_config_var(CONFIG_NEW_CONTEXT_MODELING 1
                   "Enable to improve the context modeling")
set_aom_config_var(CONFIG_WEDGE_MOD_EXT 1 "AV2 wedge modes extensions.")

set_aom_config_var(CONFIG_BRU 1 "enable BRU update")
set_aom_config_var(CONFIG_SDP_CFL_LATENCY_FIX 1 "Reduce SDP-CFL latency")
set_aom_config_var(CONFIG_CHROMA_MERGE_LATENCY_FIX 1
                   "Fix the latency issue in chroma merge region")

set_aom_config_var(CONFIG_COMPOUND_4XN 1
                   "Enable compound modes for 4XN/Nx4 blocks")

set_aom_config_var(CONFIG_LC_REF_MV_BANK 1
                   "Enable low complexity refmv bank design")

set_aom_config_var(
  CONFIG_FRAME_HEADER_SIGNAL_OPT 1
  "Enable the signaling optimization for certain frame header syntax elements")

set_aom_config_var(CONFIG_TIP_LD 1 "Enable TIP for low delay")
set_aom_config_var(CONFIG_TIP_ENHANCEMENT 1
                   "Enable different weighted prediction for TIP")

set_aom_config_var(CONFIG_TIP_INTERP_SMOOTH 1
                   "Enable Smooth interpolation for TIP direct output mode")

set_aom_config_var(CONFIG_PALETTE_THREE_NEIGHBOR 1
                   "Derive palette context using three neighbors")
set_aom_config_var(CONFIG_PALETTE_CTX_REDUCTION 1
                   "Context reductions to Palette mode related syntax")
set_aom_config_var(CONFIG_WRL_NO_PRUNING 1 "No pruning when construct WRL")

# This is an encode-only change.

set_aom_config_var(CONFIG_FIX_RESIZE_PSNR 1
                   "Fix PSNR computation of resize mode in coded resolution")

set_aom_config_var(CONFIG_MRSSE 0 "Enable MRSSE")

set_aom_config_var(CONFIG_MAX_PB_RATIO 1
                   "Enable max partition block aspect ratio constraint")

set_aom_config_var(CONFIG_SCC_DETERMINATION 1
                   "Enable the screen content tools determination improvement.")

set_aom_config_var(CONFIG_F107_GRADIENT_SIMPLIFY 1
                   "Use smaller OPFL gradient units and remove bit checks.")
set_aom_config_var(CONFIG_LF_SUB_PU 1 "AV2 enable LF on sub blocks")
set_aom_config_var(CONFIG_ASYM_DF 1 "Enable asymmetric DF")
set_aom_config_var(CONFIG_DERIVED_MVD_SIGN 1 "Enable MVD sign derivations")
set_aom_config_var(CONFIG_VQ_MVD_CODING 1 "Enable VQ based MVD coding")
set_aom_config_var(CONFIG_SUBBLK_REF_EXT 1
                   "Enable extension for subblock MV refinement.")
set_aom_config_var(CONFIG_SUBBLK_REF_DS 1
                   "Enable sad downsampling for subblock MV refinement.")
set_aom_config_var(CONFIG_SUBBLK_PAD 1
                   "Enable subblock padding for subblock mv refinement.")

set_aom_config_var(CONFIG_ML_PART_SPLIT 1
                   "Partition SPLIT pruning/forcing as predicted by ML.")
set_aom_config_var(CONFIG_COEFF_HR_ADAPTIVE 1
                   "AV2 enable adaptive coding of HR coefficients.")
set_aom_config_var(CONFIG_MV_TRAJECTORY 1
                   "Enables TMVP MV trajectory tracking.")

set_aom_config_var(CONFIG_WEDGE_SIMPL 1 "Wedge mode simplificaitons.")

set_aom_config_var(CONFIG_WARP_INTER_INTRA 1
                   "Enable inter-intra mode for warp block.")

set_aom_config_var(CONFIG_ADAPT_OPFL_IN_TIP_DIRECT 1
                   "Enable adaptive OPFL MV refinement in TIP-direct mode")

set_aom_config_var(CONFIG_NEW_PART_CTX 1 "New partition context models")

set_aom_config_var(CONFIG_EXT_SEG 1
                   "Extend the maximum number of segments to 16, CWG-F069.")
set_aom_config_var(
  CONFIG_F054_PIC_BOUNDARY
  1
  "Alignment of ref picture to be integer multiples of 8 for loop filter and inter prediction"
)
set_aom_config_var(CONFIG_GDF 1 "Enable guided detail filter.")

set_aom_config_var(CONFIG_REDUCE_SYMBOL_SIZE 1
                   "Symbol size reduction from 16 to 8.")

set_aom_config_var(CONFIG_DAMR_CLEAN_UP 1
                   "Clean up DAMR memory bandwith issue.")

set_aom_config_var(CONFIG_TMVP_MVS_WRITING_FLOW_OPT 1
                   "Enable to only write TMVP MVs once")

set_aom_config_var(CONFIG_IMPROVE_TIP_LF 1 "Enable improved LF for TIP frame.")
set_aom_config_var(CONFIG_ADJ_PYR_Q_OFFSET 0
                   "Encoder-only config to adjust pyr qp offsets from nominal.")
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
