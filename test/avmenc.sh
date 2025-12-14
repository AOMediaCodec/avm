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
## This file tests avmenc using hantro_collage_w352h288.yuv as input. To add
## new tests to this file, do the following:
##   1. Write a shell function (this is your test).
##   2. Add the function to avmenc_tests (on a new line).
##
. $(dirname $0)/tools_common.sh

# Environment check: Make sure input is available.
avmenc_verify_environment() {
  if [ ! -e "${YUV_RAW_INPUT}" ]; then
    elog "The file ${YUV_RAW_INPUT##*/} must exist in LIBAVM_TEST_DATA_PATH."
    return 1
  fi
  if [ "$(avmenc_can_encode_av2)" = "yes" ]; then
    if [ ! -e "${Y4M_NOSQ_PAR_INPUT}" ]; then
      elog "The file ${Y4M_NOSQ_PAR_INPUT##*/} must exist in"
      elog "LIBAVM_TEST_DATA_PATH."
      return 1
    fi
  fi
  if [ -z "$(avm_tool_path avmenc)" ]; then
    elog "avmenc not found. It must exist in LIBAVM_BIN_PATH or its parent."
    return 1
  fi
}

avmenc_can_encode_av2() {
  if [ "$(av2_encode_available)" = "yes" ]; then
    echo yes
  fi
}

avmenc_can_encode_av2() {
  if [ "$(av2_encode_available)" = "yes" ]; then
    echo yes
  fi
}

# Utilities that echo avmenc input file parameters.
y4m_input_non_square_par() {
  echo ""${Y4M_NOSQ_PAR_INPUT}""
}

y4m_input_720p() {
  echo ""${Y4M_720P_INPUT}""
}

# Wrapper function for running avmenc with pipe input. Requires that
# LIBAVM_BIN_PATH points to the directory containing avmenc. $1 is used as the
# input file path and shifted away. All remaining parameters are passed through
# to avmenc.
avmenc_pipe() {
  local encoder="$(avm_tool_path avmenc)"
  local input="$1"
  shift
  cat "${input}" | eval "${AVM_TEST_PREFIX}" "${encoder}" - \
    --test-decode=fatal \
    "$@" ${devnull}
}

# Wrapper function for running avmenc. Requires that LIBAVM_BIN_PATH points to
# the directory containing avmenc. $1 one is used as the input file path and
# shifted away. All remaining parameters are passed through to avmenc.
avmenc() {
  local encoder="$(avm_tool_path avmenc)"
  local input="$1"
  shift
  eval "${AVM_TEST_PREFIX}" "${encoder}" "${input}" \
    --test-decode=fatal \
    "$@" ${devnull}
}

