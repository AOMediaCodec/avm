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

set_aom_config_var(CONFIG_F322_OBUER_EXPLICIT_REFLIST 1
                   "Signal explit_ref_frame_map in uncompressed_headr")
set_aom_config_var(CONFIG_F322_OBUER_ERM 1
                   "Infer error_resilient_mode without signalling.")
set_aom_config_var(CONFIG_ERROR_RESILIENT_FIX 1 "Additional check for s_frame.")
set_aom_config_var(
  CONFIG_F322_OBUER_ERM_FIX990
  1
  "Add g_error_resilient_mode option to the encoder for the error resilient option."
)

set_aom_config_var(CONFIG_F160_TD 1 "Signal temporal delimiter optionally")
set_aom_config_var(CONFIG_F255_QMOBU 1 "Enable QM_OBU")
set_aom_config_var(CONFIG_F024_KEYOBU 1 "Use Key OBUs.")

set_aom_config_var(CONFIG_F153_FGM_OBU 1 "Signal film grain model in OBU_FGM.")

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

# CWG-F221
set_aom_config_var(CONFIG_MULTILAYER_HLS 1 "HLS for multilayer coding.")
set_aom_config_var(CONFIG_LCR_ID_IN_SH 1 "Add LCR id in the Sequence Header.")
set_aom_config_var(CONFIG_ATLAS_BACKGROUND_COLOR 1
                   "Atlas background color information.")
set_aom_config_var(CONFIG_ATLAS_ALPHA_SEGMENT 1
                   "Atlas alpha segment information.")
set_aom_config_var(CONFIG_SET_DEFAULT_VALUE_XLAYER_ID 1
                   "Set the default value of xlayer_id to global scope OBUs.")
set_aom_config_var(CONFIG_PARAKIT_COLLECT_DATA 0
                   "enables data collection for ParaKit training.")

set_aom_config_var(CONFIG_EXT_FRAME_BUFFER_POOL 1 "Buffer pool extension.")

# AV2 experiment flags.
set_aom_config_var(CONFIG_F356_SEF_DOH 1 "Signl order hint for SEF.")

set_aom_config_var(
  CONFIG_F106_OBU_TILEGROUP 1
  "Consolidate FRAME, FRAM_EHEADER, TILE_GROUP OBUs into TILE_GROUP OBU.")
set_aom_config_var(CONFIG_F106_OBU_SWITCH 1 "Use SWITCH_OBU.")
set_aom_config_var(CONFIG_F106_OBU_SEF 1 "Use SEF OBU.")
set_aom_config_var(CONFIG_F106_OBU_TIP 1 "Use TIP_OBU.")

set_aom_config_var(CONFIG_METADATA 1 "F161 metadata syntax")
set_aom_config_var(CONFIG_ICC_METADATA 1 "ICC metadata syntax")

# CWG-E242
set_aom_config_var(CONFIG_CWG_E242_SIGNAL_TILE_INFO 1
                   "Signal tile information at sequence header.")

set_aom_config_var(CONFIG_MFH_SIGNAL_TILE_INFO 1
                   "Tile information in the multi frame header.")

set_aom_config_var(CONFIG_MULTI_LEVEL_SEGMENTATION 1
                   "Segmentation information in the multi frame header")

set_aom_config_var(CONFIG_CWG_E242_PARSING_INDEP 1
                   "Parsing independence of MFH.")

set_aom_config_var(CONFIG_CWG_E242_BITDEPTH 1 "Signal Bitdepth using a LUT.")

set_aom_config_var(CONFIG_CWG_E242_SEQ_HDR_ID 1 "Signal sequence header id.")

set_aom_config_var(CONFIG_REORDER_SEQ_FLAGS 1 "Group sequence header flags.")
set_aom_config_var(CONFIG_IMPROVED_REORDER_SEQ_FLAGS 1
                   "Improvement of Group sequence header flags.")

set_aom_config_var(CONFIG_MINIMUM_LR_UNIT_SIZE_64x64 0
                   "Support minimum LR Unit size ")
set_aom_config_var(CONFIG_LR_FRAMEFILTERS_IN_HEADER 0
                   "AV2 expt to move frame filters to frame header.")
set_aom_config_var(CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES 1
                   "AV2 control to allow loopfilters across tiles or not")
set_aom_config_var(CONFIG_DIP_EXT_PRUNING 1 "AV2 DIP TFLite pruning.")

set_aom_config_var(CONFIG_SCAN_TYPE_METADATA 1
                   "CWG-F233 Scan type metadata syntax")
# CWG-F349
set_aom_config_var(CONFIG_CWG_F349_SIGNAL_TILE_INFO 1
                   "Improved tile information at sequence header.")

# CWG-E242 Chroma Format IDC
set_aom_config_var(CONFIG_CWG_E242_CHROMA_FORMAT_IDC 1 "Chroma format idc.")

# CWG-F220 Sequence Cropping Window
set_aom_config_var(CONFIG_CROP_WIN_CWG_F220 1 "Sequence Cropping Window")

set_aom_config_var(CONFIG_CWG_F270_CI_OBU 1 "Use content interpretation OBU")

