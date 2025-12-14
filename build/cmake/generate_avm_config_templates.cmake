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

string(TIMESTAMP year "%Y")
set(asm_file_header_block
    "\;
\; Copyright (c) ${year}, Alliance for Open Media. All rights reserved
\;
\; This source code is subject to the terms of the BSD 3-Clause Clear License and the
\; Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
\; not distributed with this source code in the LICENSE file, you can obtain it
\; at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
\; License 1.0 was not distributed with this source code in the PATENTS file, you
\; can obtain it at aomedia.org/license/patent-license/.
\;
")
set(h_file_header_block
    "/*
 * Copyright (c) ${year}, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License and the
 * Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
 * not distributed with this source code in the LICENSE file, you can obtain it
 * at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
 * License 1.0 was not distributed with this source code in the PATENTS file, you
 * can obtain it at aomedia.org/license/patent-license/.
 */
\#ifndef AVM_CONFIG_H_
\#define AVM_CONFIG_H_
")
set(cmake_file_header_block
    "##
## Copyright (c) ${year}, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
")

# Terminates cmake execution when $var_name is an empty string, or the variable
# name it contains does not expand to an existing directory.
function(check_directory_var var_name)
  if("${var_name}" STREQUAL "")
    message(FATAL_ERROR "The CMake variable ${var_name} must be defined.")
  endif()

  if(NOT EXISTS "${${var_name}}")
    message(FATAL_ERROR "${${var_name}} (${var_name}) missing.")
  endif()
endfunction()

check_directory_var(AVM_CONFIG_DIR)
check_directory_var(AVM_ROOT)

set(AVM_DEFAULTS "${AVM_ROOT}/build/cmake/avm_config_defaults.cmake")
if(NOT EXISTS "${AVM_DEFAULTS}")
  message(
    FATAL_ERROR "Configuration default values file (${AVM_DEFAULTS}) missing.")
endif()

include("${AVM_ROOT}/build/cmake/avm_config_defaults.cmake")
list(APPEND avm_build_vars ${AVM_DETECT_VARS} ${AVM_CONFIG_VARS})
list(SORT avm_build_vars)

set(avm_config_h_template "${AVM_CONFIG_DIR}/config/avm_config.h.cmake")
file(WRITE "${avm_config_h_template}" ${h_file_header_block})
foreach(avm_var ${avm_build_vars})
  if(NOT "${avm_var}" STREQUAL "AVM_RTCD_FLAGS")
    file(APPEND "${avm_config_h_template}"
         "\#define ${avm_var} \${${avm_var}}\n")
  endif()
endforeach()
file(APPEND "${avm_config_h_template}" "\#endif  // AVM_CONFIG_H_")

set(avm_asm_config_template "${AVM_CONFIG_DIR}/config/avm_config.asm.cmake")
file(WRITE "${avm_asm_config_template}" ${asm_file_header_block})
foreach(avm_var ${avm_build_vars})
  if(NOT "${avm_var}" STREQUAL "INLINE" AND NOT "${avm_var}" STREQUAL
                                            "AVM_RTCD_FLAGS")
    file(APPEND "${avm_asm_config_template}" "${avm_var} equ \${${avm_var}}\n")
  endif()
endforeach()
