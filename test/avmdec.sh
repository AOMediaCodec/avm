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
## This file tests avmdec. To add new tests to this file, do the following:
##   1. Write a shell function (this is your test).
##   2. Add the function to avmdec_tests (on a new line).
##
. $(dirname $0)/tools_common.sh

# Environment check: Make sure input is available.
avmdec_verify_environment() {
  if [ "$(av2_encode_available)" != "yes" ] ; then
    if [ ! -e "${AV2_IVF_FILE}" ] || \
       [ ! -e "${AV2_OBU_FILE}" ] || \
       [ ! -e "${AV2_WEBM_FILE}" ]; then
      elog "Libavm test data must exist before running this test script when " \
           " encoding is disabled. "
      return 1
    fi
  fi
  if [ -z "$(avm_tool_path avmdec)" ]; then
    elog "avmdec not found. It must exist in LIBAVM_BIN_PATH or its parent."
    return 1
  fi
}

# Wrapper function for running avmdec with pipe input. Requires that
# LIBAVM_BIN_PATH points to the directory containing avmdec. $1 is used as the
# input file path and shifted away. All remaining parameters are passed through
# to avmdec.
avmdec_pipe() {
  local input="$1"
  shift
  if [ ! -e "${input}" ]; then
    elog "Input file ($input) missing in avmdec_pipe()"
    return 1
  fi
  cat "${file}" | avmdec - "$@" ${devnull}
}


# Wrapper function for running avmdec. Requires that LIBAVM_BIN_PATH points to
# the directory containing avmdec. $1 one is used as the input file path and
# shifted away. All remaining parameters are passed through to avmdec.
avmdec() {
  local decoder="$(avm_tool_path avmdec)"
  local input="$1"
  shift
  eval "${AVM_TEST_PREFIX}" "${decoder}" "$input" "$@" ${devnull}
}

avmdec_can_decode_av2() {
  if [ "$(av2_decode_available)" = "yes" ]; then
    echo yes
  fi
}

avmdec_av2_ivf() {
  if [ "$(avmdec_can_decode_av2)" = "yes" ]; then
    local file="${AV2_IVF_FILE}"
    if [ ! -e "${file}" ]; then
      encode_yuv_raw_input_av2 "${file}" --ivf || return 1
    fi
    avmdec "${AV2_IVF_FILE}" --summary --noblit
  fi
}

avmdec_av2_ivf_error_resilient() {
  if [ "$(avmdec_can_decode_av2)" = "yes" ]; then
    local file="av2.error-resilient.ivf"
    if [ ! -e "${file}" ]; then
      encode_yuv_raw_input_av2 "${file}" --ivf --global-error-resilient=1 || return 1
    fi
    avmdec "${file}" --summary --noblit
  fi
}

avmdec_av2_ivf_multithread() {
  if [ "$(avmdec_can_decode_av2)" = "yes" ]; then
    local file="${AV2_IVF_FILE}"
    if [ ! -e "${file}" ]; then
      encode_yuv_raw_input_av2 "${file}" --ivf || return 1
    fi
    for threads in 2 3 4 5 6 7 8; do
      avmdec "${file}" --summary --noblit --threads=$threads || return 1
    done
  fi
}

avmdec_avm_ivf_pipe_input() {
  if [ "$(avmdec_can_decode_av2)" = "yes" ]; then
    local file="${AV2_IVF_FILE}"
    if [ ! -e "${file}" ]; then
      encode_yuv_raw_input_av2 "${file}" --ivf || return 1
    fi
    avmdec_pipe "${AV2_IVF_FILE}" --summary --noblit
  fi
}

avmdec_av2_obu() {
  if [ "$(avmdec_can_decode_av2)" = "yes" ]; then
    local file="${AV2_OBU_FILE}"
    if [ ! -e "${file}" ]; then
      encode_yuv_raw_input_av2 "${file}" --obu || return 1
    fi
    avmdec "${file}" --summary --noblit
  fi
}

avmdec_av2_webm() {
  if [ "$(avmdec_can_decode_av2)" = "yes" ] && \
     [ "$(webm_io_available)" = "yes" ]; then
    local file="${AV2_WEBM_FILE}"
    if [ ! -e "${file}" ]; then
      encode_yuv_raw_input_av2 "${file}" || return 1
    fi
    avmdec "${AV2_WEBM_FILE}" --summary --noblit
  fi
}

avmdec_tests="avmdec_av2_ivf
              avmdec_av2_ivf_error_resilient
              avmdec_av2_ivf_multithread
              avmdec_avm_ivf_pipe_input
              avmdec_av2_obu
              avmdec_av2_webm"

run_tests avmdec_verify_environment "${avmdec_tests}"
