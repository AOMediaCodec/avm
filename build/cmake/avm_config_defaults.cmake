#
# Copyright (c) 2021, Alliance for Open Media. All rights reserved
#
# This source code is subject to the terms of the BSD 3-Clause Clear License and
# the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
# License was not distributed with this source code in the LICENSE file, you can
# obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance
# for Open Media Patent License 1.0 was not distributed with this source code in
# the PATENTS file, you can obtain it at aomedia.org/license/patent-license/.

include("${AVM_ROOT}/build/cmake/util.cmake")

# This file sets default values for libavm configuration variables. All libavm
# config variables are added to the CMake variable cache via the macros provided
# in util.cmake.

#
# The variables in this section of the file are detected at configuration time,
# but can be overridden via the use of CONFIG_* and ENABLE_* values also defined
# in this file.
#

set_avm_detect_var(INLINE "" "Sets INLINE value for current target.")

# CPUs.
set_avm_detect_var(AVM_ARCH_AARCH64 0 "Enables AArch64 architecture.")
set_avm_detect_var(ARCH_ARM 0 "Enables ARM architecture.")
set_avm_detect_var(ARCH_MIPS 0 "Enables MIPS architecture.")
set_avm_detect_var(ARCH_PPC 0 "Enables PPC architecture.")
set_avm_detect_var(ARCH_X86 0 "Enables X86 architecture.")
set_avm_detect_var(ARCH_X86_64 0 "Enables X86_64 architecture.")

# ARM feature flags.
set_avm_detect_var(HAVE_NEON 0 "Enables NEON intrinsics optimizations.")

# MIPS feature flags.
set_avm_detect_var(HAVE_DSPR2 0 "Enables DSPR2 optimizations.")
set_avm_detect_var(HAVE_MIPS32 0 "Enables MIPS32 optimizations.")
set_avm_detect_var(HAVE_MIPS64 0 "Enables MIPS64 optimizations. ")
set_avm_detect_var(HAVE_MSA 0 "Enables MSA optimizations.")

# PPC feature flags.
set_avm_detect_var(HAVE_VSX 0 "Enables VSX optimizations.")

# x86/x86_64 feature flags.
set_avm_detect_var(HAVE_AVX 0 "Enables AVX optimizations.")
set_avm_detect_var(HAVE_AVX2 0 "Enables AVX2 optimizations.")
set_avm_detect_var(HAVE_MMX 0 "Enables MMX optimizations. ")
set_avm_detect_var(HAVE_SSE 0 "Enables SSE optimizations.")
set_avm_detect_var(HAVE_SSE2 0 "Enables SSE2 optimizations.")
set_avm_detect_var(HAVE_SSE3 0 "Enables SSE3 optimizations.")
set_avm_detect_var(HAVE_SSE4_1 0 "Enables SSE 4.1 optimizations.")
set_avm_detect_var(HAVE_SSE4_2 0 "Enables SSE 4.2 optimizations.")
set_avm_detect_var(HAVE_SSSE3 0 "Enables SSSE3 optimizations.")

# Flags describing the build environment.
set_avm_detect_var(HAVE_FEXCEPT 0
                   "Internal flag, GNU fenv.h present for target.")
set_avm_detect_var(HAVE_PTHREAD_H 0 "Internal flag, target pthread support.")
set_avm_detect_var(HAVE_UNISTD_H 0
                   "Internal flag, unistd.h present for target.")
set_avm_detect_var(HAVE_WXWIDGETS 0 "WxWidgets present.")

set_avm_config_var(CONFIG_F322_OBUER_EXPLICIT_REFLIST 1
                   "Signal explit_ref_frame_map in uncompressed_headr")
set_avm_config_var(CONFIG_ERROR_RESILIENT_FIX 1 "Additional check for s_frame.")

set_avm_config_var(CONFIG_F322_OBUER_REFRESTRICT 1
                   "Use restricted reference for switch frames and after.")

set_avm_config_var(CONFIG_F024_KEYOBU 1 "Use Key OBUs.")

#
# Variables in this section can be set from the CMake command line or from
# within the CMake GUI. The variables control libavm features.
#

# Build configuration flags.
set_avm_config_var(AVM_RTCD_FLAGS ""
                   "Arguments to pass to rtcd.pl. Separate with ';'")
