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

set(REQUIRED_ARGS "AVM_ROOT" "AVM_CONFIG_DIR" "GIT_EXECUTABLE"
                  "PERL_EXECUTABLE")

foreach(arg ${REQUIRED_ARGS})
  if("${${arg}}" STREQUAL "")
    message(FATAL_ERROR "${arg} must not be empty.")
  endif()
endforeach()

include("${AVM_ROOT}/build/cmake/util.cmake")

# Generate the version string for this run. Allow optional prefix like 'foo-'
# before the version string 'v*'.
unset(avm_version)
if(EXISTS "${GIT_EXECUTABLE}")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} --git-dir=${AVM_ROOT}/.git describe --tags --match
            "*-v[0-9]*" --match "v[0-9]*"
    OUTPUT_VARIABLE avm_version
    ERROR_QUIET
    RESULT_VARIABLE version_check_result)

  if(${version_check_result} EQUAL 0)
    string(STRIP "${avm_version}" avm_version)

    # Remove the optional prefix before version.
    string(FIND "${avm_version}" "-v" dash_pos)
    if(NOT ${dash_pos} EQUAL -1)
      math(EXPR start_pos "${dash_pos}+1")
      string(SUBSTRING "${avm_version}" ${start_pos} -1 avm_version)
    endif()

    # Remove the leading 'v' from the version string.
    string(FIND "${avm_version}" "v" v_pos)
    if(${v_pos} EQUAL 0)
      string(SUBSTRING "${avm_version}" 1 -1 avm_version)
    endif()
  else()
    set(avm_version "")
  endif()
endif()

if("${avm_version}" STREQUAL "")
  set(avm_version "${AVM_ROOT}/CHANGELOG")
endif()

unset(last_avm_version)
set(version_file "${AVM_CONFIG_DIR}/config/avm_version.h")
if(EXISTS "${version_file}")
  extract_version_string("${version_file}" last_avm_version)
  if("${avm_version}" MATCHES "CHANGELOG$")
    set(avm_version "${last_avm_version}")
  endif()
endif()

if(NOT "${avm_version}" STREQUAL "${last_avm_version}")
  # TODO(tomfinegan): Perl dependency is unnecessary. CMake can do everything
  # that is done by version.pl on its own (if a bit more verbosely...).
  execute_process(
    COMMAND
      ${PERL_EXECUTABLE} "${AVM_ROOT}/build/cmake/version.pl"
      --version_data=${avm_version} --version_filename=${version_file} VERBATIM)
endif()
