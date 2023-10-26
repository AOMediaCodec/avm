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
"""Wrapper around the extract_proto binary."""

from collections.abc import Sequence
import dataclasses
import os
import pathlib
import subprocess
from typing import Iterator

from absl import logging
from avm_stats import avm_frame_pb2
from avm_stats import proto_helpers


@dataclasses.dataclass
class ExtractProtoResult:
  output_path: pathlib.Path
  proto_count: int
  skipped: bool
  stdout: str | None = None
  stderr: str | None = None


def _count_protos(proto_dir: pathlib.Path) -> int:
  return sum([1 for _ in proto_dir.glob("*.pb")])


def extract_proto(
    *,
    extract_proto_path: pathlib.Path,
    stream_path: pathlib.Path,
    output_path: pathlib.Path,
    skip_if_output_already_exists: bool = False,
    yuv_path: pathlib.Path | None = None,
) -> ExtractProtoResult:
  if (
      skip_if_output_already_exists
      and os.path.exists(output_path)
      and os.path.isdir(output_path)
  ):
    count = _count_protos(output_path)
    if count > 0:
      logging.info(
          "%s already exists, skipping extract_proto.", str(output_path)
      )
      return ExtractProtoResult(
          output_path=output_path, proto_count=count, skipped=True
      )
  os.makedirs(output_path, exist_ok=True)
  extract_proto_args = [
      str(extract_proto_path),
      "--stream",
      str(stream_path),
      "--output_folder",
      str(output_path),
  ]
  if yuv_path is not None:
    extract_proto_args.extend([
        "--orig_yuv",
        str(yuv_path),
    ])
  logging.info("Running:\n %s", " ".join(extract_proto_args))

  p = subprocess.run(extract_proto_args, capture_output=True, check=True)
  stdout = p.stdout.decode("utf-8")
  stdin = p.stderr.decode("utf-8")
  count = _count_protos(output_path)
  return ExtractProtoResult(
      output_path=output_path,
      proto_count=count,
      skipped=False,
      stdout=stdout,
      stderr=stdin,
  )


def load_protos(proto_dir: pathlib.Path) -> Iterator[proto_helpers.Frame]:
  # Note: Sorting is important here, since the proto file names are numbered in
  # decode order, but glob returns files in arbitrary order.
  for pb in sorted(proto_dir.glob("*.pb")):
    with open(pb, "rb") as f:
      frame_proto = avm_frame_pb2.Frame.FromString(f.read())
      frame = proto_helpers.Frame(frame_proto)
      yield frame


def extract_and_load_protos(
    *,
    extract_proto_path: pathlib.Path,
    stream_path: pathlib.Path,
    output_path: pathlib.Path,
    skip_if_output_already_exists: bool = False,
    yuv_path: pathlib.Path | None = None,
) -> Iterator[proto_helpers.Frame]:
  result = extract_proto(
      extract_proto_path=extract_proto_path,
      stream_path=stream_path,
      output_path=output_path,
      skip_if_output_already_exists=skip_if_output_already_exists,
      yuv_path=yuv_path,
  )
  yield from load_protos(result.output_path)
