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
"""Matplotlib visualization library for AVM Frame protos.

Defines a number of composable frame visualization layers that can be used to
show some data of interest within a specific frame in a Colab notebook.

The typical use case will start with some representation of the underlying pixel
data, and then render annotation additional layers on top of it, which will get
combined together into the final plot.

An example use case:
```
viz = (ReconstructionYuvLayer()
        .add(TransformBlockLayer())
        .add(PartitionTreeLayer())
        .add(SuperblockLayer())
fig, ax = plt.subplots(1, 1)
viz.show(frame, ax)
```

This will render the reconstructed YUV as the base layer, and then draw on top
the transform units, the partition tree, and the superblock boundaries.
"""

from __future__ import annotations

import abc

from avm_stats import proto_helpers
import matplotlib.pyplot as plt
from mpl_toolkits.axes_grid1 import inset_locator
import numpy as np


class VisualizationLayer(metaclass=abc.ABCMeta):
  """Abstract base class for all visualization layers.

  Attributes:
    layers: list of additional layers that will be rendered on top of this one.
      The additional layers will be rendered bottom to top.
  """

  def __init__(self):
    self.layers = []

  def add(self, layer: VisualizationLayer) -> VisualizationLayer:
    """Adds another visualization layer to the list of layers.

    Args:
      layer: Additional layer to render.

    Returns:
      self, so that additional add() calls can be chained together.
    """
    self.layers.append(layer)
    return self

  @abc.abstractmethod
  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    """Render this visualization to the Matplotlib axes.

    Args:
      frame: Frame used to get render data from, e.g. pixels or coding units.
      ax: Matplotlib axes to render to.
    """

  def show(self, frame: proto_helpers.Frame, ax: plt.Axes):
    self.render(frame, ax)
    for layer in self.layers:
      layer.render(frame, ax)


class YuvPlaneLayer(VisualizationLayer):
  """Visualize a single YUV plane.

  Attributes:
    name: Name of the layer; will be displayed in the plot title.
    pixels_attribute: The attribute of the frames PlaneBuffer to plot pixels
      from.
    plane: Which YUV plane to plot.
  """

  def __init__(
      self,
      name: str,
      pixels_attribute: str,
      *,
      plane: proto_helpers.Plane = proto_helpers.Plane.Y,
  ):
    self.name = name
    self.pixels_attribute = pixels_attribute
    self.plane = plane
    super().__init__()

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    ax.title.set_text(
        f"{self.name} ({self.plane.name})\nframe={frame.frame_id}"
    )
    pixels = getattr(frame.pixels[self.plane], self.pixels_attribute)
    width = frame.width
    height = frame.height
    ax.imshow(
        pixels, cmap="gray", vmin=0, vmax=255, extent=[0, width, height, 0]
    )


class OriginalYuvLayer(YuvPlaneLayer):

  def __init__(self, **kwargs):
    super().__init__("Original", "original", **kwargs)


class PredictionYuvLayer(YuvPlaneLayer):

  def __init__(self, **kwargs):
    super().__init__("Prediction", "prediction", **kwargs)


class PrefilteredYuvLayer(YuvPlaneLayer):

  def __init__(self, **kwargs):
    super().__init__("Pre-filtered Reconstruction", "pre_filtered", **kwargs)


class ReconstructionYuvLayer(YuvPlaneLayer):

  def __init__(self, **kwargs):
    super().__init__("Reconstruction", "reconstruction", **kwargs)


class YuvDeltaLayer(VisualizationLayer):

  def __init__(
      self,
      plane: proto_helpers.Plane = proto_helpers.Plane.Y,
      show_relative=False,
  ):
    self.plane = plane
    self.show_relative = show_relative
    super().__init__()

  @abc.abstractmethod
  def get_pixels(self, frame: proto_helpers.Frame) -> np.ndarray:
    pass

  @abc.abstractmethod
  def get_name(self) -> str:
    pass

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    pixels = self.get_pixels(frame)
    pixel_min = np.min(pixels)
    pixel_max = np.max(pixels)
    vmin, vmax = -255, 255
    annotation = "Relative" if self.show_relative else "Absolute"
    if self.show_relative:
      vmin, vmax = pixel_min, pixel_max
    ax.title.set_text(
        f"{self.get_name()} ({self.plane.name}) - {annotation}"
        f"\nframe={frame.frame_id}, min={pixel_min}, max={pixel_max}"
    )
    width = frame.width
    height = frame.height
    ax.imshow(
        pixels, cmap="gray", vmin=vmin, vmax=vmax, extent=[0, width, height, 0]
    )


class ResidualYuvLayer(YuvDeltaLayer):

  def __init__(
      self,
      plane: proto_helpers.Plane = proto_helpers.Plane.Y,
      show_relative=False,
  ):
    super().__init__(plane, show_relative)

  def get_pixels(self, frame: proto_helpers.Frame) -> np.ndarray:
    return frame.pixels[int(self.plane)].residual

  def get_name(self) -> str:
    return "Residual"


class FilterDeltaYuvLayer(YuvDeltaLayer):

  def __init__(
      self,
      plane: proto_helpers.Plane = proto_helpers.Plane.Y,
      show_relative=False,
  ):
    super().__init__(plane, show_relative)

  def get_pixels(self, frame: proto_helpers.Frame) -> np.ndarray:
    return frame.pixels[int(self.plane)].filter_delta

  def get_name(self) -> str:
    return "Filter Delta"