set_avm_config_var(CONFIG_AV2_DECODER 1 "Enable AV2 decoder.")
set_avm_config_var(CONFIG_AV2_ENCODER 1 "Enable AV2 encoder.")
set_avm_config_var(CONFIG_BIG_ENDIAN 0 "Internal flag.")
set_avm_config_var(CONFIG_GCC 0 "Building with GCC (detect).")
set_avm_config_var(CONFIG_GCOV 0 "Enable gcov support.")
set_avm_config_var(CONFIG_GPROF 0 "Enable gprof support.")
set_avm_config_var(CONFIG_LIBYUV 1 "Enables libyuv scaling/conversion support.")
set_avm_config_var(CONFIG_LANCZOS_RESAMPLE 1 "Enables lanczos resize support.")

set_avm_config_var(CONFIG_MULTITHREAD 1 "Multithread support.")
set_avm_config_var(CONFIG_OS_SUPPORT 0 "Internal flag.")
set_avm_config_var(CONFIG_PIC 0 "Build with PIC enabled.")
set_avm_config_var(CONFIG_RUNTIME_CPU_DETECT 1 "Runtime CPU detection support.")
set_avm_config_var(CONFIG_SHARED 0 "Build shared libs.")
set_avm_config_var(CONFIG_WEBM_IO 1 "Enables WebM support.")

# Debugging flags.
set_avm_config_var(CONFIG_DEBUG 0 "Enable debug-only code.")
set_avm_config_var(CONFIG_MISMATCH_DEBUG 0 "Mismatch debugging flag.")
set_avm_config_var(CONFIG_EXCLUDE_SIMD_MISMATCH 1
                   "Exclude mismatch in SIMD functions for testing/debugging.")

# AV2 feature flags.
set_avm_config_var(CONFIG_ACCOUNTING 0 "Enables bit accounting.")
set_avm_config_var(CONFIG_ANALYZER 0 "Enables bit stream analyzer.")
set_avm_config_var(CONFIG_EXTRACT_PROTO 0
                   "Enables protobuf-based inspection tool.")
set_avm_config_var(CONFIG_COEFFICIENT_RANGE_CHECKING 0
                   "Coefficient range check.")
set_avm_config_var(CONFIG_DENOISE 1
                   "Denoise/noise modeling support in encoder.")
set_avm_config_var(CONFIG_INSPECTION 0 "Enables bitstream inspection.")
set_avm_config_var(CONFIG_INTERNAL_STATS 0 "Enables internal encoder stats.")
set_avm_config_var(CONFIG_MAX_DECODE_PROFILE 2
                   "Max profile to support decoding.")
set_avm_config_var(CONFIG_SIZE_LIMIT 0 "Limit max decode width/height.")
set_avm_config_var(CONFIG_SPATIAL_RESAMPLING 1 "Spatial resampling.")
set_avm_config_var(DECODE_HEIGHT_LIMIT 0 "Set limit for decode height.")
set_avm_config_var(DECODE_WIDTH_LIMIT 0 "Set limit for decode width.")
set_avm_config_var(CONFIG_TUNE_VMAF 0 "Enable encoding tuning for VMAF.")
set_avm_config_var(CONFIG_USE_VMAF_RC 0 "Use libvmaf_rc tune for VMAF_NEG.")

# AV2 experiment flags.
set_avm_config_var(CONFIG_SPEED_STATS 0 "AV2 experiment flag.")
set_avm_config_var(CONFIG_COLLECT_RD_STATS 0 "AV2 experiment flag.")
set_avm_config_var(CONFIG_DIST_8X8 0 "AV2 experiment flag.")
set_avm_config_var(CONFIG_ENTROPY_STATS 0 "AV2 experiment flag.")
set_avm_config_var(CONFIG_INTER_STATS_ONLY 0 "AV2 experiment flag.")
set_avm_config_var(CONFIG_BITSTREAM_DEBUG 0
                   "AV2 experiment flag for bitstream debugging.")
set_avm_config_var(CONFIG_RD_DEBUG 0 "AV2 experiment flag.")
set_avm_config_var(CONFIG_SHARP_SETTINGS 0 "AV2 experiment flag.")
set_avm_config_var(CONFIG_COLLECT_PARTITION_STATS 0
                   "Collect stats on partition decisions.")
set_avm_config_var(CONFIG_COLLECT_COMPONENT_TIMING 0
                   "Collect encoding component timing information.")
set_avm_config_var(CONFIG_AV2_TEMPORAL_DENOISING 0
                   "Build with temporal denoising support.")
set_avm_config_var(CONFIG_NN_V2 0 "Fully-connected neural nets ver.2.")

# CWG-F221
set_avm_config_var(CONFIG_PARAKIT_COLLECT_DATA 0
                   "enables data collection for ParaKit training.")

