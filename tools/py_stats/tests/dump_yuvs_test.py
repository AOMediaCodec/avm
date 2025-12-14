"""Tests dump_yuvs."""

import os
import pathlib
import tempfile
import unittest

from avm_stats import dump_yuvs

TESTDATA_ENV_VAR = "LIBAVM_TEST_DATA_PATH"
TESTDATA_PATH = pathlib.Path(__file__).parent.resolve() / "testdata"
PROTO_FILENAME = "park_joy_90p_8_420_frame_0000.pb"
TEST_STREAM_WIDTH = 160
TEST_STREAM_HEIGHT = 90


class DumpYuvsTest(unittest.TestCase):

  def setUp(self):
    super().setUp()
    if TESTDATA_ENV_VAR not in os.environ:
      raise RuntimeError(
          f"{TESTDATA_ENV_VAR} environment variable must be set before running"
          " pytest!"
      )
    self.testdata_path = pathlib.Path(os.environ[TESTDATA_ENV_VAR])

  def check_output_filesize(
      self, output_path: pathlib.Path, expected_file_size: int
  ):
    self.assertTrue(output_path.exists())
    size = output_path.stat().st_size
    self.assertEqual(size, expected_file_size)

  def test_all_yuvs_are_created(self):
    proto_path = self.testdata_path / PROTO_FILENAME
    with tempfile.TemporaryDirectory() as temp_dir:
      output_path = pathlib.Path(temp_dir)
      dump_yuvs.dump_all_yuvs(proto_path, output_path)

      expected_outputs = [
          "original",
          "prediction",
          "pre_filtered",
          "reconstruction",
      ]
      # With 8bpp, 4:2:0 chroma subsampling, each pixel is 1.5 bytes.
      expected_file_size = TEST_STREAM_WIDTH * TEST_STREAM_HEIGHT * 3 // 2
      for expected_output in expected_outputs:
        expected_path = output_path / f"{proto_path.stem}_{expected_output}.yuv"
        self.check_output_filesize(expected_path, expected_file_size)