set_aom_config_var(CONFIG_CWG_F298_REC11 1 NUMBER
                   "Scaling function improvements for film grain synthesis.")

# Source of throughput analysis : CWG-B065
set_aom_config_var(CONFIG_THROUGHPUT_ANALYSIS 0
                   "AV2 experiment flag to measure throughput.")
set_aom_config_var(CONFIG_FRAME_OUTPUT_ORDER_WITH_LAYER_ID 1
                   "Enable frame output order derivation with layer ID")
set_aom_config_var(CONFIG_MULTI_FRAME_HEADER 1 "Enable multi-frame header.")
set_aom_config_var(CONFIG_CWG_E242_MFH_ID_UVLC 1
                   "Signaling multi-frame header ID in UVLC")
set_aom_config_var(CONFIG_RANDOM_ACCESS_SWITCH_FRAME 1
                   "Enable random access switch (RAS) frame")
set_aom_config_var(CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME 1
                   "Enable temporal unit based on showable frame")
set_aom_config_var(CONFIG_REMOVAL_REDUNDANT_FRAME_HEADER 1
                   "Remove redundant frame header OBU from OBU types")
set_aom_config_var(CONFIG_MULTI_STREAM 1 "AV2 enable multi-streams.")
set_aom_config_var(OBU_ORDER_IN_TU 1 "Check OBU order in TU")

set_aom_config_var(CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING 1
                   "Buffer removal timing OBU")

set_aom_config_var(
  CONFIG_QM_DEBUG 0
  "Enable debug information for extension to AV1 quantization matrices.")

set_aom_config_var(CONFIG_CWG_F248_RENDER_SIZE 1 "Remove render size.")

set_aom_config_var(CONFIG_FRAME_HALF_PRECISION 1
                   "Enable frame level half precision")

set_aom_config_var(CONFIG_DC_DIV_UNIFY 1 "Unify DC division")

set_aom_config_var(CONFIG_MHCCP_BLK_SIZE 1
                   "Add block size constraint for MHCCP")

set_aom_config_var(CONFIG_MHCCP_SOLVER_BITS 1
                   "Reduce the number of bits for MHCCP solver")

set_aom_config_var(CONFIG_RU_SIZE_RESTRICTION 1
                   "RU size shall be larger than or equal to sb size")

set_aom_config_var(CONFIG_FIX_OPFL_AUTO 1
                   "Fix optical flow auto mode, option 3")
set_aom_config_var(CONFIG_FIX_BW_CHROMA_REFINED_MV 1
                   "Fix bandwidth issue for the refined MVs reused by chroma")

set_aom_config_var(
  CONFIG_MV_VALUE_CLIP 1
  "Clip MV value to ensure it is within the constrained range.")

set_aom_config_var(
  CONFIG_INTER_BAWP_CONSTRAINT 1
  "Inter BAWP template cannot outside the valid reference range")

# This is an encode-only change.
set_aom_config_var(CONFIG_FAST_INTER_RDO 1 "Fast inter mode selection")

set_aom_config_var(CONFIG_F107_GRADIENT_SIMPLIFY 1
                   "Use smaller OPFL gradient units and remove bit checks.")

set_aom_config_var(CONFIG_ML_PART_SPLIT 1
                   "Partition SPLIT pruning/forcing as predicted by ML.")

set_aom_config_var(CONFIG_UNIFORM_TILE 1
                   "Enable better uniform tile distribution")

set_aom_config_var(
  CONFIG_ADJ_PYR_Q_OFFSET_LD 1
  "Encoder-only config to adjust pyr qp offsets for LD from nominal.")

set_aom_config_var(CONFIG_MOTION_MODE_FRAME_HEADERS_OPT 1
                   "Frame header optimzation of motion modes.")

set_aom_config_var(CONFIG_CWG_F362 1
                   "Implicit frame tool flags for single picture headers.")
set_aom_config_var(
  CONFIG_CWG_F377_STILL_PICTURE 1
  "Sequence header OBU changes for single_picture_header_flag.")

set_aom_config_var(CONFIG_DF_DQP 1 "Adding DQP to the deblocking filter")

set_aom_config_var(CONFIG_SHORT_METADATA 1
                   "Enable short metadata OBU header support")

set_aom_config_var(CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS 1
                   "Disable loop filters for lossless segments")
set_aom_config_var(
  CONFIG_MIXED_LOSSLESS_ENCODE 0
  "Encoder only flag to configure encoder to enable mixed lossy/lossless coding"
)
set_aom_config_var(CONFIG_CWG_F317 1 "Bridge frame")
set_aom_config_var(CONFIG_CWG_F317_TEST_PATTERN 1
                   "Bridge frame - unit test pattern")

set_aom_config_var(CONFIG_4X4_WARP_FIX 1 "Fix 4x4 warp padding")

set_aom_config_var(CONFIG_BAND_METADATA 1 "Enable banding hints metadata.")

set_aom_config_var(
  CONFIG_DISABLE_CROSS_FRAME_CDF_INIT 1
  "Introduce frame level flag for disabling cross frame CDF initialization.")
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
