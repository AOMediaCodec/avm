#!/bin/bash
#
# Script to encode two bitstreams, mux them, and demux the muxed bitstream
#

. $(dirname $0)/tools_common.sh

# Input video file

# Generated bitstream files
readonly BITSTREAM_0="${AOM_TEST_OUTPUT_DIR}/bitstream_0.bin"
readonly BITSTREAM_1="${AOM_TEST_OUTPUT_DIR}/bitstream_1.bin"

# Output filesd
readonly MUXED_OUTPUT="${AOM_TEST_OUTPUT_DIR}/bitstream_muxed_01.bin"
readonly DEMUXED_OUTPUT="${AOM_TEST_OUTPUT_DIR}/bitstream_demuxed.bin"
readonly DEMUXED_OUTPUT_0="${AOM_TEST_OUTPUT_DIR}/bitstream_demuxed_0.bin"
readonly DEMUXED_OUTPUT_1="${AOM_TEST_OUTPUT_DIR}/bitstream_demuxed_1.bin"

# Verify environment and prerequisites
mux_demux_verify_environment() {
  if [ ! -e "${YUV_RAW_INPUT}" ]; then
    elog "The file ${YUV_RAW_INPUT##*/} must exist in LIBAOM_TEST_DATA_PATH."
    return 1
  fi

  if [ -z "$(aom_tool_path aomenc)" ]; then
    elog "aomenc not found in LIBAOM_BIN_PATH or tools/."
    return 1
  fi

  if [ -z "$(aom_tool_path stream_multiplexer)" ]; then
    elog "stream_multiplexer not found in LIBAOM_BIN_PATH or tools/."
    return 1
  fi

  if [ -z "$(aom_tool_path stream_demuxer)" ]; then
    elog "stream_demuxer not found in LIBAOM_BIN_PATH or tools/."
    return 1
  fi
}

# Encode first bitstream
encode_bitstream_0() {
  local encoder="$(aom_tool_path aomenc)"

  eval "${encoder}" \
    $(aomenc_encode_test_fast_params) \
    $(yuv_raw_input) \
    --obu \
    --use-temporal-delimiter=1 \
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
  local encoder="$(aom_tool_path aomenc)"

  eval "${encoder}" \
    $(aomenc_encode_test_fast_params) \
    $(yuv_raw_input) \
    --obu \
    --use-temporal-delimiter=1 \
    --output=${BITSTREAM_1} \
    ${devnull} || return 1

  if [ ! -e "${BITSTREAM_1}" ]; then
    elog "Encoding bitstream_1 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_1.bin"
}

# Mux the bitstreams
mux_bitstreams() {
  local multiplexer="$(aom_tool_path stream_multiplexer)"

  eval "${multiplexer}" \
    "${BITSTREAM_0}" 0 1 \
    "${BITSTREAM_1}" 1 1 \
    "${MUXED_OUTPUT}" \
    ${devnull} || return 1

  if [ ! -e "${MUXED_OUTPUT}" ]; then
    elog "Bitstream muxing failed."
    return 1
  fi

  echo "Successfully muxed bitstreams to bitstream_muxed_0123.bin"
}

# Demux the muxed bitstream
demux_bitstream() {
  local demultiplexer="$(aom_tool_path stream_demuxer)"

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

# Run complete encode, mux, and demux pipeline
run_encode_mux_demux() {
  encode_bitstream_0 || return 1
  encode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
}

# Test list
mux_demux_tests="run_encode_mux_demux"

# Execute tests
run_tests mux_demux_verify_environment "${mux_demux_tests}"