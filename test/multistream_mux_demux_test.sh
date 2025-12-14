#!/bin/bash
#
# Script to encode two bitstreams, mux them, and demux the muxed bitstream
#

. $(dirname $0)/tools_common.sh

# Input video file

# Generated bitstream files
readonly BITSTREAM_0="${AVM_TEST_OUTPUT_DIR}/bitstream_0.bin"
readonly BITSTREAM_1="${AVM_TEST_OUTPUT_DIR}/bitstream_1.bin"

# Muxed/Demuxed bitstream files
readonly MUXED_OUTPUT="${AVM_TEST_OUTPUT_DIR}/bitstream_muxed_01.bin"
readonly DEMUXED_OUTPUT="${AVM_TEST_OUTPUT_DIR}/bitstream_demuxed.bin"
readonly DEMUXED_OUTPUT_0="${AVM_TEST_OUTPUT_DIR}/bitstream_demuxed_0.bin"
readonly DEMUXED_OUTPUT_1="${AVM_TEST_OUTPUT_DIR}/bitstream_demuxed_1.bin"

# Verify environment and prerequisites
mux_demux_verify_environment() {
  if [ ! -e "${YUV_RAW_INPUT}" ]; then
    elog "The file ${YUV_RAW_INPUT##*/} must exist in LIBAVM_TEST_DATA_PATH."
    return 1
  fi

  if [ -z "$(avm_tool_path avmenc)" ]; then
    elog "avmenc not found in LIBAVM_BIN_PATH or tools/."
    return 1
  fi

  if [ -z "$(avm_tool_path stream_multiplexer)" ]; then
    elog "stream_multiplexer not found in LIBAVM_BIN_PATH or tools/."
    return 1
  fi

  if [ -z "$(avm_tool_path stream_demuxer)" ]; then
    elog "stream_demuxer not found in LIBAVM_BIN_PATH or tools/."
    return 1
  fi
}

# Encode first bitstream
encode_bitstream_0() {
  local encoder="$(avm_tool_path avmenc)"

  eval "${encoder}" \
    $(avmenc_encode_test_fast_params) \
    $(yuv_raw_input) \
    --obu \
    --output=${BITSTREAM_0} \
    ${devnull} || return 1

  if [ ! -e "${BITSTREAM_0}" ]; then
    elog "Encoding bitstream_0 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_0.bin"
}

# Encode second bitstream
encode_bitstream_1() {
  local encoder="$(avm_tool_path avmenc)"

  eval "${encoder}" \
    $(avmenc_encode_test_fast_params) \
    $(yuv_raw_input) \
    --obu \
    --output=${BITSTREAM_1} \
    ${devnull} || return 1

  if [ ! -e "${BITSTREAM_1}" ]; then
    elog "Encoding bitstream_1 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_1.bin"
}

# Decode the first bitstream
decode_bitstream_0() {
  local decoder="$(avm_tool_path avmdec)"
  local output_file="${AVM_TEST_OUTPUT_DIR}/decoded_seq_0"

  eval "${decoder}" -o "${output_file}" \
    "${BITSTREAM_0}" --md5 || return 1

  if [ ! -e "${output_file}" ]; then
    elog "Decoding bitstream_0.bin failed."
    return 1
  fi

  echo "Successfully decoded bitstream_0.bin"
}

# Decode the second bitstream
decode_bitstream_1() {
  local decoder="$(avm_tool_path avmdec)"
  local output_file="${AVM_TEST_OUTPUT_DIR}/decoded_seq_1"

  eval "${decoder}" -o "${output_file}" \
    "${BITSTREAM_1}" --md5 || return 1

  if [ ! -e "${output_file}" ]; then
    elog "Decoding bitstream_1.bin failed."
    return 1
  fi

  echo "Successfully decoded bitstream_1.bin"
}

