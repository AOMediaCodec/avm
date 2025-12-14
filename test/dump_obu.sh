#!/bin/sh
## Copyright (c) 2021, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
## This file tests the libavm dump_obu tool. To add new tests to this
## file, do the following:
##   1. Write a shell function (this is your test).
##   2. Add the function to dump_obu_tests (on a new line).
##
. $(dirname $0)/tools_common.sh

readonly dump_obu_test_file="${AVM_TEST_OUTPUT_DIR}/av2_obu_test.ivf"

dump_obu_verify_environment() {
  if [ ! -e "${YUV_RAW_INPUT}" ]; then
    elog "The file ${YUV_RAW_INPUT##*/} must exist in LIBAVM_TEST_DATA_PATH."
    return 1
  fi
  if [ "$(dump_obu_available)" = "yes" ]; then
    if [ -z "$(avm_tool_path dump_obu)" ]; then
      elog "dump_obu not found in LIBAVM_BIN_PATH, its parent, or child tools/."
    fi
  fi
}

dump_obu_available() {
  if [ "$(av2_decode_available)" = "yes" ] && \
     [ "$(av2_encode_available)" = "yes" ]; then
    echo yes
  fi
}

avmenc_available() {
  if [ -x "$(avm_tool_path avmenc)" ]; then
    echo yes
  fi
}

encode_test_file() {
  if [ "$(avmenc_available)" = "yes" ]; then
    local encoder="$(avm_tool_path avmenc)"

    eval "${encoder}" \
      $(avmenc_encode_test_fast_params) \
      $(yuv_raw_input) \
      --ivf \
      --output=${dump_obu_test_file} \
      ${devnull} || return 1

    if [ ! -e "${dump_obu_test_file}" ]; then
      elog "dump_obu test input encode failed."
      return 1
    fi
  fi
}

dump_obu() {
  encode_test_file || return 1
  eval $(avm_tool_path dump_obu) "${dump_obu_test_file}" ${devnull}
}

dump_obu_tests="dump_obu"

run_tests dump_obu_verify_environment "${dump_obu_tests}"
