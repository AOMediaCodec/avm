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
set_aom_config_var(CONFIG_LPF_MASK 0
                   "Enable the use loop filter bitmasks for optimizations.")
set_aom_config_var(CONFIG_AV1_TEMPORAL_DENOISING 0
                   "Build with temporal denoising support.")
set_aom_config_var(CONFIG_NN_V2 0 "Fully-connected neural nets ver.2.")
set_aom_config_var(CONFIG_OPTICAL_FLOW_API 0
                   "AV1 experiment flag for optical flow API.")
set_aom_config_var(CONFIG_AV2CTC_PSNR_PEAK 1
                   "Use AV2 CTC type PSNR peak for 10- and 12-bit")
set_aom_config_var(CONFIG_ZERO_OFFSET_BITUPSHIFT 1
                   "Use zero offset for non-normative bit upshift")

# AV2 experiment flags.
set_aom_config_var(CONFIG_IMPROVEIDTX_CTXS 1
                   "AV2 enable improved identity transform coding 1/2.")
set_aom_config_var(CONFIG_IMPROVEIDTX_RDPH 1
                   "AV2 enable improved identity transform coding 2/2.")
set_aom_config_var(CONFIG_COEFF_HR_LR1 1
                   "AV2 enable coding 1 LR in coefficient coding.")
set_aom_config_var(CONFIG_LCCHROMA 1 "AV2 enable low-complexity chroma coding.")
set_aom_config_var(CONFIG_ENTROPY_PARA 1 "AV2 enable PARA method for entropy.")
set_aom_config_var(
  CONFIG_CHROMA_TX_COEFF_CODING 1
  "AV2 experiment flag to enable improved chroma transform coefficient coding")
set_aom_config_var(CONFIG_BYPASS_IMPROVEMENT 1
                   "AV2 enable entropy bypass improvement.")
set_aom_config_var(CONFIG_EOB_POS_LUMA 1 "EOB position coding for luma.")
set_aom_config_var(
  CONFIG_EXT_RECUR_PARTITIONS 1 NUMBER
  "AV2 Fully recursive partitions including H partitions experiment flag")
set_aom_config_var(CONFIG_BLOCK_256 1 NUMBER "AV2 BLOCK_256 experiment flag")
set_aom_config_var(CONFIG_FLEX_PARTITION 1 NUMBER
                   "AV2 Flexible partition experiment flag")
set_aom_config_var(CONFIG_CB1TO4_SPLIT 1 NUMBER
                   "AV2 amended flexible partition experiment flag")
set_aom_config_var(CONFIG_INTRA_SDP_LATENCY_FIX 1 NUMBER
                   "AV2 intra SDP latency issue addressing flag")
set_aom_config_var(CONFIG_ERP_TFLITE 0 NUMBER "Build ERP with TFLite")
set_aom_config_var(CONFIG_COMPOUND_WARP_SAMPLES 1 NUMBER
                   "AV2 compound warped motion samples experiment flag")
set_aom_config_var(CONFIG_NEW_TX_PARTITION 1
                   "AV2 new transform partitions experiment flag.")
set_aom_config_var(
  CONFIG_IDIF 1
  "AV2 experiment flag to enable Intra Directional Interpolation Filter.")
set_aom_config_var(CONFIG_ORIP_DC_DISABLED 0
                   "AV2 experiment flag to disable ORIP for DC mode.")
set_aom_config_var(CONFIG_ORIP_NONDC_DISABLED 0
                   "AV2 experiment flag to disable ORIP for non-DC modes.")
set_aom_config_var(CONFIG_MVP_IMPROVEMENT 1 "Enable MVP improvement")
set_aom_config_var(
  CONFIG_CCSO 1 "AV2 experiment flag to enable cross component sample offset.")
set_aom_config_var(CONFIG_CCSO_EDGE_CLF 1
                   "Enable adaptive edge classifier for CCSO.")
set_aom_config_var(CONFIG_OPTFLOW_REFINEMENT 1
                   "AV2 experiment flag for optical flow MV refinement")
set_aom_config_var(
  CONFIG_IBP_DC 1
  "AV2 experiment flag to enable intra bi-prediction for DC mode.")
set_aom_config_var(CONFIG_AIMC 1 "AV2 adaptive intra mode coding flag.")
set_aom_config_var(CONFIG_WAIP 1 "AV2 wide angular intra prediction flag.")
set_aom_config_var(CONFIG_UV_CFL 1 "AV2 intra UV mode and CFL coding flag.")
set_aom_config_var(CONFIG_MORPH_PRED 1 "AV2 intra prediction mode flag.")
set_aom_config_var(
  CONFIG_CONTEXT_DERIVATION 1
  "AV2 experiment flag to enable modified context derivation : CWG-B065.")
set_aom_config_var(CONFIG_EXTENDED_WARP_PREDICTION 1
                   "AV2 experiment flag to add new local warp modes")
# Begin: CWG-C016
set_aom_config_var(
  CONFIG_LR_IMPROVEMENTS
  1
  "This is a flag which combines
                   CONFIG_LR_FLEX_SYNTAX: experiment flag to enable LR flexible syntax,
                   CONFIG_WIENER_NONSEP: nonsep Wiener filter experiment flag,
                   CONFIG_WIENER_NONSEP_CROSS_FILT: nonsep Wiener cross filter experiment flag,
                   CONFIG_PC_WIENER: pixel-classified Wiener filter experiment flag,
                   and CONFIG_FLEXIBLE_RU_SIZE: choose RU size between 128x128, 256x256 and 512x512"
)
set_aom_config_var(CONFIG_LR_MERGE_COEFFS 1
                   "AV2 experiment flag to enable LR coefficient merging")
