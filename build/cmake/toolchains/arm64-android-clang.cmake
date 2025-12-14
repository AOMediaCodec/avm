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
if(AVM_BUILD_CMAKE_TOOLCHAINS_ARM64_ANDROID_CLANG_CMAKE_)
  return()
endif() # AVM_BUILD_CMAKE_TOOLCHAINS_ARM64_ANDROID_CLANG_CMAKE_
set(AVM_BUILD_CMAKE_TOOLCHAINS_ARM64_ANDROID_CLANG_CMAKE_ 1)

if(NOT ANDROID_PLATFORM)
  set(ANDROID_PLATFORM android-21)
endif()

if(NOT ANDROID_ABI)
  set(ANDROID_ABI arm64-v8a)
endif()

set(AS_EXECUTABLE as)

# Toolchain files don't have access to cached variables:
# https://gitlab.kitware.com/cmake/cmake/issues/16170. Set an intermediate
# environment variable when loaded the first time.
if(AVM_ANDROID_NDK_PATH)
  set(ENV{_AVM_ANDROID_NDK_PATH} "${AVM_ANDROID_NDK_PATH}")
else()
  set(AVM_ANDROID_NDK_PATH "$ENV{_AVM_ANDROID_NDK_PATH}")
endif()

if("${AVM_ANDROID_NDK_PATH}" STREQUAL "")
  message(FATAL_ERROR "AVM_ANDROID_NDK_PATH not set.")
  return()
endif()

include("${AVM_ANDROID_NDK_PATH}/build/cmake/android.toolchain.cmake")

# No intrinsics flag required for arm64-android-clang.
set(AVM_NEON_INTRIN_FLAG "")

# No runtime cpu detect for arm64-android-clang.
set(CONFIG_RUNTIME_CPU_DETECT
    0
    CACHE STRING "")

set(CMAKE_SYSTEM_NAME "Android")
