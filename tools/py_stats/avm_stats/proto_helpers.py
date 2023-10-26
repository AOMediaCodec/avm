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
"""Wrapper classes around AVM Frame proto and its sub-messages."""

from __future__ import annotations

import abc
from collections.abc import Callable, Iterable, Iterator
import dataclasses
import enum
import functools
import itertools
import pathlib

from avm_stats import avm_frame_pb2
from avm_stats import yuv_tools
import numpy as np


class Symbol:
  """Wrapper class around protobuf Symbol."""

  def __init__(self, frame: Frame, proto: avm_frame_pb2.Symbol):
    self.frame = frame
    self.proto = proto
    self.info = frame.proto.symbol_info[proto.info_id]

  @property
  def source_file(self) -> str:
    return self.info.source_file

  @property
  def source_line(self) -> int:
    return self.info.source_line

  @property
  def source_function(self) -> str:
    return self.info.source_function

  @property
  def bits(self) -> float:
    return self.proto.bits


# Used to filter out a specific class of symbols from the bitstream's symbol
# dump.
SymbolFilter = Callable[[Symbol], bool]

_PALETTE_PREDICTION_MODE = "PALETTE_PRED"
_UNKNOWN_PREDICTION_MODE = "UNKNOWN"
_NUM_PLANES_CHROMA = 2


@enum.unique
class Plane(enum.IntEnum):
  # Luma component
  Y = 0
  # Chroma blue component
  U = 1
  # Chroma red component
  V = 2

  def is_chroma(self) -> bool:
    return self == Plane.U or self == Plane.V


def subsample_dimension(dimension: int, plane: Plane) -> int:
  """Converts a pixel count to a sample count to do 4:2:0 chroma subsampling."""
  if plane.is_chroma():
    return (dimension + 1) // 2
  return dimension


@dataclasses.dataclass(frozen=True, kw_only=True)
class PlaneBuffer:
  """Sample buffer for one plane of one frame.

  Since this object represents only a single plane, the data stored within it
  is technically "samples" and not "pixels" (a pixel is composed of samples
  from multiple planes). However, this data is sometimes colloquially referred
  to as "pixels" within libavm and in other contexts.

  Attributes:
    frame: Frame object that these samples belong to.
    plane: Whether these samples are luma, chroma U, or chroma V.
    width: Width of the sample data for this plane.
    height: Height of the sample data for this plane.
    original: Optional numpy array of original samples, before encoding. Since
      the original samples are not always available, this field is optional.
    prediction: Numpy array of intra or inter predicted samples.
    pre_filtered: Numpy array of reconstructed samples BEFORE post-processing
      filters (deblocking filter, CDEF, LR).
    reconstruction: Numpy array of reconstructed samples AFTER post-processing
      filters.
    residual: Numpy array of residual samples (i.e. input to the DCT transform).
    filter_delta: Numpy array of the difference between pre and post filtered
      reconstruction.
    distortion: Optional numpy array of the error/distortion (i.e. difference
      between original YUV and reconstructed YUV). Since the original samples
      are not always available, this field is optional.
  """

  frame: Frame
  plane: Plane
  width: int
  height: int
  original: np.ndarray | None
  prediction: np.ndarray
  pre_filtered: np.ndarray
  reconstruction: np.ndarray
  residual: np.ndarray
  filter_delta: np.ndarray
  distortion: np.ndarray | None


def _copy_samples_from_proto(
    frame: Frame, plane: Plane, proto_field: str
) -> np.ndarray | None:
  """Returns an Optional numpy array of samples copied from a proto.

  Args:
    frame: The frame owning the proto to copy samples from.
    plane: Which plane to copy samples from.
    proto_field: Which class of samples to copy from the proto, e.g. "original"
      or "reconstruction".
  """
  width = subsample_dimension(frame.width, plane)
  height = subsample_dimension(frame.height, plane)
  dtype = np.uint8 if frame.bit_depth == 8 else np.uint16
  samples = np.zeros((height, width), dtype=dtype)
  # Sample data is stored at the superblock level within the proto. Loop over
  # these to reconstruct the entire plane. Note that although the proto field is
  # named "pixel_data", it's specifically sample data in this context.
  for superblock in frame.proto.superblocks:
    if not superblock.pixel_data[plane].HasField(proto_field):
      return None
    superblock_plane = getattr(superblock.pixel_data[plane], proto_field)
    sb_width = superblock.size.width
    sb_height = superblock.size.height
    sb_x = superblock.position.x
    sb_y = superblock.position.y
    if plane.is_chroma():
      sb_width = sb_width // 2
      sb_height = sb_height // 2
      sb_x = sb_x // 2
      sb_y = sb_y // 2

    # Clip current superblock to frame dimensions.
    sb_width_clipped = min(sb_width, width - sb_x)
    sb_height_clipped = min(sb_height, height - sb_y)

    pixels_width = superblock_plane.width
    pixels_height = superblock_plane.height

    superblock_samples = np.array(superblock_plane.pixels).reshape(
        (pixels_height, pixels_width)
    )[:sb_height_clipped, :sb_width_clipped]

    samples[sb_y : sb_y + sb_height_clipped, sb_x : sb_x + sb_width_clipped] = (
        superblock_samples
    )

  return samples


