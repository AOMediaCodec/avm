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
if(AVM_BUILD_CMAKE_TOOLCHAINS_ARM64_LINUX_GCC_CMAKE_)
  return()
endif() # AVM_BUILD_CMAKE_TOOLCHAINS_ARM64_LINUX_GCC_CMAKE_
set(AVM_BUILD_CMAKE_TOOLCHAINS_ARM64_LINUX_GCC_CMAKE_ 1)

set(CMAKE_SYSTEM_NAME "Linux")

if("${CROSS}" STREQUAL "")

  # Default the cross compiler prefix to something known to work.
  set(CROSS aarch64-linux-gnu-)
endif()

set(CMAKE_C_COMPILER ${CROSS}gcc)
set(CMAKE_CXX_COMPILER ${CROSS}g++)
set(CMAKE_ASM_COMPILER ${CROSS}as)
set(CMAKE_C_FLAGS_INIT "-march=armv8-a")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a")
set(AVM_AS_FLAGS "-march=armv8-a")
set(CMAKE_SYSTEM_PROCESSOR "arm64")

# No intrinsics flag required for arm64-linux-gcc.
set(AVM_NEON_INTRIN_FLAG "")

# No runtime cpu detect for arm64-linux-gcc.
set(CONFIG_RUNTIME_CPU_DETECT
    0
    CACHE STRING "")
