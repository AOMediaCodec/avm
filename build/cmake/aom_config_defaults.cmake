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
set_aom_config_var(CONFIG_OPTICAL_FLOW_API 0
                   "AV1 experiment flag for optical flow API.")
set_aom_config_var(CONFIG_AV2CTC_PSNR_PEAK 1
                   "Use AV2 CTC type PSNR peak for 10- and 12-bit")
set_aom_config_var(CONFIG_ZERO_OFFSET_BITUPSHIFT 1
                   "Use zero offset for non-normative bit upshift")

set_aom_config_var(CONFIG_PARAKIT_COLLECT_DATA 0
                   "enables data collection for ParaKit training.")

set_aom_config_var(CONFIG_CTX_V_AC_SIGN 1 "FG8-Test1.")
set_aom_config_var(CONFIG_CTX_MV_SHELL_OFFSET_OTHER 1 "FG8-Test3.")

# AV2 experiment flags.
set_aom_config_var(
  CONFIG_WIENERNS_9x9 1
  "AV2 non-separable 16-tap Wiener filter with enlarged 9x9 diamond shape")
set_aom_config_var(CONFIG_IMPROVEIDTX 1
                   "AV2 enable improved identity transform coding.")
set_aom_config_var(CONFIG_COEFF_HR_LR1 1
                   "AV2 enable coding 1 LR in coefficient coding.")
set_aom_config_var(CONFIG_CHROMA_CODING 1
                   "AV2 enable low-complexity chroma coding.")
set_aom_config_var(CONFIG_ENTROPY_PARA 1 "AV2 enable PARA method for entropy.")
set_aom_config_var(CONFIG_PARA_BD_REDUCE 1 "AV2 bitdepth reduction for PARA.")
set_aom_config_var(CONFIG_BYPASS_IMPROVEMENT 1
                   "AV2 enable entropy bypass improvement.")
set_aom_config_var(CONFIG_CDF_SCALE 1
                   "AV2 enable entropy cdf scaling improvement.")
set_aom_config_var(CONFIG_EOB_POS_LUMA 1 "EOB position coding for luma.")
set_aom_config_var(
  CONFIG_EXT_RECUR_PARTITIONS 1 NUMBER
  "AV2 Fully recursive partitions including H partitions experiment flag")
set_aom_config_var(CONFIG_CB1TO4_SPLIT 1 NUMBER
                   "AV2 amended flexible partition experiment flag")
set_aom_config_var(CONFIG_INTRA_SDP_LATENCY_FIX 1 NUMBER
                   "AV2 intra SDP latency issue addressing flag")
set_aom_config_var(CONFIG_DIP 1 "AV2 intra data-driven prediction.")
set_aom_config_var(CONFIG_ERP_TFLITE 0 NUMBER "Build ERP with TFLite")
set_aom_config_var(CONFIG_TCQ 1 "AV2 trellis coded quantization flag")
set_aom_config_var(CONFIG_COMPOUND_WARP_SAMPLES 1 NUMBER
                   "AV2 compound warped motion samples experiment flag")
set_aom_config_var(CONFIG_RELAX_AFFINE_CONSTRAINTS 1
                   "AV2 relax affine constraints")
set_aom_config_var(CONFIG_NEW_TX_PARTITION 1
                   "AV2 new transform partitions experiment flag.")

set_aom_config_var(CONFIG_INTER_COMPOUND_BY_JOINT 1
                   "AV2 inter compound mode by joint.")
set_aom_config_var(CONFIG_NO_JOINTMODE_WHEN_SAME_REFINDEX 1
                   "AV2 no joint mode when same ref index.")
set_aom_config_var(CONFIG_DISABLE_4X4_INTER 1 "Disable 4x4 inter blocks")

set_aom_config_var(
  CONFIG_16_FULL_SEARCH_DMVR
  1
  "AV2 remove 2-stage search and early termination in neighbors for DMVR with 16-neighbor full search"
)

set_aom_config_var(CONFIG_24_FULL_SEARCH_DMVR 1
                   "AV2 DMVR with 24-neighbor full search")

set_aom_config_var(
  CONFIG_IDIF 1
  "AV2 experiment flag to enable Intra Directional Interpolation Filter.")