def _create_plane_buffer(frame: Frame, plane: Plane) -> PlaneBuffer:
  """Creates PlaneBuffer object storing different classes of raw sample data.

  Args:
    frame: The frame owning the proto we're copying samples from.
    plane: Which plane to copy samples from.

  Returns:
    PlaneBuffer containing raw samples at various stages in the codec pipeline.
  """
  width = subsample_dimension(frame.width, plane)
  height = subsample_dimension(frame.height, plane)
  # Note: original YUV data (i.e. the source YUV before encoding) is not always
  # available, so this might be None.
  original = _copy_samples_from_proto(frame, plane, "original")
  prediction = _copy_samples_from_proto(frame, plane, "prediction")
  pre_filtered = _copy_samples_from_proto(frame, plane, "pre_filtered")
  reconstruction = _copy_samples_from_proto(frame, plane, "reconstruction")
  assert prediction is not None
  assert pre_filtered is not None
  assert reconstruction is not None
  # Even for 8-bit frames, 16 bits are needed for the deltas, since the range is
  # [-255, 255].
  residual = pre_filtered.astype(np.int16) - prediction
  filter_delta = reconstruction.astype(np.int16) - pre_filtered
  # Since distortion is computed from the original, this might also be None.
  distortion = (
      original.astype(np.int16) - reconstruction
      if original is not None
      else None
  )
  return PlaneBuffer(
      frame=frame,
      plane=plane,
      width=width,
      height=height,
      original=original,
      prediction=prediction,
      pre_filtered=pre_filtered,
      reconstruction=reconstruction,
      residual=residual,
      filter_delta=filter_delta,
      distortion=distortion,
  )


@dataclasses.dataclass(kw_only=True)
class Rectangle:
  """Represents a 2d position and size.

  Attributes:
    left_x: Position of left edge, in pixel units.
    top_y: Position of top edge, in pixel units.
    width: Width, in pixel units.
    height: Height, in pixel units.
  """

  left_x: float
  top_y: float
  width: float
  height: float

  @property
  def center_x(self) -> float:
    """The x coordinate of the center, in pixel units."""
    return self.left_x + self.width / 2

  @property
  def center_y(self) -> float:
    """The y coordinate of the center, in pixel units."""
    return self.top_y + self.height / 2

  @property
  def right_x(self) -> float:
    """The x coordinate of the right edge, in pixel units."""
    return self.left_x + self.width

  @property
  def bottom_y(self) -> float:
    """The y coordinate of the bottom edge, in pixel units."""
    return self.top_y + self.height


class Block2d(metaclass=abc.ABCMeta):
  """Abstract class for representing an object that has a 2D position and size.

  Attributes:
    rect: Bounding box rectangle of this object.
    clipped_rect: Bounding box rectangle of this object, clipped to the
      boundaries of the frame that contains it.
  """

  @property
  @abc.abstractmethod
  def rect(self) -> Rectangle:
    pass

  @property
  @abc.abstractmethod
  def clipped_rect(self) -> Rectangle:
    pass


