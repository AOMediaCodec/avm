#!/bin/sh
## Copyright (c) 2023, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
## This file tests extract_proto. To add new tests to this file, do the following:
##   1. Write a shell function (this is your test).
##   2. Add the function to extract_proto_tests (on a new line).
##
. $(dirname $0)/tools_common.sh

# Environment check: Make sure input is available.
extract_proto_verify_environment() {
  if [ "$(av2_encode_available)" != "yes" ] ; then
    if [ ! -e "${AV2_IVF_FILE}" ] || \
       [ ! -e "${AV2_OBU_FILE}" ] || \
       [ ! -e "${AV2_WEBM_FILE}" ]; then
      elog "Libavm test data must exist before running this test script when " \
           " encoding is disabled. "
      return 1
    fi
  fi
  if [ -z "$(avm_tool_path extract_proto)" ]; then
    elog "extract_proto not found. It must exist in LIBAVM_BIN_PATH or its parent."
    return 1
  fi
}



# Wrapper function for running extract_proto. Requires that LIBAVM_BIN_PATH points to
# the directory containing extract_proto. $1 one is used as the input file path and
# shifted away. All remaining parameters are passed through to extract_proto.
extract_proto() {
  local decoder="$(avm_tool_path extract_proto)"
  local input="$1"
  shift
  eval "${AVM_TEST_PREFIX}" "${decoder}" "$input" "$@" ${devnull}
}

extract_proto_can_decode_av2() {
  if [ "$(av2_decode_available)" = "yes" ]; then
    echo yes
  fi
}

extract_proto_av2_ivf() {
  if [ "$(extract_proto_can_decode_av2)" = "yes" ]; then
    local file="${AV2_IVF_FILE}"
    if [ ! -e "${file}" ]; then
      echo "Encoding: ${file}"
      encode_yuv_raw_input_av2 "${file}" --ivf || return 1
    fi
    extract_proto --stream "${AV2_IVF_FILE}" --output_folder "${AVM_TEST_OUTPUT_DIR}"
    num_protos=$(ls -1q "${AVM_TEST_OUTPUT_DIR}"/*.pb | wc -l)
    if [ $num_protos -eq ${AV2_ENCODE_TEST_FRAME_LIMIT} ]; then
      return 0;
    else
      return 1;
    fi
  fi
}

extract_proto_tests="extract_proto_av2_ivf"

run_tests extract_proto_verify_environment "${extract_proto_tests}"