avmenc_av2_ivf() {
  if [ "$(avmenc_can_encode_av2)" = "yes" ]; then
    local output="${AV2_IVF_FILE}"
    if [ -e "${AV2_IVF_FILE}" ]; then
      output="${AVM_TEST_OUTPUT_DIR}/av2_test.ivf"
    fi
    avmenc $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --ivf \
      --output="${output}" || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

avmenc_av2_obu() {
   if [ "$(avmenc_can_encode_av2)" = "yes" ]; then
    local output="${AV2_OBU_FILE}"
    if [ -e "${AV2_OBU_FILE}" ]; then
      output="${AVM_TEST_OUTPUT_DIR}/av2_test.obu"
    fi
    avmenc $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --obu \
      --output="${output}" || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

avmenc_av2_obu_lcr_ops_atlas() {
   if [ "$(avmenc_can_encode_av2)" = "yes" ]; then
    local output="${AV2_OBU_LCR_OPS_ATLAS_FILE}"
    if [ -e "${AV2_OBU_LCR_OPS_ATLAS_FILE}" ]; then
      output="${AVM_TEST_OUTPUT_DIR}/av2_test.lcr_ops_atlas.obu"
    fi
    avmenc $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --obu \
      --enable-lcr=1 \
      --enable-ops=1 \
      --enable-atlas=1 \
      --cpu-used=5 \
      --output="${output}" || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

avmenc_av2_webm() {
  if [ "$(avmenc_can_encode_av2)" = "yes" ] && \
     [ "$(webm_io_available)" = "yes" ]; then
    local output="${AV2_WEBM_FILE}"
    if [ -e "${AV2_WEBM_FILE}" ]; then
      output="${AVM_TEST_OUTPUT_DIR}/av2_test.webm"
    fi
    avmenc $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --output="${output}" || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

avmenc_av2_webm_1pass() {
  if [ "$(avmenc_can_encode_av2)" = "yes" ] && \
     [ "$(webm_io_available)" = "yes" ]; then
    local output="${AVM_TEST_OUTPUT_DIR}/av2_test.webm"
    avmenc $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --passes=1 \
      --output="${output}" || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

avmenc_av2_ivf_lossless() {
  if [ "$(avmenc_can_encode_av2)" = "yes" ]; then
    local output="${AVM_TEST_OUTPUT_DIR}/av2_lossless.ivf"
    avmenc $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --ivf \
      --output="${output}" \
      --lossless=1 || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

avmenc_av2_ivf_minq0_maxq0() {
  if [ "$(avmenc_can_encode_av2)" = "yes" ]; then
    local output="${AVM_TEST_OUTPUT_DIR}/av2_lossless_minq0_maxq0.ivf"
    avmenc $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --ivf \
      --output="${output}" \
      --disable-warning-prompt \
      --min-qp=0 \
      --max-qp=0 || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

avmenc_av2_webm_lag5_frames10() {
  if [ "$(avmenc_can_encode_av2)" = "yes" ] && \
     [ "$(webm_io_available)" = "yes" ]; then
    local lag_total_frames=10
    local lag_frames=5
    local output="${AVM_TEST_OUTPUT_DIR}/av2_lag5_frames10.webm"
    avmenc $(yuv_raw_input) \
      $(avmenc_encode_test_fast_params) \
      --limit=${lag_total_frames} \
      --lag-in-frames=${lag_frames} \
      --output="${output}" || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

# TODO(fgalligan): Test that DisplayWidth is different than video width.
avmenc_av2_webm_non_square_par() {
  if [ "$(avmenc_can_encode_av2)" = "yes" ] && \
     [ "$(webm_io_available)" = "yes" ]; then
    local output="${AVM_TEST_OUTPUT_DIR}/av2_non_square_par.webm"
    avmenc $(y4m_input_non_square_par) \
      $(avmenc_encode_test_fast_params) \
      --output="${output}" || return 1

    if [ ! -e "${output}" ]; then
      elog "Output file does not exist."
      return 1
    fi
  fi
}

avmenc_av2_webm_cdf_update_mode() {
  if [ "$(avmenc_can_encode_av2)" = "yes" ] && \
     [ "$(webm_io_available)" = "yes" ]; then
    for mode in 0 1 2; do
      local output="${AVM_TEST_OUTPUT_DIR}/cdf_mode_${mode}.webm"
      avmenc $(yuv_raw_input) \
        $(avmenc_encode_test_fast_params) \
        --cdf-update-mode=${mode} \
        --output="${output}" || return 1

      if [ ! -e "${output}" ]; then
        elog "Output file does not exist."
        return 1
      fi
    done
  fi
}

avmenc_tests="avmenc_av2_ivf
              avmenc_av2_obu
              avmenc_av2_obu_lcr_ops_atlas
              avmenc_av2_webm
              avmenc_av2_webm_1pass
              avmenc_av2_ivf_lossless
              avmenc_av2_ivf_minq0_maxq0
              avmenc_av2_webm_lag5_frames10
              avmenc_av2_webm_non_square_par
              avmenc_av2_webm_cdf_update_mode"

run_tests avmenc_verify_environment "${avmenc_tests}"
