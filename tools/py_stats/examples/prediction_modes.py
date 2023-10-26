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

import abc
from collections import OrderedDict, defaultdict
from functools import partial
import pathlib
import tempfile
from typing import Sequence

from absl import app
from absl import flags
from absl import logging
from avm_stats.extract_proto import *
from avm_stats.frame_visualizations import *
from avm_stats.proto_helpers import *
from avm_stats.yuv_tools import *
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import PIL

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
    "frame", 0, "Frame number to visualize (defaults to first frame)."
)
_PLANE = flags.DEFINE_enum(
    "plane", "y", ["y", "u", "v"], "Plane to visualize (defaults to luma: 'y')"
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
  plane = Plane[_PLANE.value.upper()]
  mode_counts = defaultdict(int)
  for sb in seq[_FRAME.value].superblocks:
    for cu in sb.get_coding_units(use_chroma=plane.is_chroma()):
      mode_counts[cu.get_prediction_mode()] += 1
  total = sum(mode_counts.values())
  keys = mode_counts.keys()
  labels = [f"{k} ({100.0*mode_counts[k]/total:.2f}%)" for k in keys]
  sizes = [mode_counts[k] for k in keys]

  fig, ax = plt.subplots()
  ax.pie(sizes, labels=labels)
  plt.show()


if __name__ == "__main__":
  app.run(main)
