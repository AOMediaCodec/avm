"""Test yuv_tools helper functions."""

import os
import pathlib
from typing import Tuple
import unittest

from avm_stats import yuv_tools
import numpy as np

TESTDATA_ENV_VAR = "LIBAOM_TEST_DATA_PATH"
TEST_STREAM_WIDTH = 160
TEST_STREAM_HEIGHT = 90


class YuvToolsTest(unittest.TestCase):

  def setUp(self):
    super().setUp()
    if TESTDATA_ENV_VAR not in os.environ:
      raise RuntimeError(
          f"{TESTDATA_ENV_VAR} environment variable must be set before running"
          " pytest!"
      )
    self.testdata_path = pathlib.Path(os.environ[TESTDATA_ENV_VAR])

  def test_upscale_plane(self):
    arr = np.array([[1, 2], [3, 4]])
    expected = np.array([
        [1, 1, 2, 2],
        [1, 1, 2, 2],
        [3, 3, 4, 4],
        [3, 3, 4, 4],
    ])
    upscaled = yuv_tools.upscale(arr, 2)
    self.assertTrue(np.array_equal(upscaled, expected))

  def test_yuv_to_rgb(self):
    y = np.array([[50, 100], [150, 200]])
    u = np.array([[128, 64], [128, 192]])
    v = np.array([[128, 128], [64, 128]])
    expected = np.array([
        [[50, 49, 51], [100, 124, 0]],
        [[77, 186, 151], [200, 174, 255]],
    ])
    rgb = yuv_tools.yuv_to_rgb(y, u, v)
    self.assertTrue(np.array_equal(rgb, expected))

  def check_yuv_seq(
      self,
      yuv_seq: yuv_tools.YuvSequence,
      expected_frames: int,
      expected_first_pixel_rgb: Tuple[int, int, int],
  ):
    self.assertEqual(len(yuv_seq.yuvs), expected_frames)
    for i in range(expected_frames):
      self.assertEqual(
          yuv_seq.yuvs[i].rgb.shape, (TEST_STREAM_HEIGHT, TEST_STREAM_WIDTH, 3)
      )

    first_pixel_rgb = tuple(yuv_seq.yuvs[0].rgb[0, 0])
    self.assertEqual(first_pixel_rgb, expected_first_pixel_rgb)

  def test_parse_y4m(self):
    num_frames = 3
    yuv_path = self.testdata_path / "park_joy_90p_8_420.y4m"
    yuv_seq = yuv_tools.parse_raw_yuv(
        yuv_path,
        width=TEST_STREAM_WIDTH,
        height=TEST_STREAM_HEIGHT,
        num_frames=3,
    )
    self.check_yuv_seq(
        yuv_seq, num_frames, expected_first_pixel_rgb=(70, 133, 0)
    )
