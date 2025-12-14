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
include("${AVM_ROOT}/test/test_data_util.cmake")

# https://github.com/cheshirekow/cmake_format/issues/34
# cmake-format: off
if (NOT AVM_ROOT OR NOT AVM_CONFIG_DIR OR NOT AVM_TEST_FILE
    OR NOT AVM_TEST_CHECKSUM)
  message(FATAL_ERROR
          "AVM_ROOT, AVM_CONFIG_DIR, AVM_TEST_FILE and AVM_TEST_CHECKSUM must be
          defined.")
endif ()
# cmake-format: on

set(AVM_TEST_DATA_URL "https://storage.googleapis.com/aom-test-data")

if(NOT AVM_TEST_DATA_PATH)
  set(AVM_TEST_DATA_PATH "$ENV{LIBAVM_TEST_DATA_PATH}")
endif()

if("${AVM_TEST_DATA_PATH}" STREQUAL "")
  message(
    WARNING "Writing test data to ${AVM_CONFIG_DIR}, set "
            "$LIBAVM_TEST_DATA_PATH in your environment to avoid this warning.")
  set(AVM_TEST_DATA_PATH "${AVM_CONFIG_DIR}")
endif()

if(NOT EXISTS "${AVM_TEST_DATA_PATH}")
  file(MAKE_DIRECTORY "${AVM_TEST_DATA_PATH}")
endif()

expand_test_file_paths("AVM_TEST_FILE" "${AVM_TEST_DATA_PATH}" "filepath")
expand_test_file_paths("AVM_TEST_FILE" "${AVM_TEST_DATA_URL}" "url")

check_file("${filepath}" "${AVM_TEST_CHECKSUM}" "needs_download")
if(needs_download)
  download_test_file("${url}" "${AVM_TEST_CHECKSUM}" "${filepath}")
endif()
