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
## This file tests the libaom bridge frame.
##
. $(dirname $0)/tools_common.sh

Y4M_720P_INPUT_FILE="niklas_1280_720_30.y4m"
Y4M_720P_INPUT="${LIBAOM_TEST_DATA_PATH}/${Y4M_720P_INPUT_FILE}"

#Environment check : $bridge_frame_RAW_INPUT is required.
bridge_frame_tests_verify_environment() {
  if [ ! -e "${Y4M_720P_INPUT}" ]; then
    elog "Libaom test data must exist in LIBAOM_TEST_DATA_PATH."
    return 1
  fi
}

#Runs bridge_frame tests using the codec with a frame limit 3
bridge_frame_tests() {
  local encoder="$(aom_tool_path aomenc)"
  local decoder="$(aom_tool_path aomdec)"
  local bridge_frame_total_frames=3
  local output_binary="${AOM_TEST_OUTPUT_DIR}/bridge_frame.bin"
  local encoder_log="${AOM_TEST_OUTPUT_DIR}/bridge_frame_encoder.log"
  local decoder_log="${AOM_TEST_OUTPUT_DIR}/bridge_frame_decoder.log"
  local resizemode=5
  local qp=160

  eval "${encoder}" "${Y4M_720P_INPUT}" --cpu-used=5 \
    --passes=1 --lag-in-frames=0 \
    --min-gf-interval=16 --max-gf-interval=16 \
    --gf-min-pyr-height=4 --gf-max-pyr-height=4 \
    --limit="${bridge_frame_total_frames}" \
    --kf-min-dist=9999 --kf-max-dist=9999 \
    --use-fixed-qp-offsets=1 --deltaq-mode=0 \
    --enable-tpl-model=0 --end-usage=q --qp="${qp}" \
    --subgop-config-str=ld --enable-keyframe-filtering=0 \
    --obu --resize-mode="${resizemode}" \
    --test-decode=fatal \
    --tile-rows=0 --tile-columns=1 \
    --output="${output_binary}" > ${encoder_log} || return 1

  if [ ! -e "${output_binary}" ]; then
    elog "Bridge frame output binary file does not exist."
    return 1
  fi

  if [ ! -e "${encoder_log}" ]; then
    elog "Bridge frame encoder file does not exist."
    return 1
  fi

  local first_resolution="1280x720"
  if ! grep -q "${first_resolution}" "${encoder_log}"; then
    elog "String '${first_resolution}' not found in '${encoder_log}'."
    return 1
  fi

  local second_resolution="640x360"
  if ! grep -q "${second_resolution}" "${encoder_log}"; then
    elog "String '${second_resolution}' not found in '${encoder_log}'."
    return 1
  fi

  local third_resolution="320x180"
  if ! grep -q "${third_resolution}" "${encoder_log}"; then
    elog "String '${third_resolution}' not found in '${encoder_log}'."
    return 1
  fi

  eval "${decoder}" --codec=av1 \
	  --summary -o /dev/null \
	  "${output_binary}" > "${decoder_log}" 2>&1 || return 1

  if [ ! -e "${decoder_log}" ]; then
    elog "Bridge frame decoder file does not exist."
    return 1
  fi

  local decoded_frames="2 decoded frames"
  if ! grep -q "${decoded_frames}" "${decoder_log}"; then
    elog "String '${decoded_frames}' not found in '${decoder_log}'."
    return 1
  fi

  local decoded_frames="2 showed frames"
  if ! grep -q "${decoded_frames}" "${decoder_log}"; then
    elog "String '${decoded_frames}' not found in '${decoder_log}'."
    return 1
  fi

}


bridge_frame_tests_av2() {
  if [ "$(av1_encode_available)" = "yes" -a "$(av1_decode_available)" = "yes" ]; then
    bridge_frame_tests || return 1
  fi
}

bridge_frame_tests = "bridge_frame_tests_av2"

run_tests bridge_frame_tests_verify_environment "${bridge_frame_tests}"
