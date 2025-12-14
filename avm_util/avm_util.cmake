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
if(AVM_AVM_UTIL_AVM_UTIL_CMAKE_)
  return()
endif() # AVM_AVM_UTIL_AVM_UTIL_CMAKE_
set(AVM_AVM_UTIL_AVM_UTIL_CMAKE_ 1)

list(
  APPEND
  AVM_UTIL_SOURCES
  "${AVM_ROOT}/avm_util/avm_thread.c"
  "${AVM_ROOT}/avm_util/avm_thread.h"
  "${AVM_ROOT}/avm_util/endian_inl.h"
  "${AVM_ROOT}/avm_util/debug_util.c"
  "${AVM_ROOT}/avm_util/debug_util.h")

# Creates the avm_util build target and makes libavm depend on it. The libavm
# target must exist before this function is called.
function(setup_avm_util_targets)
  add_library(avm_util OBJECT ${AVM_UTIL_SOURCES})
  set(AVM_LIB_TARGETS
      ${AVM_LIB_TARGETS} avm_util
      PARENT_SCOPE)
  target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_util>)
  if(BUILD_SHARED_LIBS)
    target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_util>)
  endif()
endfunction()
