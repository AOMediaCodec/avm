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
"""Helper tool to dump an AVM frame proto to a JSON file."""

from collections.abc import Sequence
import pathlib

from absl import app
from absl import flags
from absl import logging
from avm_stats import avm_frame_pb2
from google.protobuf import json_format

_PROTO = flags.DEFINE_string("proto", None, "Path to AVM frame protobuf.")
flags.mark_flag_as_required("proto")

_OUTPUT_JSON = flags.DEFINE_string(
    "output_json", None, "Path to output JSON file."
)
flags.mark_flag_as_required("output_json")


def dump_json(proto: avm_frame_pb2.Frame, output_json: pathlib.Path):
  """Dumps an AVM frame proto to a JSON file.

  Args:
    frame: AVM frame proto object.
    output_json: Path to write JSON to.
  """
  with output_json.open("w") as f:
    json = json_format.MessageToJson(proto)
    f.write(json)
  logging.info("Wrote %s.", output_json)


def main(argv: Sequence[str]) -> None:
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")

  proto_path = pathlib.Path(_PROTO.value)
  output_json = pathlib.Path(_OUTPUT_JSON.value)
  with proto_path.open("rb") as f:
    proto = avm_frame_pb2.Frame.FromString(f.read())

  dump_json(proto, output_json)


if __name__ == "__main__":
  app.run(main)
