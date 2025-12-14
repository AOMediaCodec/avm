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
if(AVM_AVM_PORTS_AVM_PORTS_CMAKE_)
  return()
endif() # AVM_AVM_PORTS_AVM_PORTS_CMAKE_
set(AVM_AVM_PORTS_AVM_PORTS_CMAKE_ 1)

list(
  APPEND
  AVM_PORTS_INCLUDES
  "${AVM_ROOT}/avm_ports/avm_once.h"
  "${AVM_ROOT}/avm_ports/avm_timer.h"
  "${AVM_ROOT}/avm_ports/bitops.h"
  "${AVM_ROOT}/avm_ports/emmintrin_compat.h"
  "${AVM_ROOT}/avm_ports/mem.h"
  "${AVM_ROOT}/avm_ports/mem_ops.h"
  "${AVM_ROOT}/avm_ports/mem_ops_aligned.h"
  "${AVM_ROOT}/avm_ports/msvc.h"
  "${AVM_ROOT}/avm_ports/sanitizer.h"
  "${AVM_ROOT}/avm_ports/system_state.h")

list(APPEND AVM_PORTS_ASM_X86 "${AVM_ROOT}/avm_ports/emms.asm")

list(APPEND AVM_PORTS_INCLUDES_X86 "${AVM_ROOT}/avm_ports/x86_abi_support.asm")

list(APPEND AVM_PORTS_SOURCES_ARM "${AVM_ROOT}/avm_ports/arm.h"
     "${AVM_ROOT}/avm_ports/arm_cpudetect.c")

list(APPEND AVM_PORTS_SOURCES_PPC "${AVM_ROOT}/avm_ports/ppc.h"
     "${AVM_ROOT}/avm_ports/ppc_cpudetect.c")

# For arm and x86 targets:
#
# * Creates the avm_ports build target, adds the includes in avm_ports to the
#   target, and makes libavm depend on it.
#
# Otherwise:
#
# * Adds the includes in avm_ports to the libavm target.
#
# For all target platforms:
#
# * The libavm target must exist before this function is called.
function(setup_avm_ports_targets)
  if(XCODE AND "${AVM_TARGET_CPU}" STREQUAL "x86_64")
    add_asm_library("avm_ports" "AVM_PORTS_ASM_X86")
    # Xcode is the only one
    set(avm_ports_is_embedded 1)
    set(avm_ports_has_symbols 1)
  elseif("${AVM_TARGET_CPU}" MATCHES "^x86")
    add_asm_library("avm_ports" "AVM_PORTS_ASM_X86")
    set(avm_ports_has_symbols 1)
  elseif("${AVM_TARGET_CPU}" MATCHES "arm")
    add_library(avm_ports OBJECT ${AVM_PORTS_SOURCES_ARM})
    set(avm_ports_has_symbols 1)
  elseif("${AVM_TARGET_CPU}" MATCHES "ppc")
    add_library(avm_ports OBJECT ${AVM_PORTS_SOURCES_PPC})
    set(avm_ports_has_symbols 1)
  endif()

  if("${AVM_TARGET_CPU}" MATCHES "arm|ppc")
    target_sources(avm PRIVATE $<TARGET_OBJECTS:avm_ports>)
    if(BUILD_SHARED_LIBS)
      target_sources(avm_static PRIVATE $<TARGET_OBJECTS:avm_ports>)
    endif()
  endif()

  # Note AVM_PORTS_INCLUDES_X86 are not added to the avm_ports, avm or
  # avm_static targets to avoid compilation issues in projects that enable ASM
  # language support in project(). These sources were never included in
  # libavm_srcs.*; if it becomes necessary for a particular generator another
  # method should be used.
  if(avm_ports_has_symbols)
    if(NOT avm_ports_is_embedded)
      target_sources(avm_ports PRIVATE ${AVM_PORTS_INCLUDES})
    endif()
    set(AVM_LIB_TARGETS
        ${AVM_LIB_TARGETS}
        PARENT_SCOPE)
  else()
    target_sources(avm PRIVATE ${AVM_PORTS_INCLUDES})
    if(BUILD_SHARED_LIBS)
      target_sources(avm_static PRIVATE ${AVM_PORTS_INCLUDES})
    endif()
  endif()
endfunction()
