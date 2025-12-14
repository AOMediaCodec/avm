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
if(AVM_AVM_MEM_AVM_MEM_CMAKE_)
  return()
endif() # AVM_AVM_MEM_AVM_MEM_CMAKE_
set(AVM_AVM_MEM_AVM_MEM_CMAKE_ 1)

list(APPEND AVM_MEM_SOURCES "${AVM_ROOT}/avm_mem/avm_mem.c"
     "${AVM_ROOT}/avm_mem/avm_mem.h"
     "${AVM_ROOT}/avm_mem/include/avm_mem_intrnl.h")

# Creates the avm_mem build target and makes libavm depend on it. The libavm
# target must exist before this function is called.
function(setup_avm_mem_targets)
  add_library(avm_mem OBJECT ${AVM_MEM_SOURCES})
  set(AVM_LIB_TARGETS
      ${AVM_LIB_TARGETS} avm_mem
      PARENT_SCOPE)
  target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_mem>)
  if(BUILD_SHARED_LIBS)
    target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_mem>)
  endif()
endfunction()
