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
"""Helper functions for working with YUV files from Codec ML Colab notebooks."""

import dataclasses

import numpy as np

# This byte sequence marks the end of the header and the start of frame data
# within a .y4m file.
_Y4M_FRAME_MARKER = b"FRAME\n"


# TODO(comc): More sophisticated colorspace handling.
def yuv_to_rgb(
    y: np.ndarray, u: np.ndarray, v: np.ndarray, bit_depth: int = 8
) -> np.ndarray:
  """Converts individual Y, U, V planes into a single RGB array.

  matplotlib can't show YUVs directly, so this helper function lets us visualize
  all three planes at once with pyplot.imshow(). Note that this function does
  not compensate for chroma subsampling. If necessary, the chroma planes should
  be upscaled beforehand.

  Args:
    y: (H, W) shaped luma plane.
    u: (H, W) shaped chroma U plane.
    v: (H, W) shaped chroma V plane.
    bit_depth: image bit-depth (8 or 10 bits per pixel).

  Returns:
    (H, W, 3) shaped RGB image. dtype is uint8 for bit_depth=8, and uint16
    otherwise.
  """
  if bit_depth not in (8, 10):
    raise ValueError(
        f"Only 8-bit and 10-bit YUVs are supported: bit_depth={bit_depth}."
    )
  y_max = (1 << bit_depth) - 1
  # Scale luma to [0, 1] and chroma to [-0.5, 0.5]
  y = y.astype(np.float32) / y_max
  u = u.astype(np.float32) / y_max - 0.5
  v = v.astype(np.float32) / y_max - 0.5

  # RGB conversion matrix from:
  # https://en.wikipedia.org/wiki/YUV#SDTV_with_BT.470
  # It is possible to end up with values outside the range [0.0, 1.0] after this
  # transformation, so clamp them.
  r = np.clip(y + 1.13983 * v, 0, 1)
  g = np.clip(y - 0.39465 * u - 0.58060 * v, 0, 1)
  b = np.clip(y + 2.03211 * u, 0, 1)
  rgb = np.stack((r, g, b), axis=2) * y_max
  dtype = np.uint8 if bit_depth == 8 else np.uint16
  return rgb.astype(dtype)


def upscale(plane: np.ndarray, factor: int = 2) -> np.ndarray:
  """Upscale a 2d array, e.g. to compensate for chroma subsampling.

  For example, `upscale(np.array([[1, 2], [3, 4]]), 2)` returns:
  ```
    array([[1, 1, 2, 2],
           [1, 1, 2, 2],
           [3, 3, 4, 4],
           [3, 3, 4, 4]])
  ```
  Args:
    plane: (H, W) shaped ndarray.
    factor: subsampling factor to compensate for.

  Returns:
    Upsampled array.
  """
  return plane.repeat(factor, axis=0).repeat(factor, axis=1)


def _plane_size_420(width: int, height: int, *, is_chroma: bool) -> int:
  """Calculates how many samples make up a single plane.

  Note that this is specifically for 4:2:0 chroma subsampled planes, i.e. the U
  and V chroma planes will have 1/4th the samples of the luma plane.

  Args:
    width: Width of the plane in pixels.
    height: Height of the plane in pixels.
    is_chroma: If True, scale dimensions by a factor of 2 to compensate for
      chroma subsampling.

  Returns:
    Number of samples that make up this plane.
  """
  if is_chroma:
    chroma_width = (width + 1) // 2
    chroma_height = (height + 1) // 2
    return chroma_width * chroma_height
  return width * height


def _frame_size_420(width: int, height: int) -> int:
  """Calculates how many samples make up one frame.

  Note that this is specifically for 4:2:0 chroma subsampled images.

  Args:
    width: Width of the image in pixels.
    height: Height of the image in pixels.

  Returns:
    Number of samples that make up this image (i.e. the sum of the luma
    and chroma planes).
  """
  luma_size = _plane_size_420(width, height, is_chroma=False)
  chroma_size = _plane_size_420(width, height, is_chroma=True)
  return luma_size + 2 * chroma_size


