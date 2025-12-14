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

# Converts spaces in $in_string to semicolons and writes the output to
# $out_string. In CMake's eyes this converts the input string to a list.
function(listify_string in_string out_string)
  string(REPLACE " " ";" ${out_string} ${in_string})
  set(${out_string}
      "${${out_string}}"
      PARENT_SCOPE)
endfunction()

set(REQUIRED_ARGS "AVM_ROOT" "AVM_CONFIG_DIR" "AVM_DIST_DIR"
                  "AVM_DIST_INCLUDES" "AVM_DIST_LIBS" "ENABLE_DOCS")

foreach(arg ${REQUIRED_ARGS})
  if("${${arg}}" STREQUAL "")
    message(FATAL_ERROR "${arg} must not be empty.")
  endif()
endforeach()

if(ENABLE_DOCS)
  file(INSTALL "${AVM_CONFIG_DIR}/docs" DESTINATION "${AVM_DIST_DIR}")
endif()

if(AVM_DIST_EXAMPLES)
  listify_string("${AVM_DIST_EXAMPLES}" "AVM_DIST_EXAMPLES")
  foreach(example ${AVM_DIST_EXAMPLES})
    if(NOT "${example}" MATCHES "avmdec\|avmenc")
      file(INSTALL "${example}" DESTINATION "${AVM_DIST_DIR}/bin/examples")
    endif()
  endforeach()
endif()

if(AVM_DIST_TOOLS)
  listify_string("${AVM_DIST_TOOLS}" "AVM_DIST_TOOLS")
  foreach(tool ${AVM_DIST_TOOLS})
    file(INSTALL "${tool}" DESTINATION "${AVM_DIST_DIR}/bin/tools")
  endforeach()
endif()

if(AVM_DIST_APPS)
  listify_string("${AVM_DIST_APPS}" "AVM_DIST_APPS")
  foreach(app ${AVM_DIST_APPS})
    file(INSTALL "${app}" DESTINATION "${AVM_DIST_DIR}/bin")
  endforeach()
endif()

listify_string("${AVM_DIST_INCLUDES}" "AVM_DIST_INCLUDES")
foreach(inc ${AVM_DIST_INCLUDES})
  file(INSTALL "${inc}" DESTINATION "${AVM_DIST_DIR}/include/avm")
endforeach()

listify_string("${AVM_DIST_LIBS}" "AVM_DIST_LIBS")
foreach(lib ${AVM_DIST_LIBS})
  file(INSTALL "${lib}" DESTINATION "${AVM_DIST_DIR}/lib")
endforeach()