class DistortionYuvLayer(YuvDeltaLayer):

  def __init__(
      self,
      plane: proto_helpers.Plane = proto_helpers.Plane.Y,
      show_relative=False,
  ):
    super().__init__(plane, show_relative)

  def get_pixels(self, frame: proto_helpers.Frame) -> np.ndarray:
    return frame.pixels[int(self.plane)].distortion

  def get_name(self) -> str:
    return "Distortion"


class SuperblockLayer(VisualizationLayer):
  """Draw the boundaries of the superblocks."""

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    for superblock in frame.superblocks:
      rectangle = superblock.rect
      ax.add_patch(
          plt.Rectangle(
              (rectangle.left_x, rectangle.top_y),
              rectangle.width,
              rectangle.height,
              linewidth=2,
              edgecolor="blue",
              facecolor="none",
          )
      )


class PartitionTreeLayer(VisualizationLayer):
  """Draw the boundaries of the partition tree leaf nodes."""

  def __init__(self, use_chroma=False):
    self.use_chroma = use_chroma
    super().__init__()

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    for superblock in frame.superblocks:
      for rectangle in superblock.get_partition_rects(self.use_chroma):
        ax.add_patch(
            plt.Rectangle(
                (rectangle.left_x, rectangle.top_y),
                rectangle.width,
                rectangle.height,
                linewidth=1,
                edgecolor="red",
                facecolor="none",
            )
        )


class TransformBlockLayer(VisualizationLayer):
  """Draw the boundaries of all transform units."""

  def __init__(self, use_chroma=False):
    self.use_chroma = use_chroma
    super().__init__()

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    for superblock in frame.superblocks:
      for rectangle in superblock.get_transform_rects(
          use_chroma=self.use_chroma
      ):
        ax.add_patch(
            plt.Rectangle(
                (rectangle.left_x, rectangle.top_y),
                rectangle.width,
                rectangle.height,
                linestyle=":",
                linewidth=1,
                edgecolor="green",
                facecolor="none",
            )
        )


class MotionVectorLayer(VisualizationLayer):
  MOTION_VECTOR_PRECISION = 8

  def __init__(self, use_chroma=False):
    self.use_chroma = use_chroma
    super().__init__()

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    # Don't plot motion vectors for intra frames
    if frame.proto.frame_params.frame_type == 0:
      return
    for sb in frame.superblocks:
      for cu in sb.get_coding_units(use_chroma=self.use_chroma):
        for mv in cu.proto.prediction_mode.motion_vectors:
          if mv.ref_frame == -1:
            continue
          cx = cu.proto.position.x + cu.proto.size.width / 2
          cy = cu.proto.position.y + cu.proto.size.height / 2
          dx = mv.dx / self.MOTION_VECTOR_PRECISION
          dy = mv.dy / self.MOTION_VECTOR_PRECISION
          ax.add_patch(
              plt.Arrow(
                  cx,
                  cy,
                  dx,
                  dy,
                  linewidth=1,
                  edgecolor="green",
              )
          )


class SuperblockAnnotationLayer(VisualizationLayer):

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    for sb in frame.superblocks:
      rect = sb.clipped_rect
      cx = rect.x + rect.width / 2
      cy = rect.y + rect.height / 2
      bits = sb.get_total_bits()
      qindex = sb.qindex
      ax.text(
          cx,
          cy,
          f"Bits: {bits:.1f}\nqindex={qindex}",
          verticalalignment="center",
          horizontalalignment="center",
          size="large",
      )


class BitsHeatmapLayer(VisualizationLayer):

  def __init__(
      self,
      use_chroma: bool = False,
      filt: proto_helpers.SymbolFilter | None = None,
  ):
    self.use_chroma = use_chroma
    self.filt = filt
    super().__init__()

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    heatmap = np.zeros((frame.height, frame.width))
    # TODO(comc): allow min / max across multiple frames.
    min_bpp, max_bpp = 1e9, 0
    for sb in frame.superblocks:
      bits_by_cu = list(
          sb.get_bits_per_coding_unit(
              use_chroma=self.use_chroma, filt=self.filt
          )
      )
      for i, rect in enumerate(sb.get_partition_rects(self.use_chroma)):
        bits = bits_by_cu[i]
        bpp = bits / (rect.width * rect.height)
        min_bpp = min(bpp, min_bpp)
        max_bpp = max(bpp, max_bpp)
        heatmap[
            rect.top_y : rect.top_y + rect.height,
            rect.left_x : rect.left_x + rect.width,
        ] = bpp
    width = frame.width
    height = frame.height
    pos = ax.imshow(
        heatmap,
        cmap="YlOrRd",
        interpolation="none",
        vmin=min_bpp,
        vmax=max_bpp,
        extent=[0, width, height, 0],
    )
    axins = inset_locator.inset_axes(
        ax, width="100%", height="5%", loc="lower center", borderpad=-5
    )
    ax.get_figure().colorbar(pos, cax=axins, orientation="horizontal")


class PredictionModeAnnotationLayer(VisualizationLayer):

  def __init__(self, use_chroma: bool = False):
    self.use_chroma = use_chroma
    super().__init__()

  def render(self, frame: proto_helpers.Frame, ax: plt.Axes):
    for sb in frame.superblocks:
      for cu in sb.get_coding_units(use_chroma=self.use_chroma):
        rect = cu.rect
        intra_mode = cu.get_prediction_mode()
        cx = rect.left_x + rect.width / 2
        cy = rect.top_y + rect.height / 2
        mode = intra_mode.removesuffix("_PRED")
        ax.text(cx, cy, mode, horizontalalignment="center", size="small")