class CodingUnit(Block2d):
  """Wrapper class around protobuf CodingUnit.

  Attributes:
    frame: Frame object that owns this coding unit.
    superblock: Superblock object that owns this coding unit.
    proto: Protobuf representation of this coding unit.
    rect: Bounding box of this coding unit, in pixel units.
    clipped_rect: Bounding box of this coding unit, in pixel units, clipped to
      the boundaries of the frame. Coding units along the right or bottom edges
      of a frame may extend past its width and height, so clipped_rect will
      compensate for that.
  """

  def __init__(
      self,
      frame: Frame,
      superblock: Superblock,
      proto: avm_frame_pb2.CodingUnit,
  ):
    self.frame = frame
    self.superblock = superblock
    self.proto = proto

  @property
  def rect(self) -> Rectangle:
    return Rectangle(
        left_x=self.proto.position.x,
        top_y=self.proto.position.y,
        width=self.proto.size.width,
        height=self.proto.size.height,
    )

  @property
  def clipped_rect(self) -> Rectangle:
    return self.frame.clip_rect(self.rect)

  def get_transform_rects(self) -> Iterator[Rectangle]:
    """Yields the bounding boxes of all transform units within this coding unit."""

    # Note: this always uses transform_planes[0]. For luma coding units, there
    # will be exactly one transform plane. For chroma, there will be exactly
    # two, but both will have the same structure.
    for tx in self.proto.transform_planes[0].transform_units:
      yield Rectangle(
          left_x=tx.position.x,
          top_y=tx.position.y,
          width=tx.size.width,
          height=tx.size.height,
      )

  def get_symbols(self, filt: SymbolFilter | None = None) -> Iterable[Symbol]:
    """Yields all bitstream symbols associated with this CodingUnit.

    Args:
      filt: Optional filter to apply to the returned symbols, e.g. to get
        symbols associated with some specific part of the decoding pipeline.
    """
    wrapped = functools.partial(Symbol, self.frame)
    symbol_slice = map(
        wrapped,
        itertools.islice(
            self.superblock.proto.symbols,
            self.proto.symbol_range.start,
            self.proto.symbol_range.end,
        ),
    )
    if filt:
      yield from filter(filt, symbol_slice)
    else:
      yield from symbol_slice

  def is_chroma_block(self) -> bool:
    """Returns whether this block is a chroma block."""
    # In AV2, luma and chroma partition trees are stored separately because of
    # SDP (semi-decoupled partitioning). For consistency, luma and chroma are
    # always stored in separate partition trees in the proto, even when SDP is
    # disabled. The chroma partition tree is identical to the luma partition
    # tree in this case.
    return len(self.proto.transform_planes) == _NUM_PLANES_CHROMA

  def uses_palette_prediction(self) -> bool:
    """Returns whether this block uses the palette prediction mode."""
    if self.is_chroma_block():
      return self.proto.prediction_mode.uv_palette_count > 0
    else:
      return self.proto.prediction_mode.palette_count > 0

  def get_prediction_mode(self) -> str:
    """Returns the name of the prediction mode for this block."""
    # Palette mode is a special case; it gets coded as DC_PRED in the bitstream,
    # but is a distinct prediction mode.
    if self.uses_palette_prediction():
      return _PALETTE_PREDICTION_MODE

    if self.is_chroma_block():
      mode = self.proto.prediction_mode.uv_mode
      mode_mapping = self.frame.proto.enum_mappings.uv_prediction_mode_mapping
    else:
      mode = self.proto.prediction_mode.mode
      mode_mapping = self.frame.proto.enum_mappings.prediction_mode_mapping

    if mode in mode_mapping:
      return mode_mapping[mode]
    else:
      return _UNKNOWN_PREDICTION_MODE


class Superblock(Block2d):
  """Wrapper class around protobuf Superblock.

  Attributes:
    frame: Frame object that owns this coding unit.
    proto: Protobuf representation of this superblock.
    coding_units_luma: List of all coding units within this superblock for the
      luma plane.
    coding_units_chroma: List of all coding units within this superblock for the
      chroma planes.
    qindex: Quantization index for this superblock.
    rect: Bounding box of this superblock, in pixel units.
    clipped_rect: Bounding box of this superblock, in pixel units, clipped to
      the boundaries of the frame. Superblocks along the right or bottom edges
      of a frame may extend past its width and height, so clipped_rect will
      compensate for that.
  """

  def __init__(self, frame: Frame, proto: avm_frame_pb2.Superblock):
    self.frame = frame
    self.proto = proto
    self.coding_units_shared = [
        CodingUnit(frame, self, cu) for cu in proto.coding_units_shared
    ]
    self.coding_units_chroma = [
        CodingUnit(frame, self, cu) for cu in proto.coding_units_chroma
    ]

  @property
  def qindex(self) -> int:
    # TODO(comc): qindex can only vary by superblock, not by coding
    # unit, so this field could be promoted to the Superblock message.
    return self.coding_units_luma[0].proto.qindex

  @property
  def rect(self) -> Rectangle:
    return Rectangle(
        left_x=self.proto.position.x,
        top_y=self.proto.position.y,
        width=self.proto.size.width,
        height=self.proto.size.height,
    )

  @property
  def clipped_rect(self) -> Rectangle:
    return self.frame.clip_rect(self.rect)

  def get_coding_units(
      self, *, use_chroma: bool = False
  ) -> Iterator[CodingUnit]:
    """Get all coding units for this superblock.

    Args:
      use_chroma: If True, returns the chroma coding units rather than luma.

    Yields:
      Luma or chroma coding units contained within this superblock.
    """
    if use_chroma:
      yield from self.coding_units_chroma
    else:
      yield from self.coding_units_shared

  def get_partition_rects(
      self, use_chroma: bool = False
  ) -> Iterator[Rectangle]:
    """Get bounding boxes for all coding units within this frame.

    Args:
      use_chroma: Use the chroma partition tree rather than luma.

    Yields:
      Bounding boxes for the leaf nodes (i.e. coding units) of this superblock's
      partition tree.
    """
    for cu in self.get_coding_units(use_chroma=use_chroma):
      yield cu.rect

  def get_transform_rects(
      self, *, use_chroma: bool = False
  ) -> Iterator[Rectangle]:
    """Get bounding boxes for all transform units within this frame.

    Args:
      use_chroma: Use the chroma partition tree rather than luma.

    Yields:
      Bounding boxes for all transform units within the coding units of this
      superblock.
    """
    for cu in self.get_coding_units(use_chroma=use_chroma):
      yield from cu.get_transform_rects()

  def get_bits_per_coding_unit(
      self,
      *,
      filt: SymbolFilter | None = None,
      use_chroma: bool = False,
  ) -> Iterator[float]:
    """Maps each coding unit to a number of bits used to encode it.

    For each coding unit within this superblock, add up the number of fractional
    bits that were used to encode its symbols in the bitstream.

    Args:
      filt: Optional filter to apply to each symbol, e.g. to count the bits used
        for some specific class of symbol.
      use_chroma: If True, look at chroma coding units rather than luma.

    Yields:
      Fractional bit counts, one for each coding unit within this superblock.
    """
    yield from (
        sum(sym.bits for sym in cu.get_symbols(filt))
        for cu in self.get_coding_units(use_chroma=use_chroma)
    )

  def get_total_bits(
      self,
      *,
      filt: SymbolFilter | None = None,
      use_chroma: bool = False,
  ) -> float:
    """Counts the total number of bits used to encode this superblock.

    Args:
      filt: Optional filter to apply to each symbol, e.g. to count the bits used
        for some specific class of symbol.
      use_chroma: If True, look at chroma coding units rather than luma.

    Returns:
      Total bit count used to encode this superblock.
    """
    return sum(self.get_bits_per_coding_unit(filt=filt, use_chroma=use_chroma))


