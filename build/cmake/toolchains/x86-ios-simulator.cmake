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
if(AVM_BUILD_CMAKE_TOOLCHAINS_X86_IOS_SIMULATOR_CMAKE_)
  return()
endif() # AVM_BUILD_CMAKE_TOOLCHAINS_X86_IOS_SIMULATOR_CMAKE_
set(AVM_BUILD_CMAKE_TOOLCHAINS_X86_IOS_SIMULATOR_CMAKE_ 1)

if(XCODE)

  # TODO(tomfinegan): Handle ios sim builds in Xcode.
  message(FATAL_ERROR "This toolchain does not support Xcode.")
endif()

set(CMAKE_SYSTEM_PROCESSOR "i386")
set(CMAKE_OSX_ARCHITECTURES "i386")

# Avoid noisy PIC/PIE warnings.
set(CONFIG_PIC
    1
    CACHE STRING "")

include("${CMAKE_CURRENT_LIST_DIR}/ios-simulator-common.cmake")
