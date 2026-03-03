#!/bin/bash
#
# Script to encode two bitstreams, mux them, and demux the muxed bitstream
#

. $(dirname $0)/tools_common.sh

# Input video file

# Generated bitstream files
readonly BITSTREAM_0="${AVM_TEST_OUTPUT_DIR}/bitstream_0.bin"
readonly BITSTREAM_1="${AVM_TEST_OUTPUT_DIR}/bitstream_1.bin"
readonly BITSTREAM_2="${AVM_TEST_OUTPUT_DIR}/bitstream_2.bin"
readonly BITSTREAM_3="${AVM_TEST_OUTPUT_DIR}/bitstream_3.bin"

# Muxed/Demuxed bitstream files
readonly MUXED_OUTPUT="${AVM_TEST_OUTPUT_DIR}/bitstream_muxed_01.bin"
readonly DEMUXED_OUTPUT="${AVM_TEST_OUTPUT_DIR}/bitstream_demuxed.bin"
readonly DEMUXED_OUTPUT_0="${AVM_TEST_OUTPUT_DIR}/bitstream_demuxed_0.bin"
readonly DEMUXED_OUTPUT_1="${AVM_TEST_OUTPUT_DIR}/bitstream_demuxed_1.bin"
readonly DEMUXED_OUTPUT_2="${AVM_TEST_OUTPUT_DIR}/bitstream_demuxed_2.bin"
readonly DEMUXED_OUTPUT_3="${AVM_TEST_OUTPUT_DIR}/bitstream_demuxed_3.bin"

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
    --limit=$1 \
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
    --limit=$1 \
    --obu \
    --output=${BITSTREAM_1} \
    ${devnull} || return 1

  if [ ! -e "${BITSTREAM_1}" ]; then
    elog "Encoding bitstream_1 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_1.bin"
}