class Yuv420:
  """Represents a single YUV 4:2:0 frame.

  Attributes:
    width: Frame width in pixels.
    height: Frame height in pixels.
    bit_depth: Bits per sample, either 8 or 10.
    y: numpy array storing luma plane data.
    u: numpy array storing chroma U plane data.
    v: numpy array storing chroma V plane data.
    rgb: numpy array of this frame converted to RGB.
  """

  def __init__(
      self,
      raw: np.ndarray,
      width: int,
      height: int,
      bit_depth: int = 8,
      offset: int = 0,
  ):
    self.width = width
    self.height = height
    if bit_depth not in (8, 10):
      raise ValueError(
          f"Only 8-bit and 10-bit YUVs are supported: bit_depth={bit_depth}."
      )
    self.bit_depth = bit_depth
    chroma_width = (width + 1) // 2
    chroma_height = (height + 1) // 2
    luma_size = _plane_size_420(width, height, is_chroma=False)
    chroma_size = _plane_size_420(width, height, is_chroma=True)
    u_offset = offset + luma_size
    v_offset = u_offset + chroma_size
    self.y = raw[offset : offset + luma_size].reshape((height, width))
    self.u = raw[u_offset : u_offset + chroma_size].reshape(
        (chroma_height, chroma_width)
    )
    self.v = raw[v_offset : v_offset + chroma_size].reshape(
        (chroma_height, chroma_width)
    )
    uu = upscale(self.u, 2)
    vv = upscale(self.v, 2)
    self.rgb = yuv_to_rgb(self.y, uu, vv, bit_depth)


@dataclasses.dataclass
class YuvSequence:
  """Represents a sequence of YUV frames."""

  yuvs: list[Yuv420]


def parse_raw_yuv(
    yuv_path: str, width: int, height: int, num_frames: int, bit_depth: int = 8
) -> YuvSequence:
  """Parse a raw YUV file.

  Args:
    yuv_path: Local file path to .yuv file.
    width: Width of YUV in pixels.
    height: Height of YUV in pixels.
    num_frames: Number of frames to read from YUV file.
    bit_depth: Bits per sample, either 8 or 10.

  Returns:
    YuvSequence of length num_frames.
  """
  yuvs = []
  with open(yuv_path, "rb") as f:
    frame_size = _frame_size_420(width, height)
    bytes_per_pixel = 1 if bit_depth == 8 else 2
    frame_size_bytes = frame_size * bytes_per_pixel
    for _ in range(num_frames):
      dtype = np.uint8 if bit_depth == 8 else np.uint16
      raw_bytes = f.read(frame_size_bytes)
      raw = np.frombuffer(raw_bytes, dtype=dtype)
      yuv = Yuv420(raw, width, height, bit_depth)
      yuvs.append(yuv)
  return YuvSequence(yuvs)


def parse_y4m(
    y4m_path: str, width: int, height: int, num_frames: int, bit_depth: int = 8
) -> YuvSequence:
  """Parse a .y4m file.

  Args:
    y4m_path: Local file path to .y4m file.
    width: Width of YUV in pixels.
    height: Height of YUV in pixels.
    num_frames: Number of frames to read from YUV file.
    bit_depth: Bits per sample, either 8 or 10.

  Returns:
    YuvSequence of length num_frames.
  """

  with open(y4m_path, "rb") as f:
    # TODO(comc): Read one frame at a time.
    raw_bytes = f.read()
  yuvs = []
  frame_size = _frame_size_420(width, height)
  offset = 0
  for _ in range(num_frames):
    offset = raw_bytes.find(_Y4M_FRAME_MARKER, offset)
    offset += len(_Y4M_FRAME_MARKER)
    dtype = np.uint8 if bit_depth == 8 else np.uint16
    raw = np.frombuffer(raw_bytes, dtype=dtype, count=frame_size, offset=offset)
    yuv = Yuv420(raw, width, height, bit_depth)
    yuvs.append(yuv)
  return YuvSequence(yuvs)