set_aom_config_var(CONFIG_ORIP_DC_DISABLED 0
                   "AV2 experiment flag to disable ORIP for DC mode.")
set_aom_config_var(CONFIG_ORIP_NONDC_DISABLED 0
                   "AV2 experiment flag to disable ORIP for non-DC modes.")
set_aom_config_var(CONFIG_MVP_IMPROVEMENT 1 "Enable MVP improvement")
set_aom_config_var(
  CONFIG_IBP_DC 1
  "AV2 experiment flag to enable intra bi-prediction for DC mode.")
set_aom_config_var(CONFIG_AIMC 1 "AV2 adaptive intra mode coding flag.")
set_aom_config_var(CONFIG_SIMPLIFIED_AIMC 1 "Simplification for AIMC.")
set_aom_config_var(CONFIG_WAIP 1 "AV2 wide angular intra prediction flag.")
set_aom_config_var(CONFIG_MORPH_PRED 1 "AV2 intra prediction mode flag.")
set_aom_config_var(CONFIG_IMPROVED_MORPH_PRED 1
                   "Improvement of AV2 linear intra prediction mode.")
set_aom_config_var(
  CONFIG_CONTEXT_DERIVATION 1
  "AV2 experiment flag to enable modified context derivation : CWG-B065.")

set_aom_config_var(CONFIG_EXTENDED_SDP 1
                   "Enable SDP for intra blocks in inter frame")
set_aom_config_var(
  CONFIG_EXTENDED_SDP_64x64 1
  "Enable SDP for intra blocks in inter frame up to 64x64 size")

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
  CONFIG_ACROSS_SCALE_TPL_MVS 0 NUMBER
  "AV2 experiment flag to enable across scale temporal mv projection")

