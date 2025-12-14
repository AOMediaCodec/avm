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
if(AVM_BUILD_CMAKE_EXPORTS_CMAKE_)
  return()
endif() # AVM_BUILD_CMAKE_EXPORTS_CMAKE_
set(AVM_BUILD_CMAKE_EXPORTS_CMAKE_ 1)

include("${AVM_ROOT}/build/cmake/exports_sources.cmake")

# Creates the custom target which handles generation of the symbol export lists.
function(setup_exports_target)
  if("${AVM_TARGET_SYSTEM}" STREQUAL "Darwin")
    set(symbol_file_ext "syms")
  elseif("${AVM_TARGET_SYSTEM}" MATCHES "Windows\|MSYS" AND MSVC)
    set(symbol_file_ext "def")
  else()
    set(symbol_file_ext "ver")
  endif()

  set(avm_sym_file "${AVM_CONFIG_DIR}/libavm.${symbol_file_ext}")

  add_custom_target(
    generate_exports
    COMMAND
      ${CMAKE_COMMAND} -DAVM_ROOT="${AVM_ROOT}"
      -DAVM_CONFIG_DIR="${AVM_CONFIG_DIR}"
      -DAVM_TARGET_SYSTEM=${AVM_TARGET_SYSTEM} -DAVM_SYM_FILE="${avm_sym_file}"
      -DAVM_MSVC=${MSVC} -DAVM_XCODE=${XCODE} -DCONFIG_NAME=$<CONFIG>
      -DCONFIG_AV2_DECODER=${CONFIG_AV2_DECODER}
      -DCONFIG_AV2_ENCODER=${CONFIG_AV2_ENCODER}
      -DCONFIG_INSPECTION=${CONFIG_INSPECTION} -DENABLE_TESTS=${ENABLE_TESTS} -P
      "${AVM_ROOT}/build/cmake/generate_exports.cmake"
    SOURCES ${AVM_EXPORTS_SOURCES}
    DEPENDS ${AVM_EXPORTS_SOURCES})

  # Make libavm depend on the exports file, and set flags to pick it up when
  # creating the dylib.
  add_dependencies(avm generate_exports)

  if(APPLE)
    set_property(
      TARGET avm
      APPEND_STRING
      PROPERTY LINK_FLAGS "-exported_symbols_list ${avm_sym_file}")
  elseif(WIN32)
    if(NOT MSVC)
      set_property(
        TARGET avm
        APPEND_STRING
        PROPERTY LINK_FLAGS "-Wl,--version-script ${avm_sym_file}")
    else()
      set_property(
        TARGET avm
        APPEND_STRING
        PROPERTY LINK_FLAGS "/DEF:${avm_sym_file}")
    endif()

    # TODO(tomfinegan): Sort out the import lib situation and flags for MSVC.

  else()
    set_property(
      TARGET avm
      APPEND_STRING
      PROPERTY LINK_FLAGS "-Wl,--version-script,${avm_sym_file}")
  endif()
endfunction()