# End: CWG-C016
set_aom_config_var(
  CONFIG_IMPROVED_DS_CC_WIENER 1
  "AV2 improved luma downsampling for high pass cross non-sep wiener filter")

# Source of throughput analysis : CWG-B065
set_aom_config_var(CONFIG_THROUGHPUT_ANALYSIS 0
                   "AV2 experiment flag to measure throughput.")
set_aom_config_var(CONFIG_IBC_SR_EXT 1 "Enables IntraBC search range extension")
set_aom_config_var(CONFIG_IBC_BV_IMPROVEMENT 1
                   "Enables BV improvements for IBC")
set_aom_config_var(CONFIG_IBC_MAX_DRL 1
                   "Enables Max DRL index signaling for IntraBC")
set_aom_config_var(CONFIG_CCSO_EXT 1
                   "AV2 experiment flag to enable extended CCSO.")
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
set_aom_config_var(CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT 1
                   "Enable frame output order derivation from order hint")
set_aom_config_var(CONFIG_OPTFLOW_ON_TIP 1
                   "Enable optical flow refinement on top of TIP")
set_aom_config_var(CONFIG_TIP_DIRECT_FRAME_MV 1
                   "Enable frame level MV for TIP direct mode")
set_aom_config_var(CONFIG_TIP_REF_PRED_MERGING 1
                   "Enable Merge the prediction steps for TIP_FRAME_AS_REF")
set_aom_config_var(CONFIG_ALLOW_SAME_REF_COMPOUND 1
                   "Allow compound mode to refer to the same reference frame")
set_aom_config_var(CONFIG_IMPROVED_SAME_REF_COMPOUND 1
                   "Improved same reference compound mode")
set_aom_config_var(CONFIG_DISPLAY_ORDER_HINT_FIX 1
                   "Bug fix on display order hints of key frames")
set_aom_config_var(CONFIG_BAWP 1 "Enable block adaptive weighted prediction")
set_aom_config_var(CONFIG_EXPLICIT_BAWP 0
                   "Explicit signaling for block adaptive weighted prediction")
set_aom_config_var(CONFIG_BAWP_CHROMA 1
                   "Enable block adaptive weighted prediction for Chroma")
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

set_aom_config_var(CONFIG_EXT_WARP_FILTER 1 "Enable extended warp filter")

set_aom_config_var(CONFIG_ADST_TUNED 1
                   "AV2 experiment to replace the ADST 4, 8 and 16 basis")

set_aom_config_var(CONFIG_TX_PARTITION_CTX 1
                   "Enable to optimize txfm partition context")

set_aom_config_var(CONFIG_INTERINTRA_IMPROVEMENT 1
                   "Enable additional inter-intra block sizes")
set_aom_config_var(CONFIG_REFRESH_FLAG 0
                   "Experiment flag to Signal refresh frame flag with 3 bits")

set_aom_config_var(CONFIG_D149_CTX_MODELING_OPT 1
                   "Enable to optimize block size dependent context modeling")
set_aom_config_var(
  CONFIG_COMPOUND_WARP_CAUSAL 1
  "AV2 experiment flag to enable compound new_newmv warp_causal mode")

set_aom_config_var(CONFIG_D072_SKIP_MODE_IMPROVE 1
                   "Enable to improve skip mode")

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
set_aom_config_var(CONFIG_BLEND_MODE 1
                   "Enable improved intra blend mode from CWG-D046")
set_aom_config_var(CONFIG_ENABLE_MHCCP 1
                   "Enable multi hypothesis cross component prediction")
set_aom_config_var(CONFIG_C071_SUBBLK_WARPMV 1
                   "AV2 experiment flag to use subblock warp MV for SMVP")

set_aom_config_var(CONFIG_C076_INTER_MOD_CTX 1
                   "AV2 experiment flag to simplify inter mode contexts")

set_aom_config_var(CONFIG_NEW_CONTEXT_MODELING 1
                   "Enable to improve the context modeling")
set_aom_config_var(CONFIG_WEDGE_MOD_EXT 1 "AV2 wedge modes extensions.")

set_aom_config_var(CONFIG_MF_IMPROVEMENT 1
                   "Enable to improve temporal motion projection")

set_aom_config_var(CONFIG_MVP_SIMPLIFY 1
                   "Enable to simplify MVP list construction")

set_aom_config_var(CONFIG_TMVP_IMPROVE 1
                   "Enable to improve TMVP candidate selection in DRL list")

# This is an encode-only change.
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

set_aom_config_var(CONFIG_CCSO_BO_ONLY_OPTION 1
                   "Enable CCSO band offset only option.")

set_aom_config_var(
  CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX 1
  "CWG-D086: Context modeling for transform block zero flag signaling")

set_aom_config_var(
  CONFIG_TIP_IMPLICIT_QUANT 1
  "Enable implicit quantization derivation for TIP direct mode")
set_aom_config_var(CONFIG_REFINED_MVS_IN_TMVP 1
                   "Keep optical flow refined MVs in TMVP list.")
set_aom_config_var(CONFIG_AFFINE_REFINEMENT 1
                   "Decoder side affine motion refinement.")
set_aom_config_var(CONFIG_DERIVED_MVD_SIGN 1 "Enable MVD sign derivations")
set_aom_config_var(CONFIG_VQ_MVD_CODING 1 "Enable VQ based MVD coding")
set_aom_config_var(CONFIG_QM_SIMPLIFY 1
                   "Enable new QMs based on 8x8, 8x4, and 4x8 sizes.")
set_aom_config_var(CONFIG_OPFL_BI 1 "Enable bilinear for initial opfl.")

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