# AV2 experiment flags.
set_avm_config_var(CONFIG_METADATA 1 "F161 metadata syntax")
set_avm_config_var(CONFIG_ICC_METADATA 1 "ICC metadata syntax")

set_avm_config_var(CONFIG_CWG_E242_BITDEPTH 1 "Signal Bitdepth using a LUT.")

set_avm_config_var(CONFIG_CWG_E242_SEQ_HDR_ID 1 "Signal sequence header id.")

set_avm_config_var(CONFIG_DIP_EXT_PRUNING 1 "AV2 DIP TFLite pruning.")

set_avm_config_var(CONFIG_CWG_F270_CI_OBU 1 "Use content interpretation OBU")

set_avm_config_var(CONFIG_CWG_F270_OPS 1 "Add OPS and SH related changes")

# Source of throughput analysis : CWG-B065
set_avm_config_var(CONFIG_THROUGHPUT_ANALYSIS 0
                   "AV2 experiment flag to measure throughput.")

set_avm_config_var(
  CONFIG_QM_DEBUG 0
  "Enable debug information for extension to AV2 quantization matrices.")

# This is an encode-only change.
set_avm_config_var(CONFIG_ML_PART_SPLIT 1
                   "Partition SPLIT pruning/forcing as predicted by ML.")

set_avm_config_var(
  CONFIG_MIXED_LOSSLESS_ENCODE 0
  "Encoder only flag to configure encoder to enable mixed lossy/lossless coding"
)

#
# Variables in this section control optional features of the build system.
#
set_avm_option_var(ENABLE_CCACHE "Enable ccache support." OFF)
set_avm_option_var(ENABLE_DECODE_PERF_TESTS "Enables decoder performance tests"
                   OFF)
set_avm_option_var(ENABLE_DISTCC "Enable distcc support." OFF)
set_avm_option_var(ENABLE_DOCS
                   "Enable documentation generation (doxygen required)." ON)
set_avm_option_var(ENABLE_ENCODE_PERF_TESTS "Enables encoder performance tests"
                   OFF)
set_avm_option_var(ENABLE_EXAMPLES "Enables build of example code." ON)
set_avm_option_var(ENABLE_GOMA "Enable goma support." OFF)
set_avm_option_var(
  ENABLE_IDE_TEST_HOSTING
  "Enables running tests within IDEs like Visual Studio and Xcode." OFF)
set_avm_option_var(ENABLE_NASM "Use nasm instead of yasm for x86 assembly." OFF)
set_avm_option_var(ENABLE_TESTDATA "Enables unit test data download targets."
                   ON)
set_avm_option_var(ENABLE_TESTS "Enables unit tests." ON)
set_avm_option_var(ENABLE_TOOLS "Enable applications in tools sub directory."
                   ON)
set_avm_option_var(ENABLE_WERROR "Converts warnings to errors at compile time."
                   OFF)

# ARM assembly/intrinsics flags.
set_avm_option_var(ENABLE_NEON "Enables NEON optimizations on ARM targets." ON)

# MIPS assembly/intrinsics flags.
set_avm_option_var(ENABLE_DSPR2 "Enables DSPR2 optimizations on MIPS targets."
                   OFF)
set_avm_option_var(ENABLE_MSA "Enables MSA optimizations on MIPS targets." OFF)

# VSX intrinsics flags.
set_avm_option_var(ENABLE_VSX "Enables VSX optimizations on PowerPC targets."
                   ON)

# x86/x86_64 assembly/intrinsics flags.
set_avm_option_var(ENABLE_MMX
                   "Enables MMX optimizations on x86/x86_64 targets." ON)
set_avm_option_var(ENABLE_SSE
                   "Enables SSE optimizations on x86/x86_64 targets." ON)
set_avm_option_var(ENABLE_SSE2
                   "Enables SSE2 optimizations on x86/x86_64 targets." ON)
set_avm_option_var(ENABLE_SSE3
                   "Enables SSE3 optimizations on x86/x86_64 targets." ON)
set_avm_option_var(ENABLE_SSSE3
                   "Enables SSSE3 optimizations on x86/x86_64 targets." ON)
set_avm_option_var(ENABLE_SSE4_1
                   "Enables SSE4_1 optimizations on x86/x86_64 targets." ON)
set_avm_option_var(ENABLE_SSE4_2
                   "Enables SSE4_2 optimizations on x86/x86_64 targets." ON)
set_avm_option_var(ENABLE_AVX
                   "Enables AVX optimizations on x86/x86_64 targets." ON)
set_avm_option_var(ENABLE_AVX2
                   "Enables AVX2 optimizations on x86/x86_64 targets." ON)
