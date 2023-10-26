#!/usr/bin/env python3
# Copyright (c) 2023, Alliance for Open Media. All rights reserved
##
# This source code is subject to the terms of the BSD 3-Clause Clear License and the
# Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
# not distributed with this source code in the LICENSE file, you can obtain it
# at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
# License 1.0 was not distributed with this source code in the PATENTS file, you
# can obtain it at aomedia.org/license/patent-license/.
##
from __future__ import annotations

import pathlib
import tempfile

from absl import app
from absl import flags
from absl import logging
from avm_stats.extract_proto import *
from avm_stats.frame_visualizations import *
from avm_stats.proto_helpers import *
from avm_stats.yuv_tools import *
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
    "plane", "y", ["y", "u", "v"], "Plane to visualize (defaults to luma: 'y')."
)


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

  fig, ax = plt.subplots(1, 1)
  plane = Plane[_PLANE.value.upper()]
  use_chroma = plane.is_chroma()
  vis = PredictionYuvLayer(plane=plane)
  vis.add(TransformBlockLayer(use_chroma=use_chroma))
  vis.add(PartitionTreeLayer(use_chroma=use_chroma))
  vis.add(SuperblockLayer())
  vis.add(MotionVectorLayer(use_chroma=use_chroma))
  vis.add(PredictionModeAnnotationLayer(use_chroma=use_chroma))
  vis.show(seq[_FRAME.value], ax)
  plt.show()


if __name__ == "__main__":
  app.run(main)
