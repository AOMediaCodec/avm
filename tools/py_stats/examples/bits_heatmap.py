#!/usr/bin/env python3
## Copyright (c) 2023, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
from __future__ import annotations

import pathlib
import tempfile
from typing import Sequence

from absl import app
from absl import flags
from absl import logging
from avm_stats.extract_proto import *
from avm_stats.frame_visualizations import *
from avm_stats.proto_helpers import *
import matplotlib.pyplot as plt

_STREAM = flags.DEFINE_string("stream", None, "Path to AVM stream.")
flags.mark_flag_as_required("stream")

_SOURCE = flags.DEFINE_string(
    "source", None, "Path to source YUV/Y4M before encoding (optional)."
)

_EXTRACT_PROTO_BIN = flags.DEFINE_string(
    "extract_proto_bin", None, "Path to extract_proto binary."
)
flags.mark_flag_as_required("extract_proto_bin")

_FRAME = flags.DEFINE_integer(
    "frame", 0, "Frame number to visualize (defaults to the first frame)."
)
_PLANE = flags.DEFINE_enum(
    "plane", "y", ["y", "u", "v"], "Plane to visualize (defaults to luma: 'y')"
)
_FILTER = flags.DEFINE_string("filter", None, "Symbol filter to apply.")


def main(argv: Sequence[str]) -> None:
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")
  with tempfile.TemporaryDirectory() as tmp_dir:
    tmp_path = pathlib.Path(tmp_dir)
    stream_path = pathlib.Path(_STREAM.value)
    extract_proto_path = pathlib.Path(_EXTRACT_PROTO_BIN.value)
    yuv_path = _SOURCE.value or None
    frames = extract_and_load_protos(
        extract_proto_path=extract_proto_path,
        stream_path=stream_path,
        output_path=tmp_path,
        skip_if_output_already_exists=False,
        yuv_path=yuv_path,
    )
    seq = list(frames)
    num_frames = len(seq)
    logging.info(f"Loaded {num_frames} frame protos.")

  frame = seq[_FRAME.value]
  c_functions = set()
  c_functions |= set(
      i.source_function for i in frame.proto.symbol_info.values()
  )
  print("Available symbol functions:")
  for c_function in sorted(c_functions):
    print(c_function)
  symbol_filter = _FILTER.value

  _, axes = plt.subplots(2, 1)

  def my_filter(sym: Symbol):
    return sym.source_function == symbol_filter or symbol_filter is None

  plane = Plane[_PLANE.value.upper()]
  use_chroma = plane.is_chroma()
  ReconstructionYuvLayer(plane=plane).show(frame, axes[0])
  BitsHeatmapLayer(use_chroma=use_chroma, filt=my_filter).add(
      PartitionTreeLayer(use_chroma=use_chroma)
  ).add(SuperblockLayer()).show(frame, axes[1])
  total_bits = 0
  bits_for_symbol = 0
  for sb in frame.superblocks:
    total_bits += sb.get_total_bits()
    bits_for_symbol += sb.get_total_bits(use_chroma=use_chroma, filt=my_filter)
  if symbol_filter is not None:
    percent = 100.0 * bits_for_symbol / total_bits
    print(f"Bits for {symbol_filter}: {bits_for_symbol} ({percent:.2f}%)")
  plt.show()


if __name__ == "__main__":
  app.run(main)
