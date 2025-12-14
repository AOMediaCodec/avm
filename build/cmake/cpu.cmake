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

if("${AVM_TARGET_CPU}" STREQUAL "arm64")
  set(AVM_ARCH_ARM 1)
  set(AVM_ARCH_AARCH64 1)
  set(RTCD_ARCH_ARM "yes")
  set(ARM64_FLAVORS "NEON;ARM_CRC32;NEON_DOTPROD;NEON_I8MM;SVE;SVE2")
  set(AVM_ARM_CRC32_DEFAULT_FLAG "-march=armv8-a+crc")
  set(AVM_NEON_DOTPROD_DEFAULT_FLAG "-march=armv8.2-a+dotprod")
  set(AVM_NEON_I8MM_DEFAULT_FLAG "-march=armv8.2-a+dotprod+i8mm")
  set(AVM_SVE_DEFAULT_FLAG "-march=armv8.2-a+dotprod+i8mm+sve")
  set(AVM_SVE2_DEFAULT_FLAG "-march=armv9-a+sve2") # SVE2 is a v9-only feature
  # Check that the compiler flag to enable each flavor is supported by the
  # compiler. This may not be the case for new architecture features on old
  # compiler versions.
  foreach(flavor ${ARM64_FLAVORS})
    if(ENABLE_${flavor} AND NOT DEFINED AVM_${flavor}_FLAG)
      set(AVM_${flavor}_FLAG "${AVM_${flavor}_DEFAULT_FLAG}")
      string(TOLOWER "${flavor}" flavor_lower)
      # Do not use check_c_compiler_flag here since the regex used to match
      # against stderr does not recognise the "invalid feature modifier" error
      # produced by certain versions of GCC, leading to the feature being
      # incorrectly marked as available.
      set(OLD_CMAKE_REQURED_FLAGS ${CMAKE_REQUIRED_FLAGS})
      set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${AVM_${flavor}_FLAG}")
      unset(FLAG_SUPPORTED)
      avm_check_source_compiles("arm_feature_flag_${flavor_lower}_available"
                                "static void function(void) {}" FLAG_SUPPORTED)
      set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQURED_FLAGS})
      if(NOT ${FLAG_SUPPORTED})
        set(ENABLE_${flavor} 0)
      endif()
    endif()
  endforeach()
  # SVE and SVE2 require that the Neon-SVE bridge header is also available.
  if(ENABLE_SVE OR ENABLE_SVE2)
    set(OLD_CMAKE_REQURED_FLAGS ${CMAKE_REQUIRED_FLAGS})
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${AVM_SVE_FLAG}")
    avm_check_source_compiles(
      "arm_neon_sve_bridge_available"
      "
#ifndef __ARM_NEON_SVE_BRIDGE
#error 1
#endif
#include <arm_sve.h>
#include <arm_neon_sve_bridge.h>"
      HAVE_SVE_HEADERS)
    set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQURED_FLAGS})
    if(HAVE_SVE_HEADERS EQUAL 0)
      set(ENABLE_SVE 0)
      set(ENABLE_SVE2 0)
    endif()
  endif()
  foreach(flavor ${ARM64_FLAVORS})
    if(ENABLE_${flavor})
      set(HAVE_${flavor} 1)
      set(RTCD_HAVE_${flavor} "yes")
    else()
      set(HAVE_${flavor} 0)
      string(TOLOWER ${flavor} flavor)
      set(AVM_RTCD_FLAGS ${AVM_RTCD_FLAGS} --disable-${flavor})
    endif()
  endforeach()

elseif("${AVM_TARGET_CPU}" MATCHES "^arm")
  set(ARCH_ARM 1)
  set(RTCD_ARCH_ARM "yes")

  if(ENABLE_NEON)
    set(HAVE_NEON 1)
    set(RTCD_HAVE_NEON "yes")
  else()
    set(HAVE_NEON 0)
    set(AVM_RTCD_FLAGS ${AVM_RTCD_FLAGS} --disable-neon)
  endif()
elseif("${AVM_TARGET_CPU}" MATCHES "^mips")
  set(ARCH_MIPS 1)
  set(RTCD_ARCH_MIPS "yes")

  if("${AVM_TARGET_CPU}" STREQUAL "mips32")
    set(HAVE_MIPS32 1)
    set(RTCD_HAVE_MIPS32 "yes")
  elseif("${AVM_TARGET_CPU}" STREQUAL "mips64")
    set(HAVE_MIPS64 1)
    set(RTCD_HAVE_MIPS64 "yes")
  endif()

  # HAVE_DSPR2 is set by mips toolchain files.
  if(ENABLE_DSPR2 AND HAVE_DSPR2)
    set(RTCD_HAVE_DSPR2 "yes")
  else()
    set(HAVE_DSPR2 0)
    set(AVM_RTCD_FLAGS ${AVM_RTCD_FLAGS} --disable-dspr2)
  endif()

  # HAVE_MSA is set by mips toolchain files.
  if(ENABLE_MSA AND HAVE_MSA)
    set(RTCD_HAVE_MSA "yes")
  else()
    set(HAVE_MSA 0)
    set(AVM_RTCD_FLAGS ${AVM_RTCD_FLAGS} --disable-msa)
  endif()
elseif("${AVM_TARGET_CPU}" MATCHES "ppc")
  set(ARCH_PPC 1)
  set(RTCD_ARCH_PPC "yes")

  if(ENABLE_VSX)
    set(HAVE_VSX 1)
    set(RTCD_HAVE_VSX "yes")
  else()
    set(HAVE_VSX 0)
    set(AVM_RTCD_FLAGS ${AVM_RTCD_FLAGS} --disable-vsx)
  endif()
elseif("${AVM_TARGET_CPU}" MATCHES "^x86")
  if("${AVM_TARGET_CPU}" STREQUAL "x86")
    set(ARCH_X86 1)
    set(RTCD_ARCH_X86 "yes")
  elseif("${AVM_TARGET_CPU}" STREQUAL "x86_64")
    set(ARCH_X86_64 1)
    set(RTCD_ARCH_X86_64 "yes")
  endif()

  set(X86_FLAVORS "MMX;SSE;SSE2;SSE3;SSSE3;SSE4_1;SSE4_2;AVX;AVX2")
  foreach(flavor ${X86_FLAVORS})
    if(ENABLE_${flavor} AND NOT disable_remaining_flavors)
      set(HAVE_${flavor} 1)
      set(RTCD_HAVE_${flavor} "yes")
    else()
      set(disable_remaining_flavors 1)
      set(HAVE_${flavor} 0)
      string(TOLOWER ${flavor} flavor)
      set(AVM_RTCD_FLAGS ${AVM_RTCD_FLAGS} --disable-${flavor})
    endif()
  endforeach()
endif()