set_aom_config_var(CONFIG_ACROSS_SCALE_WARP 0 NUMBER
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
set_aom_config_var(CONFIG_SKIP_MODE_SSE_BUG_FIX 1
                   "AV2 experiment flag to fix the SSE calc bug for skip mode.")
set_aom_config_var(CONFIG_SKIP_MODE_ENHANCEMENT 1
                   "AV2 experiment flag to enable skip mode enhancement.")
set_aom_config_var(CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT 1
                   "Enable enhanced frame output order derivation")
set_aom_config_var(CONFIG_OPTFLOW_ON_TIP 1
                   "Enable optical flow refinement on top of TIP")
set_aom_config_var(CONFIG_TIP_DIRECT_FRAME_MV 1
                   "Enable frame level MV for TIP direct mode")
set_aom_config_var(CONFIG_SAME_REF_COMPOUND 1
                   "Allow compound mode to refer to the same reference frame")
set_aom_config_var(CONFIG_DISPLAY_ORDER_HINT_FIX 1
                   "Bug fix on display order hints of key frames")
set_aom_config_var(CONFIG_BAWP 1 "Enable block adaptive weighted prediction")
set_aom_config_var(CONFIG_EXPLICIT_BAWP 1
                   "Explicit signaling for block adaptive weighted prediction")
set_aom_config_var(CONFIG_BAWP_CHROMA 1
                   "Enable block adaptive weighted prediction for Chroma")

set_aom_config_var(
  CONFIG_BAWP_FIX_DIVISION_16x16_MC
  1
  "Fix BAWP from CWG-E245, only use up to 16 samples from left and above and remove division using 8, 16 , or 32 samples"
)

set_aom_config_var(CONFIG_BAWP_ACROSS_SCALES_FIX 0 NUMBER
                   "Fix on BAWP across scales prediction")
set_aom_config_var(CONFIG_IMPROVED_INTRA_DIR_PRED 1 "Improved intra prediction")
set_aom_config_var(CONFIG_D071_IMP_MSK_BLD 1
                   "Enable single reference mode for frame boundary")

set_aom_config_var(CONFIG_SKIP_TXFM_OPT 1
                   "Enable to optimize the signaling of skip_txfm")
set_aom_config_var(CONFIG_REFINEMV 1 "Enable refinemv modes")

set_aom_config_var(CONFIG_EXPLICIT_TEMPORAL_DIST_CALC 1
                   "Enable to explicit temporal distance calculation")
set_aom_config_var(CONFIG_PRIMARY_REF_FRAME_OPT 1
                   "Enable the primary reference frame improvement")
set_aom_config_var(CONFIG_IMPROVED_GLOBAL_MOTION 1
                   "New global motion syntax for AV2")
set_aom_config_var(CONFIG_SEP_COMP_DRL 1
                   "Use separate drl list for compound modes")
set_aom_config_var(CONFIG_SKIP_ME_FOR_OPFL_MODES 1
                   "Reuse the mvs of compound mode from non-opfl path")

set_aom_config_var(CONFIG_E191_OFS_PRED_RES_HANDLE 1
                   "Enable outside frame boundary block handling")

set_aom_config_var(CONFIG_EXT_WARP_FILTER 1 "Enable extended warp filter")

set_aom_config_var(CONFIG_IMPROVE_EXT_WARP 1
                   "Enable ext warp filter for chroma blocks with bw=4 or bh=4")

set_aom_config_var(CONFIG_ADST_TUNED 1
                   "AV2 experiment to replace the ADST 4, 8 and 16 basis")

# CWG-E230: On core transform for AV2
set_aom_config_var(CONFIG_CORE_TX 1 "AV2 core transform")

set_aom_config_var(CONFIG_TX_PARTITION_CTX 1
                   "Enable to optimize txfm partition context")
set_aom_config_var(CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING 1
                   "TX partition type signalling bugfix")
set_aom_config_var(CONFIG_INTERINTRA_IMPROVEMENT 1
                   "Enable additional inter-intra block sizes")
set_aom_config_var(CONFIG_REFRESH_FLAG 0
                   "Experiment flag to Signal refresh frame flag with 3 bits")

set_aom_config_var(CONFIG_D149_CTX_MODELING_OPT 1
                   "Enable to optimize block size dependent context modeling")
set_aom_config_var(
  CONFIG_COMPOUND_WARP_CAUSAL 0
  "AV2 experiment flag to enable compound new_newmv warp_causal mode")
set_aom_config_var(CONFIG_DSMVP_REFBANK_MV_SWAP 1
                   "Swap ref bank mv and derived smvp in DRL generation")
set_aom_config_var(CONFIG_D072_SKIP_MODE_IMPROVE 1
                   "Enable to improve skip mode")

set_aom_config_var(CONFIG_SKIP_MODE_NO_REFINEMENTS 1
                   "Disable refinement algorithms for the skip mode")

set_aom_config_var(CONFIG_MF_HOLE_FILL_ALWAYS_ENABLE 1
                   "Always enable motion field hole filling")

set_aom_config_var(
  CONFIG_TX_TYPE_FLEX_IMPROVE 1
  "Enable transform type flexbility improvement for large transform blocks")

set_aom_config_var(CONFIG_TIP_DIRECT_MODE_SIGNALING 1
                   "Enable to optimize the signaling of TIP direct output mode")
set_aom_config_var(CONFIG_OPTIMIZE_CTX_TIP_WARP 1
                   "Optimize entropy contexts of TIP, warp extended, TIP DRL")
set_aom_config_var(CONFIG_CWG_E099_DRL_WRL_SIMPLIFY 1
                   "Enable the simplification of DRL and WRL")
set_aom_config_var(CONFIG_BANK_IMPROVE 1
                   "Enable to improve refmv bank and warp parameter bank")
set_aom_config_var(CONFIG_PARTITION_CONTEXT_REDUCE 1
                   "Enable to reduce partition contexts")
set_aom_config_var(CONFIG_CCSO_IMPROVE 1 "Enable CCSO improvements")

set_aom_config_var(CONFIG_OPT_INTER_MODE_CTX 1
                   "Improvement of all inter mode related contexts")

set_aom_config_var(CONFIG_WRL_PRUNE_FOUR_PARAMETERS 1
                   "Enable WRL only prune four non-translational parameters")

set_aom_config_var(CONFIG_KEY_OVERLAY 1
                   "Enable to support the key overlay frame")

set_aom_config_var(CONFIG_DRL_REORDER_CONTROL 1
                   "Enable to have a flag to turn on and off DRL reorder")

set_aom_config_var(CONFIG_CDEF_ENHANCEMENTS 1
                   "Enable the optimization of CDEF strengths")
set_aom_config_var(CONFIG_TMVP_SIMPLIFICATION 1
                   "Enable to reduce the number of TMVP candidates")

set_aom_config_var(
  CONFIG_MRLS_IMPROVE 1
  "Enable MRLS improvement to enable two reference lines for intra prediction")

set_aom_config_var(CONFIG_TMVP_MV_COMPRESSION 1 "Enable to compress TMVP MV")
set_aom_config_var(
  CONFIG_DRL_WRL_LINE_BUFFER_REDUCTION
  1
  "Reduce the line buffer size for DRL and WRL. The access unit is changed from 4x4 to 8x8"
)

# This is an encode-only change.
set_aom_config_var(CONFIG_MV_SEARCH_RANGE 1
                   "Enable a sufficient MV search range.")
# This is an encode-only change.
set_aom_config_var(CONFIG_LARGE_TF_BLOCK 1
                   "Enable large adaptively selected temporal filter blocks.")
set_aom_config_var(CONFIG_FIX_CDEF_SYNTAX 1
                   "AV2 experiment flag to fix CDEF syntax.")
set_aom_config_var(CONFIG_IMPROVED_CFL 1
                   "Enable improved CfL mode from CWG-C044")
set_aom_config_var(
  CONFIG_CFL_64x64 1
  "Enable cross-component prediction modes up to luma size 64x64")
set_aom_config_var(CONFIG_CFL_SIMPLIFICATION 1 "Fix issues of CfL")

set_aom_config_var(CONFIG_BLEND_MODE 1
                   "Enable improved intra blend mode from CWG-D046")
set_aom_config_var(CONFIG_ENABLE_MHCCP 1
                   "Enable multi hypothesis cross component prediction")
set_aom_config_var(CONFIG_E125_MHCCP_SIMPLIFY 1
                   "Simplify the parameter derivation for MHCCP")
set_aom_config_var(CONFIG_MHCCP_CONVOLVE_SIMPLIFY 1
                   "Simplify the convolve for MHCCP")
set_aom_config_var(CONFIG_CCSO_FT_SHAPE 1
                   "Change CCSO filter shape to meet hardware requirement")

set_aom_config_var(CONFIG_E149_MHCCP_4PARA 1 "Using 4 parameters in MHCCP")
set_aom_config_var(
  CONFIG_MHCCP_SB_BOUNDARY 1
  "Using only 1 lines when MHCCP block at the superblock top boundary")

set_aom_config_var(MHCCP_BUFFER_4LINES 1
                   "Using 2 lines for the chroma reference region in MHCCP")

set_aom_config_var(CONFIG_MHCCP_GAUSSIAN 1
                   "Fix gaussian elimination precision for MHCCP")

set_aom_config_var(CONFIG_C071_SUBBLK_WARPMV 1
                   "AV2 experiment flag to use subblock warp MV for SMVP")

set_aom_config_var(CONFIG_C076_INTER_MOD_CTX 1
                   "AV2 experiment flag to simplify inter mode contexts")

set_aom_config_var(CONFIG_NEW_CONTEXT_MODELING 1
                   "Enable to improve the context modeling")
set_aom_config_var(CONFIG_WEDGE_MOD_EXT 1 "AV2 wedge modes extensions.")

set_aom_config_var(CONFIG_INTER_DDT 1
                   "Use data driven transform to replace ADST for inter.")

set_aom_config_var(CONFIG_MF_IMPROVEMENT 1
                   "Enable to improve temporal motion projection")

set_aom_config_var(CONFIG_MVP_SIMPLIFY 1
                   "Enable to simplify MVP list construction")

# IST for sub-TU partitions from CWG-E151
set_aom_config_var(CONFIG_IST_NON_ZERO_DEPTH 1
                   "Enable IST non zero depth TUs from CWG-E151.")

# IST set reduction (non-normative)
set_aom_config_var(
  CONFIG_IST_REDUCTION 1
  "Non-normatively use 4 sets for IST encoder search from CWG-E142.")

set_aom_config_var(CONFIG_TMVP_IMPROVE 1
                   "Enable to improve TMVP candidate selection in DRL list")
set_aom_config_var(CONFIG_COMPOUND_4XN 1
                   "Enable compound modes for 4XN/Nx4 blocks")

set_aom_config_var(CONFIG_TILE_CDFS_AVG_TO_FRAME 1
                   "Average the CDFs from tiles for frame CDF")
set_aom_config_var(CONFIG_LC_REF_MV_BANK 1
                   "Enable low complexity refmv bank design")

set_aom_config_var(
  CONFIG_FRAME_HEADER_SIGNAL_OPT 1
  "Enable the signaling optimization for certain frame header syntax elements")

set_aom_config_var(CONFIG_TIP_LD 1 "Enable TIP for low delay")

set_aom_config_var(CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW 1
                   "Enable the redesign of warp modes signaling flow")
set_aom_config_var(CONFIG_PALETTE_THREE_NEIGHBOR 1
                   "Derive palette context using three neighbors")

# This is an encode-only change.
set_aom_config_var(CONFIG_MOTION_MODE_RD_PRUNE 1
                   "Enable fast motion mode search")

set_aom_config_var(CONFIG_OPFL_MV_SEARCH 1 "Optical flow based MV search")

set_aom_config_var(CONFIG_MRSSE 0 "Enable MRSSE")

set_aom_config_var(CONFIG_IST_SET_FLAG 1
                   "AV2 experiment flag to signal Secondary Tx set ID.")

set_aom_config_var(CONFIG_SCC_DETERMINATION 1
                   "Enable the screen content tools determination improvement.")

set_aom_config_var(CONFIG_IST_ANY_SET 1
                   "Enable R-D Optimized IST set selection from CWG-D159.")

set_aom_config_var(CONFIG_D143_CCSO_FM_FLAG 1
                   "Enable CCSO frame level joint flag.")

set_aom_config_var(CONFIG_CCSO_BO_REDUCE 1 "Reduce band only 128 case.")

set_aom_config_var(
  CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX 1
  "CWG-D086: Context modeling for transform block zero flag signaling")

set_aom_config_var(
  CONFIG_TIP_IMPLICIT_QUANT 1
  "Enable implicit quantization derivation for TIP direct mode")
set_aom_config_var(CONFIG_REFINED_MVS_IN_TMVP 1
                   "Keep optical flow refined MVs in TMVP list.")
set_aom_config_var(CONFIG_IMPROVE_REFINED_MV 1
                   "Keep refined MVs in TMVP list for TIP modes.")
set_aom_config_var(CONFIG_AFFINE_REFINEMENT 1
                   "Decoder side affine motion refinement.")
set_aom_config_var(CONFIG_AFFINE_REFINEMENT_SB 1
                   "Subblock based affine refinement")
set_aom_config_var(CONFIG_LF_SUB_PU 1 "AV2 enable LF on sub blocks")
set_aom_config_var(CONFIG_DERIVED_MVD_SIGN 1 "Enable MVD sign derivations")
set_aom_config_var(CONFIG_VQ_MVD_CODING 1 "Enable VQ based MVD coding")
set_aom_config_var(CONFIG_QM_SIMPLIFY 1
                   "Enable new QMs based on 8x8, 8x4, and 4x8 sizes.")
set_aom_config_var(CONFIG_OPFL_BI 1 "Enable bilinear for initial opfl.")
set_aom_config_var(CONFIG_CCSO_SIGFIX 1 "Enable signaling fix for CCSO.")

set_aom_config_var(CONFIG_SUBBLK_REF_EXT 1
                   "Enable extension for subblock MV refinement.")
set_aom_config_var(CONFIG_SUBBLK_REF_DS 1
                   "Enable sad downsampling for subblock MV refinement.")
set_aom_config_var(CONFIG_CHROMA_TX 1
                   "Sequence-level flag to use only DCT for chroma.")
set_aom_config_var(CONFIG_SUBBLK_PAD 1
                   "Enable subblock padding for subblock mv refinement.")

set_aom_config_var(CONFIG_ML_PART_SPLIT 1
                   "Partition SPLIT pruning/forcing as predicted by ML.")
set_aom_config_var(
  CONFIG_INTRA_TX_IST_PARSE 1
  "Parsing dependency removal for intra tx type and IST set signaling.")
set_aom_config_var(CONFIG_E194_FLEX_SECTX 1
                   "Secondary transforms with flexible support regions for 8x8")
set_aom_config_var(CONFIG_F105_IST_MEM_REDUCE 1
                   "Prune ADST_ADST IST sets for 8x8 intra blocks")
set_aom_config_var(CONFIG_E124_IST_REDUCE_METHOD1 0 "AV2 IST reduction.")
set_aom_config_var(CONFIG_E124_IST_REDUCE_METHOD4 1
                   "AV2 remove worst-case multiplications.")
set_aom_config_var(CONFIG_COEFF_HR_ADAPTIVE 1
                   "AV2 enable adaptive coding of HR coefficients.")
set_aom_config_var(CONFIG_IBP_WEIGHT 1 "Reduce IBP weights memory.")
set_aom_config_var(CONFIG_FIX_IBP_DC 1 "Simplify prediction of IBP DC mode.")
set_aom_config_var(CONFIG_WEDGE_TMVP 1
                   "Improvements to tmvp MV storing for wedge mode.")
set_aom_config_var(
  CONFIG_TMVP_MEM_OPT 1
  "Enables directional storage and adaptive sampling for TMVP MVs.")
set_aom_config_var(CONFIG_MV_TRAJECTORY 1
                   "Enables TMVP MV trajectory tracking.")
set_aom_config_var(CONFIG_ADAPTATION_RATE_IMPROVE 1
                   "Enables adaptation rate improvement with CDF propagation.")
set_aom_config_var(
  CONFIG_ENHANCED_FRAME_CONTEXT_INIT 1
  "Enables improved frame context initialization with frame averaging.")

set_aom_config_var(
  CONFIG_PRIMARY_QP_FIRST 1
  "Improvements to the primary reference frame by selecting the closet QP.")

set_aom_config_var(
  CONFIG_IMPROVED_SECONDARY_REFERENCE
  1
  "Enables improved secondary reference frame derivation for frame context initialization."
)

set_aom_config_var(CONFIG_FIX_INTER_DDT_PRECISION 1
                   "Fix precision of inter DDT.")
set_aom_config_var(
  CONFIG_ALIGN_DEBLOCK_ERP_SDP 1
  "Align deblocking filter boundaries with AVM partitioning scheme.")

set_aom_config_var(CONFIG_WEDGE_SIMPL 1 "Wedge mode simplificaitons.")
set_aom_config_var(CONFIG_WARP_PRECISION 1 "Enable precisions of warp models.")
set_aom_config_var(CONFIG_SIX_PARAM_WARP_DELTA 1
                   "Enable six parameter warp models.")
set_aom_config_var(
  CONFIG_REORDER_SIX_PARAM_DELTA 1
  "Enable to six parameter warp models signal when warp_ref_idx is 1.")

set_aom_config_var(CONFIG_WARP_INTER_INTRA 1
                   "Enable inter-intra mode for warp block.")

set_aom_config_var(CONFIG_OPFL_MEMBW_REDUCTION 1
                   "Reduce memory bandwith for OPFL/subblk ref/DAMR to 15x15.")

set_aom_config_var(CONFIG_WARP_BD_BOX 1 "4x4 warp constraints.")

set_aom_config_var(CONFIG_DISABLE_4X4_IBP_ORIP 1 "Disable 4x4 for IBP/ORIP.")
set_aom_config_var(CONFIG_DF_PAR_BITS 1
                   "Flexible control of deblocking parameter bits.")

set_aom_config_var(CONFIG_NEW_PART_CTX 1 "New partition context models")

set_aom_config_var(CONFIG_EXT_SEG 1
                   "Extend the maximum number of segments to 16, CWG-F069.")

set_aom_config_var(CONFIG_DELTAQ_OPT 1
                   "Enable delta-q entropy coding optimization.")
set_aom_config_var(CONFIG_TX_PARTITION_RESTRICT 1
                   "Disallow transform partition for large coding blocks.")

set_aom_config_var(CONFIG_REDUCE_SYMBOL_SIZE 1
                   "Symbol size reduction from 16 to 8.")
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
