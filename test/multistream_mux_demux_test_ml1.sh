#!/bin/bash
#
# Script to encode two bitstreams, mux them, and demux the muxed bitstream
#

. $(dirname $0)/multistream_mux_demux_util.sh


# Run complete encode, mux, and demux pipeline
run_encode_mux_demux() {

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

  echo "Done with multi layer streams"
}

# Test list
mux_demux_tests="run_encode_mux_demux"

# Execute tests
run_tests mux_demux_verify_environment "${mux_demux_tests}"
