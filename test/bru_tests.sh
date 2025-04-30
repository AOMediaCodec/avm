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
## This file tests the libaom bru_encoder_decoder example. To add new tests to this
## file, do the following:
##   1. Write a shell function (this is your test).
##   2. Add the function to bru_tests (on a new line).
##
. $(dirname $0)/tools_common.sh
BRU_RAW_INPUT="Debugging_480x270p3000_yuv420p_20frames.yuv"
BRU_RAW_INPUT="${LIBAOM_TEST_DATA_PATH}/${BRU_RAW_INPUT}"
BRU_RAW_INPUT_WIDTH=480
BRU_RAW_INPUT_HEIGHT=270

#Environment check : $BRU_RAW_INPUT is required.
bru_tests_verify_environment() {
  if [ ! -e "${BRU_RAW_INPUT}" ]; then
    echo "Libaom test data must exist in LIBAOM_TEST_DATA_PATH."
    return 1
  fi
}

#Runs BRU tests using the codec with a frame limit of 100.
bru_tests() {
  local encoder_decoder="${LIBAOM_BIN_PATH}/bru_encoder_decoder${AOM_TEST_EXE_SUFFIX}"
  local output_md5_1_opt="${AOM_TEST_OUTPUT_DIR}/bru_encoder_decoder_1_opt.md5"
  local output_md5_1_reg="${AOM_TEST_OUTPUT_DIR}/bru_encoder_decoder_1_reg.md5"
  local output_bin_1="${AOM_TEST_OUTPUT_DIR}/bru_encoder_decoder_1.bin"
  local output_md5_0_opt="${AOM_TEST_OUTPUT_DIR}/bru_encoder_decoder_0_opt.md5"
  local output_md5_0_reg="${AOM_TEST_OUTPUT_DIR}/bru_encoder_decoder_0_reg.md5"
  local output_bin_0="${AOM_TEST_OUTPUT_DIR}/bru_encoder_decoder_0.bin"

  if [ ! -x "${encoder_decoder}" ]; then
    elog "${encoder_decoder} does not exist or is not executable."
    return 1
  fi

  eval "${AOM_TEST_PREFIX}" "${encoder_decoder}" "${BRU_RAW_INPUT_WIDTH}" \
    "${BRU_RAW_INPUT_HEIGHT}" 8 1 "${BRU_RAW_INPUT}" "${output_bin_1}" \
    "${output_md5_1_opt}" "${output_md5_1_reg}" \
    ${devnull} || return 1
  [ -e "${output_md5_1_opt}" ] || return 1
  [ -e "${output_md5_1_reg}" ] || return 1
  #compare md5 of enable - bru = 1
  awk 'NR==FNR{a[$0];next}(!($0 in a)){print}' ${output_md5_1_opt} ${output_md5_1_reg} || return 1
  if [ $? -ne 0 ]; then
    return 1
  fi

  eval "${AOM_TEST_PREFIX}" "${encoder_decoder}" "${BRU_RAW_INPUT_WIDTH}" \
    "${BRU_RAW_INPUT_HEIGHT}" 8 0 "${BRU_RAW_INPUT}" "${output_bin_0}" \
    "${output_md5_0_opt}" "${output_md5_0_reg}" \
    ${devnull} || return 1

  [ -e "${output_md5_0_opt}" ] || return 1
  [ -e "${output_md5_0_reg}" ] || return 1
  #compare md5 of enable - bru = 0
  awk 'NR==FNR{a[$0];next}(!($0 in a)){print}' ${output_md5_0_opt} ${output_md5_0_reg} || return 1
  if [ $? -ne 0 ]; then
    return 1
  fi
}

bru_tests_av2() {
  if [ "$(av1_encode_available)" = "yes" -a "$(av1_decode_available)" = "yes" ]; then
    bru_tests || return 1
  fi
}

bru_tests = "bru_tests_av2"

run_tests bru_tests_verify_environment "${bru_tests}"