# Encode first bitstream
encode_lag_bitstream_0() {
  local encoder="$(avm_tool_path avmenc)"

  eval "${encoder}" \
    $(avmenc_encode_test_fast_params_lag) \
    $(yuv_raw_input) \
    --limit=$1 \
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
encode_lag_bitstream_1() {
  local encoder="$(avm_tool_path avmenc)"

  eval "${encoder}" \
    $(avmenc_encode_test_fast_params_lag) \
    $(yuv_raw_input) \
    --limit=$1 \
    --obu \
    --output=${BITSTREAM_1} \
    ${devnull} || return 1

  if [ ! -e "${BITSTREAM_1}" ]; then
    elog "Encoding bitstream_1 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_1.bin"
}

# Encode first bitstream for multi_layer encoder.
ml_encode_bitstream_0() {
  local encoder="$(avm_tool_path examples/scalable_encoder)"

  eval "${encoder}" \
      352 288 \
      $(yuv_raw_input_ml) \
      ${BITSTREAM_0} \
      $4 \
      $1 $2 $3 \
      ${devnull} || return 1

  if [ ! -e "${BITSTREAM_0}" ]; then
    elog "Encoding bitstream_0 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_0.bin"
}

# Encode second bitstream for multi_layer encoder.
ml_encode_bitstream_1() {
  local encoder="$(avm_tool_path examples/scalable_encoder)"

 eval "${encoder}" \
      352 288 \
      $(yuv_raw_input_ml) \
      ${BITSTREAM_1} \
      $4 \
      $1 $2 $3 \
      ${devnull} || return 1

  if [ ! -e "${BITSTREAM_1}" ]; then
    elog "Encoding bitstream_1 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_1.bin"
}

# Encode third bitstream for multi_layer encoder.
ml_encode_bitstream_2() {
  local encoder="$(avm_tool_path examples/scalable_encoder)"

 eval "${encoder}" \
      352 288 \
      $(yuv_raw_input_ml) \
      ${BITSTREAM_2} \
      $4 \
      $1 $2 $3 \
      ${devnull} || return 1

  if [ ! -e "${BITSTREAM_2}" ]; then
    elog "Encoding bitstream_2 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_2.bin"
}

# Encode fourth bitstream for multi_layer encoder.
ml_encode_bitstream_3() {
  local encoder="$(avm_tool_path examples/scalable_encoder)"

 eval "${encoder}" \
      352 288 \
      $(yuv_raw_input_ml) \
      ${BITSTREAM_3} \
      $4 \
      $1 $2 $3 \
      ${devnull} || return 1

  if [ ! -e "${BITSTREAM_3}" ]; then
    elog "Encoding bitstream_3 failed."
    return 1
  fi

  echo "Successfully encoded bitstream_3.bin"
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

# Decode the third bitstream
decode_bitstream_2() {
  local decoder="$(avm_tool_path avmdec)"
  local output_file="${AVM_TEST_OUTPUT_DIR}/decoded_seq_2"

  eval "${decoder}" -o "${output_file}" \
    "${BITSTREAM_2}" --md5 || return 1

  if [ ! -e "${output_file}" ]; then
    elog "Decoding bitstream_2.bin failed."
    return 1
  fi

  echo "Successfully decoded bitstream_2.bin"
}

# Decode the fourth bitstream
decode_bitstream_3() {
  local decoder="$(avm_tool_path avmdec)"
  local output_file="${AVM_TEST_OUTPUT_DIR}/decoded_seq_3"

  eval "${decoder}" -o "${output_file}" \
    "${BITSTREAM_3}" --md5 || return 1

  if [ ! -e "${output_file}" ]; then
    elog "Decoding bitstream_3.bin failed."
    return 1
  fi

  echo "Successfully decoded bitstream_3.bin"
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

# Decode the muxed bitstream
decode_muxed_bitstream4() {
  local decoder="$(avm_tool_path avmdec)"
  local output_file="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed"
  local output_file_0="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_0"
  local output_file_1="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_1"
  local output_file_2="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_2"
  local output_file_3="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_3"

  eval "${decoder}" -o "${output_file}" \
    "${MUXED_OUTPUT}" --num-streams=4 --md5 || return 1

  if [ ! -e "${output_file_0}" ]; then
    elog "Decoding bitstream_muxed_01.bin failed."
    return 1
  fi

  if [ ! -e "${output_file_1}" ]; then
    elog "Decoding bitstream_muxed_01.bin failed."
    return 1
  fi

  if [ ! -e "${output_file_2}" ]; then
    elog "Decoding bitstream_muxed_01.bin failed."
    return 1
  fi

  if [ ! -e "${output_file_3}" ]; then
    elog "Decoding bitstream_muxed_01.bin failed."
    return 1
  fi

  echo "Successfully decoded bitstream_muxed_01.bin"
}

# Mux the two bitstreams
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

# Mux the four bitstreams
mux_bitstreams4() {
  local multiplexer="$(avm_tool_path stream_multiplexer)"

  eval "${multiplexer}" \
    "${BITSTREAM_0}" 0 1 \
    "${BITSTREAM_1}" 1 1 \
    "${BITSTREAM_2}" 2 1 \
    "${BITSTREAM_3}" 3 1 \
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

# Demux the muxed bitstream for 4 input streams.
demux_bitstream4() {
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

   if [ ! -e "${DEMUXED_OUTPUT_2}" ]; then
    elog "Bitstream demuxing to output 2 failed."
    return 1
  fi

  echo "Successfully demuxed bitstream to bitstream_demuxed_2.bin"

   if [ ! -e "${DEMUXED_OUTPUT_3}" ]; then
    elog "Bitstream demuxing to output 3 failed."
    return 1
  fi

  echo "Successfully demuxed bitstream to bitstream_demuxed_3.bin"
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

# Compare demuxed bitstreams with original bitstreams, for 4 input streams.
compare_bitstreams4() {
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

  # Compare second bitstream
  if cmp -s "${BITSTREAM_2}" "${DEMUXED_OUTPUT_2}"; then
    echo "PASS: bitstream_2.bin matches bitstream_demuxed_2.bin"
  else
    elog "FAIL: bitstream_2.bin does NOT match bitstream_demuxed_2.bin"
    return 1
  fi

  # Compare second bitstream
  if cmp -s "${BITSTREAM_3}" "${DEMUXED_OUTPUT_3}"; then
    echo "PASS: bitstream_3.bin matches bitstream_demuxed_3.bin"
  else
    elog "FAIL: bitstream_3.bin does NOT match bitstream_demuxed_3.bin"
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

# Compare the four demuxed bitstreams with original bitstreams
compare_md5_4() {
  local output_file_0="${AVM_TEST_OUTPUT_DIR}/decoded_seq_0"
  local output_file_1="${AVM_TEST_OUTPUT_DIR}/decoded_seq_1"
  local output_file_2="${AVM_TEST_OUTPUT_DIR}/decoded_seq_2"
  local output_file_3="${AVM_TEST_OUTPUT_DIR}/decoded_seq_3"
  local output_file_demuxed_0="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_0"
  local output_file_demuxed_1="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_1"
  local output_file_demuxed_2="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_2"
  local output_file_demuxed_3="${AVM_TEST_OUTPUT_DIR}/decoded_seq_demuxed_3"

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

  # Compare second output seq
  if cmp -s "${output_file_2}" "${output_file_demuxed_2}"; then
    echo "PASS: decoded_seq_2 matches decoded_seq_demuxed_2"
  else
    elog "FAIL: decoded_seq_2 does NOT match decoded_seq_demuxed_2"
    return 1
  fi

  # Compare second output seq
  if cmp -s "${output_file_3}" "${output_file_demuxed_3}"; then
    echo "PASS: decoded_seq_3 matches decoded_seq_demuxed_3"
  else
    elog "FAIL: decoded_seq_3 does NOT match decoded_seq_demuxed_3"
    return 1
  fi
  echo "All decoded sequences comparisons passed successfully!"
}


# Run complete encode, mux, and demux pipeline

run_encode_mux_demux_avmenc() {
  echo "Start single layer run_encode_mux_demux_ml_4streams"

  echo "avmenc with lag = 0"
  encode_bitstream_0 5 || return 1
  encode_bitstream_1 5 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "avmenc with nonzero lag"
  encode_lag_bitstream_0 10 || return 1
  encode_lag_bitstream_1 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "Done avmenc single layer streamS"
}

run_encode_mux_demux_ml_temporal() {
  echo "Start multi layer streams"

  echo "(#temporal, #embedded) = (1, 1)"
  ml_encode_bitstream_0 1 1 0 10 || return 1
  ml_encode_bitstream_1 1 1 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (2, 1)"
  ml_encode_bitstream_0 2 1 0 10 || return 1
  ml_encode_bitstream_1 2 1 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (3, 1)"
  ml_encode_bitstream_0 3 1 0 10 || return 1
  ml_encode_bitstream_1 3 1 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (2, 1) and (1, 1) for first/second stream"
  ml_encode_bitstream_0 2 1 0 10 || return 1
  ml_encode_bitstream_1 1 1 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (3, 1) and (1, 1) for first/second stream"
  ml_encode_bitstream_0 3 1 0 10 || return 1
  ml_encode_bitstream_1 1 1 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (3, 1) and (2, 1) for first/second stream"
  ml_encode_bitstream_0 3 1 0 10 || return 1
  ml_encode_bitstream_1 2 1 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "Done with multi layer streams"
}

run_encode_mux_demux_ml_embedded() {
  echo "Start multi layer streams"

  echo "(#temporal, #embedded) = (1, 2)"
  ml_encode_bitstream_0 1 2 0 10 || return 1
  ml_encode_bitstream_1 1 2 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (2, 2)"
  ml_encode_bitstream_0 2 2 0 10 || return 1
  ml_encode_bitstream_1 2 2 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (1, 2) and (2, 1) for first/second stream"
  ml_encode_bitstream_0 1 2 0 10 || return 1
  ml_encode_bitstream_1 2 1 0 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (3, 3)"
  ml_encode_bitstream_0 3 3 0 4|| return 1
  ml_encode_bitstream_1 3 3 0 4 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "Done with multi layer streams"
}

run_encode_mux_demux_ml_lag_ex1() {
  echo "Start multi layer streams"

  echo "(#temporal, #embedded) = (2, 1) for nonzero lag"
  ml_encode_bitstream_0 2 1 15 20 || return 1
  ml_encode_bitstream_1 2 1 15 20 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (1, 2) for nonzero lag"
  ml_encode_bitstream_0 1 2 15 20 || return 1
  ml_encode_bitstream_1 1 2 15 20 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "(#temporal, #embedded) = (2, 2) for nonzero lag"
  ml_encode_bitstream_0 2 2 15 20 || return 1
  ml_encode_bitstream_1 2 2 15 20 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

 echo "Done with multi layer streams"
}

run_encode_mux_demux_ml_lag_ex2() {
  echo "Start multi layer streams"

  echo "(#temporal, #embedded) = (2, 1) vs (2, 2) for nonzero lag"
  ml_encode_bitstream_0 2 1 10 15|| return 1
  ml_encode_bitstream_1 2 2 8 13 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "Done with multi layer streams"
}

run_encode_mux_demux_ml_4streams() {
  echo "Start multi layer streams"

  echo "test 4 multi layer streams:"
  echo "1. (#temporal, #embedded) = (3, 1)"
  echo "2. (#temporal, #embedded) = (1, 2)"
  echo "3. (#temporal, #embedded) = (1, 3)"
  echo "4. (#temporal, #embedded) = (2, 1) with lag"
  ml_encode_bitstream_0 3 1 0 10 || return 1
  ml_encode_bitstream_1 1 2 0 10 || return 1
  ml_encode_bitstream_2 1 3 0 4 || return 1
  ml_encode_bitstream_3 2 1 15 10 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  decode_bitstream_2 || return 1
  decode_bitstream_3 || return 1
  mux_bitstreams4 || return 1
  demux_bitstream4 || return 1
  compare_bitstreams4 || return 1
  decode_muxed_bitstream4 || return 1
  compare_md5_4 || return 1

 echo "Done with multi layer streams"
}

run_encode_mux_demux_ml_4streams_lag() {
  echo "Start multi layer streams"

  echo "test 4 multi layer streams: all with lag"
  echo "1. (#temporal, #embedded) = (2, 1)"
  echo "2. (#temporal, #embedded) = (1, 2)"
  echo "3. (#temporal, #embedded) = (2, 2)"
  echo "4. (#temporal, #embedded) = (1, 1)"
  ml_encode_bitstream_0 2 1 12 15 || return 1
  ml_encode_bitstream_1 1 2 8 15 || return 1
  ml_encode_bitstream_2 2 2 10 15 || return 1
  ml_encode_bitstream_3 1 1 11 15 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  decode_bitstream_2 || return 1
  decode_bitstream_3 || return 1
  mux_bitstreams4 || return 1
  demux_bitstream4 || return 1
  compare_bitstreams4 || return 1
  decode_muxed_bitstream4 || return 1
  compare_md5_4 || return 1

  echo "Done with multi layer streams"
}

# Test list
mux_demux_tests="run_encode_mux_demux_avmenc
                 run_encode_mux_demux_ml_temporal
                 run_encode_mux_demux_ml_embedded
                 run_encode_mux_demux_ml_lag_ex1
                 run_encode_mux_demux_ml_lag_ex2
                 run_encode_mux_demux_ml_4streams
                 run_encode_mux_demux_ml_4streams_lag"

# Execute tests
run_tests mux_demux_verify_environment "${mux_demux_tests}"
