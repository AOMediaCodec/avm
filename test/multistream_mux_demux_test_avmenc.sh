#!/bin/bash
#
# Script to encode two bitstreams, mux them, and demux the muxed bitstream
#

. $(dirname $0)/multistream_mux_demux_util.sh


# Run complete encode, mux, and demux pipeline
run_encode_mux_demux() {

  echo "Start single layer stream"

  echo "avmenc with lag = 0"
  encode_bitstream_0 || return 1
  encode_bitstream_1 || return 1
  decode_bitstream_0 || return 1
  decode_bitstream_1 || return 1
  mux_bitstreams || return 1
  demux_bitstream || return 1
  compare_bitstreams || return 1
  decode_muxed_bitstream || return 1
  compare_md5 || return 1

  echo "Done avmenc single layer stream"
}

# Test list
mux_demux_tests="run_encode_mux_demux"

# Execute tests
run_tests mux_demux_verify_environment "${mux_demux_tests}"