# Decode the muxed bitstream
decode_muxed_bitstream() {
  local decoder="$(avm_tool_path avmdec)"
  local output_file="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed"
  local output_file_0="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_0"
  local output_file_1="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_1"

  eval "${decoder}" -o "${output_file}" \
    "${MUXED_OUTPUT}" --num-streams=2 --md5 || return 1

  if [ ! -e "${output_file_0}" ]; then
    elog "Decoding bitstream_muxed_01.bin failed."
    return 1
  fi

  if [ ! -e "${output_file_1}" ]; then
    elog "Decoding bitstream_muxed_01.bin failed."
    return 1
  fi

  echo "Successfully decoded bitstream_muxed_01.bin"
}

# Mux the bitstreams
mux_bitstreams() {
  local multiplexer="$(avm_tool_path stream_multiplexer)"

  eval "${multiplexer}" \
    "${BITSTREAM_0}" 0 1 \
    "${BITSTREAM_1}" 1 1 \
    "${MUXED_OUTPUT}" \
    ${devnull} || return 1

  if [ ! -e "${MUXED_OUTPUT}" ]; then
    elog "Bitstream muxing failed."
    return 1
  fi

  echo "Successfully muxed bitstreams to bitstream_muxed_01.bin"
}

# Demux the muxed bitstream
demux_bitstream() {
  local demultiplexer="$(avm_tool_path stream_demuxer)"

  # Demux to first output
  eval "${demultiplexer}" \
    "${MUXED_OUTPUT}" \
    "${DEMUXED_OUTPUT}" \
    ${devnull} || return 1

  if [ ! -e "${DEMUXED_OUTPUT_0}" ]; then
    elog "Bitstream demuxing to output 0 failed."
    return 1
  fi

  echo "Successfully demuxed bitstream to bitstream_demuxed_0.bin"

  if [ ! -e "${DEMUXED_OUTPUT_1}" ]; then
    elog "Bitstream demuxing to output 1 failed."
    return 1
  fi

  echo "Successfully demuxed bitstream to bitstream_demuxed_1.bin"
}

# Compare demuxed bitstreams with original bitstreams
compare_bitstreams() {
  echo "Comparing demuxed bitstreams with original bitstreams..."

  # Compare first bitstream
  if cmp -s "${BITSTREAM_0}" "${DEMUXED_OUTPUT_0}"; then
    echo "PASS: bitstream_0.bin matches bitstream_demuxed_0.bin"
  else
    elog "FAIL: bitstream_0.bin does NOT match bitstream_demuxed_0.bin"
    return 1
  fi

  # Compare second bitstream
  if cmp -s "${BITSTREAM_1}" "${DEMUXED_OUTPUT_1}"; then
    echo "PASS: bitstream_1.bin matches bitstream_demuxed_1.bin"
  else
    elog "FAIL: bitstream_1.bin does NOT match bitstream_demuxed_1.bin"
    return 1
  fi

  echo "All bitstream comparisons passed successfully!"
}

# Compare demuxed bitstreams with original bitstreams
compare_md5() {
  local output_file_0="${AVM_TEST_OUTPUT_DIR}/decoded_seq_0"
  local output_file_1="${AVM_TEST_OUTPUT_DIR}/decoded_seq_1"
  local output_file_demuxed_0="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_0"
  local output_file_demuxed_1="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_1"

  echo "Comparing demuxed decoded sequences with original decoded sequences..."

  # Compare first output seq
  if cmp -s "${output_file_0}" "${output_file_demuxed_0}"; then
    echo "PASS: decoded_seq_0 matches decoded_seq_demuxed_0"
  else
    elog "FAIL: decoded_seq_0 does NOT match decoded_seq_demuxed_0"
    return 1
  fi

  # Compare second output seq
  if cmp -s "${output_file_1}" "${output_file_demuxed_1}"; then
    echo "PASS: decoded_seq_1 matches decoded_seq_demuxed_1"
  else
    elog "FAIL: decoded_seq_1 does NOT match decoded_seq_demuxed_1"
    return 1
  fi

  echo "All decoded sequences comparisons passed successfully!"
}

# Run complete encode, mux, and demux pipeline
run_encode_mux_demux() {
  encode_bitstream_0 || return 1
  encode_bitstream_1 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1
}

# Test list
mux_demux_tests="run_encode_mux_demux"

# Execute tests
run_tests mux_demux_verify_environment "${mux_demux_tests}"