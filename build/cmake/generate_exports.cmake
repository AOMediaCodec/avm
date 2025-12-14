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
cmake_minimum_required(VERSION 3.16)

set(REQUIRED_ARGS "AVM_ROOT" "AVM_CONFIG_DIR" "AVM_TARGET_SYSTEM"
                  "AVM_SYM_FILE" "CONFIG_AV2_DECODER" "CONFIG_AV2_ENCODER")

foreach(arg ${REQUIRED_ARGS})
  if("${${arg}}" STREQUAL "")
    message(FATAL_ERROR "${arg} must not be empty.")
  endif()
endforeach()

include("${AVM_ROOT}/build/cmake/exports_sources.cmake")

if("${AVM_TARGET_SYSTEM}" STREQUAL "Darwin")
  set(symbol_prefix "_")
elseif("${AVM_TARGET_SYSTEM}" MATCHES "Windows\|MSYS" AND AVM_MSVC)
  file(WRITE "${AVM_SYM_FILE}" "LIBRARY avm\n" "EXPORTS\n")
else()
  set(symbol_suffix ";")
endif()

set(avm_sym_file "${AVM_SYM_FILE}")

if("${AVM_TARGET_SYSTEM}" STREQUAL "Darwin")
  file(REMOVE "${avm_sym_file}")
elseif("${AVM_TARGET_SYSTEM}" MATCHES "Windows\|MSYS" AND AVM_MSVC)
  file(WRITE "${avm_sym_file}" "LIBRARY avm\n" "EXPORTS\n")
else()
  file(WRITE "${avm_sym_file}" "{\nglobal:\n")
endif()

foreach(export_file ${AVM_EXPORTS_SOURCES})
  file(STRINGS "${export_file}" exported_file_data)
  set(exported_symbols "${exported_symbols} ${exported_file_data};")
  string(STRIP "${exported_symbols}" exported_symbols)
endforeach()

foreach(exported_symbol ${exported_symbols})
  string(STRIP "${exported_symbol}" exported_symbol)
  if("${AVM_TARGET_SYSTEM}" MATCHES "Windows\|MSYS" AND AVM_MSVC)
    string(SUBSTRING ${exported_symbol} 0 4 export_type)
    string(COMPARE EQUAL "${export_type}" "data" is_data)
    if(is_data)
      set(symbol_suffix " DATA")
    else()
      set(symbol_suffix "")
    endif()
  endif()
  string(REGEX REPLACE "text \|data " "" "exported_symbol" "${exported_symbol}")
  set(exported_symbol "  ${symbol_prefix}${exported_symbol}${symbol_suffix}")
  file(APPEND "${avm_sym_file}" "${exported_symbol}\n")
endforeach()

if("${avm_sym_file}" MATCHES "ver$")
  file(APPEND "${avm_sym_file}" " \nlocal:\n  *;\n};")
endif()
