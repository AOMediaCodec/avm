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
"""Helper tool to dump pixel data from a frame proto as YUVs."""

from collections.abc import Sequence
import pathlib

from absl import app
from absl import flags
from absl import logging
from avm_stats import proto_helpers

_PROTO = flags.DEFINE_string("proto", None, "Path to AVM frame protobuf.")
flags.mark_flag_as_required("proto")

_OUTPUT_FOLDER = flags.DEFINE_string(
    "output_folder", None, "Path to YUV output folder."
)
flags.mark_flag_as_required("output_folder")

_PIXEL_FIELDS = [
    "original",
    "prediction",
    "pre_filtered",
    "reconstruction",
]


def dump_yuv(
    frame: proto_helpers.Frame, field_name: str, output_path: pathlib.Path
):
  """Dumps pixels from a frame as raw YUV.

  Args:
    frame: Frame to extract pixels from.
    field_name: Name of the pixel field to extract, e.g. "original" or
      "prediction".
    output_path: Path to output YUV.
  """
  with output_path.open("wb") as f:
    for plane_id in range(3):
      if not hasattr(frame.pixels[plane_id], field_name):
        logging.warning(
            "Skipping %s (pixels do not exist on proto).", output_path
        )
        return
      plane = getattr(frame.pixels[plane_id], field_name)
      plane.tofile(f)
  logging.info("Generated %s.", output_path)


def _create_output_path(
    proto_path: pathlib.Path, output_folder: pathlib.Path, pixel_field: str
) -> pathlib.Path:
  proto_name = proto_path.stem
  return output_folder / f"{proto_name}_{pixel_field}.yuv"


def dump_all_yuvs(proto_path: pathlib.Path, output_folder: pathlib.Path):
  """Loads a proto frame and dumps each class of pixels as raw YUVs.

  Args:
    proto_path: Path to proto frame to load and extract pixels from.
    output_folder: Path to output folder. Output filename will be the same as
      the input proto, but with a .yuv extension instead.
  """
  frame = proto_helpers.load_frame_from_path(proto_path)
  for pixel_field in _PIXEL_FIELDS:
    output_path = _create_output_path(proto_path, output_folder, pixel_field)
    dump_yuv(frame, pixel_field, output_path)


def main(argv: Sequence[str]) -> None:
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")

  proto_path = pathlib.Path(_PROTO.value)
  output_folder = pathlib.Path(_OUTPUT_FOLDER.value)
  dump_all_yuvs(proto_path, output_folder)


if __name__ == "__main__":
  app.run(main)
