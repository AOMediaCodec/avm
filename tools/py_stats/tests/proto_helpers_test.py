"""Test proto_helpers wrapper classes."""

import os
import pathlib
import unittest

from avm_stats import avm_frame_pb2
from avm_stats import proto_helpers
from avm_stats import yuv_tools
import numpy as np

TESTDATA_ENV_VAR = "LIBAVM_TEST_DATA_PATH"
TEST_SEQUENCE_NAME = "park_joy_90p_8_420"
STREAM_FILENAME = f"{TEST_SEQUENCE_NAME}.ivf"
PROTO_FILENAME = f"{TEST_SEQUENCE_NAME}_frame_0000.pb"
TEST_STREAM_WIDTH = 160
TEST_STREAM_HEIGHT = 90


class ProtoHelpersTest(unittest.TestCase):

  def setUp(self):
    super().setUp()
    if TESTDATA_ENV_VAR not in os.environ:
      raise RuntimeError(
          f"{TESTDATA_ENV_VAR} environment variable must be set before running"
          " pytest!"
      )
    self.testdata_path = pathlib.Path(os.environ[TESTDATA_ENV_VAR])
    proto_path = self.testdata_path / PROTO_FILENAME
    with open(proto_path, "rb") as f:
      self.frame_proto = avm_frame_pb2.Frame.FromString(f.read())
      self.frame = proto_helpers.Frame(self.frame_proto)
    stream_path = self.testdata_path / STREAM_FILENAME
    self.stream_size_bytes = stream_path.stat().st_size

  def test_frame_dimensions(self):
    self.assertEqual(self.frame.width, TEST_STREAM_WIDTH)
    self.assertEqual(self.frame.height, TEST_STREAM_HEIGHT)
    self.assertEqual(
        self.frame.reconstruction_rgb.shape,
        (TEST_STREAM_HEIGHT, TEST_STREAM_WIDTH, 3),
    )

  def test_clip_rect(self):
    rect = proto_helpers.Rectangle(
        left_x=50.0, top_y=50.0, width=500.0, height=500.0
    )
    clipped = self.frame.clip_rect(rect)
    self.assertEqual(clipped.width, TEST_STREAM_WIDTH - 50.0)
    self.assertEqual(clipped.height, TEST_STREAM_HEIGHT - 50.0)

  def test_plane_subsample(self):
    even_width = 100
    odd_width = 101
    y_plane = proto_helpers.Plane.Y
    u_plane = proto_helpers.Plane.U
    self.assertEqual(
        proto_helpers.subsample_dimension(even_width, y_plane), 100
    )
    self.assertEqual(proto_helpers.subsample_dimension(odd_width, y_plane), 101)
    self.assertEqual(proto_helpers.subsample_dimension(even_width, u_plane), 50)
    self.assertEqual(proto_helpers.subsample_dimension(odd_width, u_plane), 51)

  def compare_yuv_planes(
      self,
      luma: np.ndarray,
      chroma_u: np.ndarray,
      chroma_v: np.ndarray,
      expected_yuv_path: str,
  ):
    """Compares luma and chroma planes to a golden YUV.

    Args:
      luma: Luma plane to test.
      chroma_u: Chroma U plane to test.
      chroma_v: Chroma V plane to test.
      expected_yuv_path: Path to the golden YUV to load and compare against.
    """
    expected_yuv = yuv_tools.parse_raw_yuv(
        expected_yuv_path, TEST_STREAM_WIDTH, TEST_STREAM_HEIGHT, 1, bit_depth=8
    ).yuvs[0]
    self.assertTrue(np.array_equal(luma, expected_yuv.y))
    self.assertTrue(np.array_equal(chroma_u, expected_yuv.u))
    self.assertTrue(np.array_equal(chroma_v, expected_yuv.v))

  def compare_luma_diff(
      self,
      luma_diff: np.ndarray,
      first_yuv_path: str,
      second_yuv_path: str,
  ):
    """Compares a luma difference against the difference of two golden YUVs.

    Args:
      luma_diff: Difference between two luma planes to test, e.g. the distortion
        (reconstruction - original).
      first_yuv_path: Path to the first golden YUV.
      second_yuv_path: Path to the first second YUV. This will be subtracted
        from the first and compared to luma_diff.
    """
    first_yuv = yuv_tools.parse_raw_yuv(
        first_yuv_path, TEST_STREAM_WIDTH, TEST_STREAM_HEIGHT, 1, bit_depth=8
    ).yuvs[0]
    second_yuv = yuv_tools.parse_raw_yuv(
        second_yuv_path, TEST_STREAM_WIDTH, TEST_STREAM_HEIGHT, 1, bit_depth=8
    ).yuvs[0]
    expected_luma_diff = first_yuv.y.astype(np.int16) - second_yuv.y.astype(
        np.int16
    )
    np.testing.assert_equal(
        luma_diff,
        expected_luma_diff,
    )

  def test_pixel_data_original(self):
    luma = self.frame.pixels[0].original
    chroma_u = self.frame.pixels[1].original
    chroma_v = self.frame.pixels[2].original
    orig_yuv_path = (
        self.testdata_path / f"{TEST_SEQUENCE_NAME}_frame_0000_original.yuv"
    )

    self.compare_yuv_planes(luma, chroma_u, chroma_v, orig_yuv_path)

  def remove_original_data_from_proto(self):
    for superblock in self.frame_proto.superblocks:
      for plane in superblock.pixel_data:
        plane.ClearField("original")
    self.frame = proto_helpers.Frame(self.frame_proto)

  def test_pixel_data_original_missing(self):
    self.remove_original_data_from_proto()
    self.assertIsNone(self.frame.pixels[0].original)
    self.assertIsNone(self.frame.pixels[1].original)
    self.assertIsNone(self.frame.pixels[2].original)
    self.assertIsNone(self.frame.pixels[0].distortion)
    self.assertIsNone(self.frame.pixels[1].distortion)
    self.assertIsNone(self.frame.pixels[2].distortion)
    self.assertIsNone(self.frame.original_rgb)

  def test_pixel_data_reconstruction(self):
    luma = self.frame.pixels[0].reconstruction
    chroma_u = self.frame.pixels[1].reconstruction
    chroma_v = self.frame.pixels[2].reconstruction
    orig_yuv_path = (
        self.testdata_path
        / f"{TEST_SEQUENCE_NAME}_frame_0000_reconstruction.yuv"
    )

    self.compare_yuv_planes(luma, chroma_u, chroma_v, orig_yuv_path)

  def test_pixel_data_pre_filtered(self):
    luma = self.frame.pixels[0].pre_filtered
    chroma_u = self.frame.pixels[1].pre_filtered
    chroma_v = self.frame.pixels[2].pre_filtered
    orig_yuv_path = (
        self.testdata_path / f"{TEST_SEQUENCE_NAME}_frame_0000_pre_filtered.yuv"
    )
    self.compare_yuv_planes(luma, chroma_u, chroma_v, orig_yuv_path)

  def test_luma_distortion(self):
    luma_distortion = self.frame.pixels[0].distortion
    orig_yuv_path = (
        self.testdata_path / f"{TEST_SEQUENCE_NAME}_frame_0000_original.yuv"
    )
    recon_yuv_path = (
        self.testdata_path
        / f"{TEST_SEQUENCE_NAME}_frame_0000_reconstruction.yuv"
    )
    self.compare_luma_diff(luma_distortion, orig_yuv_path, recon_yuv_path)

  def test_filter_delta(self):
    luma_filter_delta = self.frame.pixels[0].filter_delta
    recon_yuv_path = (
        self.testdata_path
        / f"{TEST_SEQUENCE_NAME}_frame_0000_reconstruction.yuv"
    )
    pre_filtered_yuv_path = (
        self.testdata_path / f"{TEST_SEQUENCE_NAME}_frame_0000_pre_filtered.yuv"
    )
    self.compare_luma_diff(
        luma_filter_delta, recon_yuv_path, pre_filtered_yuv_path
    )

  def test_pixel_data_prediction(self):
    luma = self.frame.pixels[0].prediction
    chroma_u = self.frame.pixels[1].prediction
    chroma_v = self.frame.pixels[2].prediction
    orig_yuv_path = (
        self.testdata_path / f"{TEST_SEQUENCE_NAME}_frame_0000_prediction.yuv"
    )
    self.compare_yuv_planes(luma, chroma_u, chroma_v, orig_yuv_path)

  def test_coding_unit_count(self):
    first_sb = self.frame.superblocks[0]
    # Since the test stream (Park Joy) is moderately complex and was encoded at
    # a reasonable quality (qp=185), we expect a decent number of coding units
    # to be present in the partition tree. 50 is a reasonable lower bound to
    # use as a quick sanity check (although in practice it's closer to 150).
    # Refer to generate_testdata.sh for exact encoder args for this test stream.
    minimum_num_coding_units = 50
    # For an upper bound, assume a 128x128 superblock was partitioned entirely
    # into 4x4 coding units.
    maximum_num_coding_units = (128 * 128) // (4 * 4)
    num_coding_units = len(list(first_sb.get_coding_units()))
    self.assertLessEqual(num_coding_units, maximum_num_coding_units)
    self.assertGreaterEqual(num_coding_units, minimum_num_coding_units)

  def test_transform_unit_count(self):
    first_sb = self.frame.superblocks[0]
    # As a lower bound, there will be at least one transform unit per coding
    # unit.
    num_coding_units = len(list(first_sb.get_coding_units()))
    num_transform_units = len(list(first_sb.get_transform_rects()))
    minimum_num_transform_units = num_coding_units
    # Similar to the coding unit upper bound, assume a 128x128 superblock made
    # entirely of 4x4 transform units.
    maximum_num_transform_units = (128 * 128) // (4 * 4)
    self.assertLessEqual(num_transform_units, maximum_num_transform_units)
    self.assertGreaterEqual(num_transform_units, minimum_num_transform_units)

  def test_partition_rects_cover_whole_frame_without_overlap(self):
    pixel_hit_count = np.zeros(
        (TEST_STREAM_HEIGHT, TEST_STREAM_WIDTH), dtype=np.int32
    )
    for superblock in self.frame.superblocks:
      for rect in superblock.get_partition_rects():
        pixel_hit_count[
            rect.top_y : rect.top_y + rect.height,
            rect.left_x : rect.left_x + rect.width,
        ] += 1

    np.testing.assert_equal(
        pixel_hit_count,
        np.ones((TEST_STREAM_HEIGHT, TEST_STREAM_WIDTH), dtype=np.int32),
    )

  def test_total_bits_of_first_superblock(self):
    first_sb = self.frame.superblocks[0]
    # The majority of the bits in this stream will be spent in the first SB of
    # the first frame, so half the total size of the stream is a reasonable
    # lower bound.
    minimum_expected_bits = 0.5 * self.stream_size_bytes * 8
    # In the extreme case, ALL of the bits in the stream are used to encode
    # this SB, so this value is the upper bound.
    maximum_expected_bits = self.stream_size_bytes * 8

    total_bits = first_sb.get_total_bits()
    self.assertLessEqual(total_bits, maximum_expected_bits)
    self.assertGreaterEqual(total_bits, minimum_expected_bits)

  def test_total_intra_mode_bits_of_first_superblock(self):
    first_sb = self.frame.superblocks[0]
    num_coding_units = len(list(first_sb.get_coding_units()))
    # As a lower bound, assume half a bit is used per coding unit to encode
    # the luma prediction mode (in practice it's closer to 4 bits).
    minimum_expected_bits = num_coding_units * 0.5
    # In the extreme case, ALL of the bits in the first SB are used to encode
    # the luma prediction mode.
    maximum_expected_bits = first_sb.get_total_bits()

    def intra_mode_filter(sym: proto_helpers.Symbol):
      return sym.source_function == "read_intra_luma_mode"

    total_bits = first_sb.get_total_bits(filt=intra_mode_filter)
    self.assertLessEqual(total_bits, maximum_expected_bits)
    self.assertGreaterEqual(total_bits, minimum_expected_bits)

  def test_first_coding_unit_prediction_mode(self):
    first_superblock = self.frame.superblocks[0]
    first_luma_coding_unit = list(
        first_superblock.get_coding_units(use_chroma=False)
    )[0]
    first_chroma_coding_unit = list(
        first_superblock.get_coding_units(use_chroma=True)
    )[0]
    # All intra prediction modes end with this suffix.
    self.assertTrue(
        first_luma_coding_unit.get_prediction_mode().endswith("_PRED")
    )
    # All chroma prediction modes start with this prefix.
    self.assertTrue(
        first_chroma_coding_unit.get_prediction_mode().startswith("UV_")
    )