class Frame:
  """Wrapper class around protobuf Frame.

  Attributes:
    proto: Protobuf representation of this frame.
    superblocks: List of superblocks that belong to this frame.
    pixels: List of PlaneBuffer associated with this frame, one for each plane.
    original_rgb: Optional original (pre-encode) pixels of this frame, converted
      from YUV to RGB. Since the original data may be missing, this can also be
      None.
    reconstruction_rgb: Reconstructed pixels of this from, converted from YUV to
      RGB.
    frame_id: Decode-order index of this frame.
    width: Width of this frame in pixels.
    height: Height of this frame in pixels.
    bit_depth: Either 8 or 10 bits per sample.
  """

  def __init__(self, proto: avm_frame_pb2.Frame):
    self.proto = proto
    self.superblocks = [
        Superblock(self, sb_proto) for sb_proto in proto.superblocks
    ]
    self.pixels = [
        _create_plane_buffer(self, p) for p in (Plane.Y, Plane.U, Plane.V)
    ]

    if self.pixels[0].original is None:
      self.original_rgb = None
    else:
      self.original_rgb = yuv_tools.yuv_to_rgb(
          self.pixels[0].original,
          yuv_tools.upscale(self.pixels[1].original, 2),
          yuv_tools.upscale(self.pixels[2].original, 2),
      )
    self.reconstruction_rgb = yuv_tools.yuv_to_rgb(
        self.pixels[0].reconstruction,
        yuv_tools.upscale(self.pixels[1].reconstruction, 2),
        yuv_tools.upscale(self.pixels[2].reconstruction, 2),
    )

  @property
  def frame_id(self) -> int:
    return self.proto.frame_params.decode_index

  @property
  def width(self) -> int:
    return self.proto.frame_params.width

  @property
  def height(self) -> int:
    return self.proto.frame_params.height

  @property
  def bit_depth(self) -> int:
    return self.proto.frame_params.bit_depth

  def clip_rect(self, rect: Rectangle) -> Rectangle:
    """Clips a rectangle to be contained with the frame boundaries.

    Args:
      rect: Rectangle to clip, typically the bounding box of some sub-object
        within this frame, e.g. a coding unit.

    Returns:
      The rectangle clipped to the boundaries of this frame.
    """
    width = min(rect.width, self.width - rect.left_x)
    height = min(rect.height, self.height - rect.top_y)
    return Rectangle(
        left_x=rect.left_x, top_y=rect.top_y, width=width, height=height
    )


def load_frame_from_path(proto_path: pathlib.Path) -> Frame:
  with proto_path.open("rb") as f:
    frame_proto = avm_frame_pb2.Frame.FromString(f.read())
  return Frame(frame_proto)
