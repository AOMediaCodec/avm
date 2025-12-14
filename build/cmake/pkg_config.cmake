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

set(REQUIRED_ARGS
    "AVM_ROOT"
    "AVM_CONFIG_DIR"
    "CMAKE_INSTALL_PREFIX"
    "CMAKE_INSTALL_BINDIR"
    "CMAKE_INSTALL_INCLUDEDIR"
    "CMAKE_INSTALL_LIBDIR"
    "CMAKE_PROJECT_NAME"
    "CONFIG_MULTITHREAD"
    "HAVE_PTHREAD_H")

foreach(arg ${REQUIRED_ARGS})
  if("${${arg}}" STREQUAL "")
    message(FATAL_ERROR "${arg} must not be empty.")
  endif()
endforeach()

include("${AVM_ROOT}/build/cmake/util.cmake")

extract_version_string("${AVM_CONFIG_DIR}/config/avm_version.h" avm_version)

# Create a version string suitable for comparison using the RPM version compare
# algorithm: strip out everything after the number.
string(FIND "${avm_version}" "-" dash_pos)
if(${dash_pos} EQUAL -1)
  set(package_version "${avm_version}")
else()
  string(SUBSTRING "${avm_version}" 0 ${dash_pos} package_version)
endif()

# Write pkg-config info.
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(bindir "${CMAKE_INSTALL_BINDIR}")
set(includedir "${CMAKE_INSTALL_INCLUDEDIR}")
set(libdir "${CMAKE_INSTALL_LIBDIR}")
set(pkgconfig_file "${AVM_CONFIG_DIR}/avm.pc")
string(TOLOWER ${CMAKE_PROJECT_NAME} pkg_name)
file(WRITE "${pkgconfig_file}" "# libavm pkg-config.\n")
file(APPEND "${pkgconfig_file}" "prefix=${prefix}\n")
file(APPEND "${pkgconfig_file}" "exec_prefix=\${prefix}\n")
file(APPEND "${pkgconfig_file}" "includedir=\${prefix}/${includedir}\n")
file(APPEND "${pkgconfig_file}" "libdir=\${exec_prefix}/${libdir}\n\n")
file(APPEND "${pkgconfig_file}" "Name: ${pkg_name}\n")
file(
  APPEND "${pkgconfig_file}"
  "Description: Alliance for Open Media AV2 codec library v${avm_version}.\n")
file(APPEND "${pkgconfig_file}" "Version: ${package_version}\n")
file(APPEND "${pkgconfig_file}" "Requires:\n")
file(APPEND "${pkgconfig_file}" "Conflicts:\n")
file(APPEND "${pkgconfig_file}" "Libs: -L\${libdir} -l${pkg_name}\n")
if(CONFIG_MULTITHREAD AND HAVE_PTHREAD_H)
  file(APPEND "${pkgconfig_file}" "Libs.private: -lm -lpthread\n")
else()
  file(APPEND "${pkgconfig_file}" "Libs.private: -lm\n")
endif()
file(APPEND "${pkgconfig_file}" "Cflags: -I\${includedir}\n")
