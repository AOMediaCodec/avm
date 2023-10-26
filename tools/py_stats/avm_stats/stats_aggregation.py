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
from collections.abc import Iterable, Iterator
from typing import Generic, TypeVar

from avm_stats import proto_helpers
import pandas as pd

# Type of the object we're extracting data from. This will typically be
# superblocks, coding units, or transform units.
_TObj = TypeVar("_TObj")
# Type of the data that gets extracted from the objects we iterate over. Typically this will be a namedtuple or similar type that can easily be converted into a pandas dataframe.
_TData = TypeVar("_TData")


class Extractor(Generic[_TObj, _TData], metaclass=abc.ABCMeta):
  """Takes in a single frame, and extracts some data of interest from subobjects contained within that frame."""

  @abc.abstractmethod
  def sample(self, obj: _TObj) -> Iterator[_TData]:
    pass

  def extract(self, frame: proto_helpers.Frame) -> Iterator[_TData]:
    for obj in self.get_objects(frame):
      yield from self.sample(obj)

  def extract_to_dataframe(self, frame: proto_helpers.Frame) -> pd.DataFrame:
    return pd.DataFrame(self.extract(frame))

  @abc.abstractmethod
  def get_objects(self, frame: proto_helpers.Frame) -> Iterator[_TObj]:
    """Typically this will either be superblocks, coding units, or transform units."""
    pass


class FrameExtractor(Extractor):

  def get_objects(self, frame: proto_helpers.Frame) -> Iterator[_TObj]:
    yield frame


class SuperblockExtractor(Extractor):

  def get_objects(self, frame: proto_helpers.Frame) -> Iterator[_TObj]:
    yield from frame.superblocks


class CodingUnitExtractor(Extractor):

  def get_objects(self, frame: proto_helpers.Frame) -> Iterator[_TObj]:
    # TODO(comc): Add an option to get chroma CUs as well.
    for sb in frame.superblocks:
      yield from sb.get_coding_units()


def aggregate_frames(
    frames: Iterable[proto_helpers.Frame], extractor: Extractor
) -> Iterator[_TData]:
  for frame in frames:
    yield from extractor.extract(frame)


def aggregate_to_dataframe(
    frames: Iterable[proto_helpers.Frame], extractor: Extractor
) -> pd.DataFrame:
  return pd.DataFrame(aggregate_frames(frames, extractor))
