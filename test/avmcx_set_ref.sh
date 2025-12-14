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
## This file tests the libavm avm_cx_set_ref example. To add new tests to this
## file, do the following:
##   1. Write a shell function (this is your test).
##   2. Add the function to avm_cx_set_ref_tests (on a new line).
##
. $(dirname $0)/tools_common.sh

# Environment check: $YUV_RAW_INPUT is required.
avm_cx_set_ref_verify_environment() {
  if [ ! -e "${YUV_RAW_INPUT}" ]; then
    echo "Libavm test data must exist in LIBAVM_TEST_DATA_PATH."
    return 1
  fi
}

# Runs avm_cx_set_ref and updates the reference frame before encoding frame 90.
# $1 is the codec name, which avm_cx_set_ref does not support at present: It's
# currently used only to name the output file.
# TODO(tomfinegan): Pass the codec param once the example is updated to support
# AV2.
avm_set_ref() {
  local encoder="${LIBAVM_BIN_PATH}/avm_cx_set_ref${AVM_TEST_EXE_SUFFIX}"
  local codec="$1"
  local output_file="${AVM_TEST_OUTPUT_DIR}/avm_cx_set_ref_${codec}.ivf"
  local ref_frame_num=4
  local limit=10
  if [ ! -x "${encoder}" ]; then
    elog "${encoder} does not exist or is not executable."
    return 1
  fi

  eval "${AVM_TEST_PREFIX}" "${encoder}" "${codec}" "${YUV_RAW_INPUT_WIDTH}" \
      "${YUV_RAW_INPUT_HEIGHT}" "${YUV_RAW_INPUT}" "${output_file}" \
      "${ref_frame_num}" "${limit}" ${devnull} || return 1

  [ -e "${output_file}" ] || return 1
}

avm_cx_set_ref_av2() {
  if [ "$(av2_encode_available)" = "yes" ]; then
    avm_set_ref av2 || return 1
  fi
}

avm_cx_set_ref_tests="avm_cx_set_ref_av2"

run_tests avm_cx_set_ref_verify_environment "${avm_cx_set_ref_tests}"

