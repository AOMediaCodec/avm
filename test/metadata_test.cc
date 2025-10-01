/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "aom/aom_codec.h"
#include "aom/aom_image.h"
#include "aom/internal/aom_image_internal.h"
#include "aom_scale/yv12config.h"
#include "av1/encoder/bitstream.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/video_source.h"

namespace {
const size_t kMetadataPayloadSizeT35 = 24;
// 0xB5 stands for the itut t35 metadata country code for the Unites States
const uint8_t kMetadataPayloadT35[kMetadataPayloadSizeT35] = {
  0xB5, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
  0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

const size_t kMetadataPayloadSizeCll = 4;
const uint8_t kMetadataPayloadCll[kMetadataPayloadSizeCll] = { 0xB5, 0x01, 0x02,
                                                               0x03 };

#if CONFIG_AV1_ENCODER

const size_t kMetadataObuSizeT35 = 28;
const uint8_t kMetadataObuT35[kMetadataObuSizeT35] = {
  0x1B, 0x14, 0x04, 0xB5, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x80
};
const size_t kMetadataObuSizeMdcv = 28;
const uint8_t kMetadataObuMdcv[kMetadataObuSizeMdcv] = {
  0x1B, 0x14, 0x02, 0xB5, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x80
};
const size_t kMetadataObuSizeCll = 8;
const uint8_t kMetadataObuCll[kMetadataObuSizeCll] = { 0x07, 0x14, 0x01, 0xB5,
                                                       0x01, 0x02, 0x03, 0x80 };

class MetadataEncodeTest
    : public ::libaom_test::CodecTestWithParam<libaom_test::TestMode>,
      public ::libaom_test::EncoderTest {
 protected:
  MetadataEncodeTest() : EncoderTest(GET_PARAM(0)) {}

  virtual ~MetadataEncodeTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
    set_cpu_used_ = 5;
    max_partition_size_ = 16;
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, set_cpu_used_);
      encoder->Control(AV1E_SET_MAX_PARTITION_SIZE, max_partition_size_);
      encoder->Control(AV1E_SET_ENABLE_KEYFRAME_FILTERING, 0);
    }

    aom_image_t *current_frame = video->img();
    if (current_frame) {
      if (current_frame->metadata) aom_img_remove_metadata(current_frame);
      ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                                     kMetadataPayloadT35, 0, AOM_MIF_ANY_FRAME),
                -1);
      ASSERT_EQ(
          aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35, NULL,
                               kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME),
          -1);
      ASSERT_EQ(aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                                     NULL, 0, AOM_MIF_ANY_FRAME),
                -1);
      ASSERT_EQ(
          aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                               kMetadataPayloadT35, kMetadataPayloadSizeT35,
                               AOM_MIF_ANY_FRAME),
          0);

      ASSERT_EQ(
          aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_HDR_MDCV,
                               kMetadataPayloadT35, kMetadataPayloadSizeT35,
                               AOM_MIF_KEY_FRAME),
          0);

      ASSERT_EQ(
          aom_img_add_metadata(current_frame, OBU_METADATA_TYPE_HDR_CLL,
                               kMetadataPayloadCll, kMetadataPayloadSizeCll,
                               AOM_MIF_KEY_FRAME),
          0);
    }
  }

  virtual void FramePktHook(const aom_codec_cx_pkt_t *pkt,
                            ::libaom_test::DxDataIterator *dec_iter) {
    (void)dec_iter;
    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT ||
        pkt->kind == AOM_CODEC_CX_FRAME_NULL_PKT) {
      const size_t bitstream_size = pkt->data.frame.sz;
      const uint8_t *bitstream =
          static_cast<const uint8_t *>(pkt->data.frame.buf);
      bool is_keyframe =
          (pkt->data.frame.flags & AOM_FRAME_IS_KEY) ? true : false;

      // look for valid metadatas in bitstream
      bool itut_t35_metadata_found = false;
      if (bitstream_size >= kMetadataObuSizeT35) {
        for (size_t i = 0; i <= bitstream_size - kMetadataObuSizeT35; ++i) {
          if (memcmp(bitstream + i, kMetadataObuT35, kMetadataObuSizeT35) ==
              0) {
            itut_t35_metadata_found = true;
          }
        }
      }
      ASSERT_EQ(itut_t35_metadata_found, 1u);

      if (is_keyframe) {
        // Testing for HDR MDCV metadata
        bool hdr_mdcv_metadata_found = false;
        if (bitstream_size >= kMetadataObuSizeMdcv) {
          for (size_t i = 0; i <= bitstream_size - kMetadataObuSizeMdcv; ++i) {
            if (memcmp(bitstream + i, kMetadataObuMdcv, kMetadataObuSizeMdcv) ==
                0) {
              hdr_mdcv_metadata_found = true;
            }
          }
        }
        ASSERT_TRUE(hdr_mdcv_metadata_found);

        // Testing for HDR CLL metadata
        bool hdr_cll_metadata_found = false;
        if (bitstream_size >= kMetadataObuSizeCll) {
          for (size_t i = 0; i <= bitstream_size - kMetadataObuSizeCll; ++i) {
            if (memcmp(bitstream + i, kMetadataObuCll, kMetadataObuSizeCll) ==
                0) {
              hdr_cll_metadata_found = true;
            }
          }
        }
        ASSERT_TRUE(hdr_cll_metadata_found);
      }
    }
  }

  virtual void DecompressedFrameHook(const aom_image_t &img,
                                     aom_codec_pts_t pts) {
    ASSERT_TRUE(img.metadata != nullptr);
    unsigned n_meta = pts == 0 ? 3 : 1;

    ASSERT_EQ(img.metadata->sz, n_meta);

    ASSERT_EQ(kMetadataPayloadSizeT35, img.metadata->metadata_array[0]->sz);
    EXPECT_EQ(
        memcmp(kMetadataPayloadT35, img.metadata->metadata_array[0]->payload,
               kMetadataPayloadSizeT35),
        0);

    if (n_meta == 3) {
      // Check keyframe-only metadata
      ASSERT_EQ(kMetadataPayloadSizeT35, img.metadata->metadata_array[1]->sz);
      EXPECT_EQ(
          memcmp(kMetadataPayloadT35, img.metadata->metadata_array[1]->payload,
                 kMetadataPayloadSizeT35),
          0);

      ASSERT_EQ(kMetadataPayloadSizeCll, img.metadata->metadata_array[2]->sz);
      EXPECT_EQ(
          memcmp(kMetadataPayloadCll, img.metadata->metadata_array[2]->payload,
                 kMetadataPayloadSizeCll),
          0);
    }
  }

  int set_cpu_used_;
  int max_partition_size_;
};

TEST_P(MetadataEncodeTest, TestMetadataEncoding) {
  ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 5);
  init_flags_ = AOM_CODEC_USE_PSNR;

  cfg_.g_w = 352;
  cfg_.g_h = 288;

  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 600;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_min_quantizer = 8;
  cfg_.rc_max_quantizer = 164;
  cfg_.rc_undershoot_pct = 50;
  cfg_.rc_overshoot_pct = 50;
  cfg_.rc_end_usage = AOM_CBR;
  cfg_.kf_mode = AOM_KF_AUTO;
  cfg_.g_lag_in_frames = 1;
  cfg_.kf_min_dist = cfg_.kf_max_dist = 3000;
  // Disable error_resilience mode.
  cfg_.g_error_resilient = 0;
  // Run at low bitrate.
  cfg_.rc_target_bitrate = 40;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV1_INSTANTIATE_TEST_SUITE(MetadataEncodeTest,
                           ::testing::Values(::libaom_test::kOnePassGood));

#endif  // CONFIG_AV1_ENCODER
}  // namespace

TEST(MetadataTest, MetadataAllocation) {
  aom_metadata_t *metadata =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME);
  ASSERT_NE(metadata, nullptr);
  aom_img_metadata_free(metadata);
}

TEST(MetadataTest, MetadataArrayAllocation) {
  aom_metadata_array_t *metadata_array = aom_img_metadata_array_alloc(2);
  ASSERT_NE(metadata_array, nullptr);

  metadata_array->metadata_array[0] =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME);
  metadata_array->metadata_array[1] =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME);

  aom_img_metadata_array_free(metadata_array);
}

TEST(MetadataTest, AddMetadataToImage) {
  aom_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(aom_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME),
            0);
  aom_img_metadata_array_free(image.metadata);
  EXPECT_EQ(aom_img_add_metadata(NULL, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME),
            -1);
}

TEST(MetadataTest, RemoveMetadataFromImage) {
  aom_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(aom_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME),
            0);
  aom_img_remove_metadata(&image);
  aom_img_remove_metadata(NULL);
}

TEST(MetadataTest, CopyMetadataToFrameBuffer) {
  YV12_BUFFER_CONFIG yvBuf;
  yvBuf.metadata = NULL;

  aom_metadata_array_t *metadata_array = aom_img_metadata_array_alloc(1);
  ASSERT_NE(metadata_array, nullptr);

  metadata_array->metadata_array[0] =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME);

  // Metadata_array
  int status = aom_copy_metadata_to_frame_buffer(&yvBuf, metadata_array);
  EXPECT_EQ(status, 0);
  status = aom_copy_metadata_to_frame_buffer(NULL, metadata_array);
  EXPECT_EQ(status, -1);
  aom_img_metadata_array_free(metadata_array);

  // Metadata_array_2
  aom_metadata_array_t *metadata_array_2 = aom_img_metadata_array_alloc(0);
  ASSERT_NE(metadata_array_2, nullptr);
  status = aom_copy_metadata_to_frame_buffer(&yvBuf, metadata_array_2);
  EXPECT_EQ(status, -1);
  aom_img_metadata_array_free(metadata_array_2);

  // YV12_BUFFER_CONFIG
  status = aom_copy_metadata_to_frame_buffer(&yvBuf, NULL);
  EXPECT_EQ(status, -1);
  aom_remove_metadata_from_frame_buffer(&yvBuf);
  aom_remove_metadata_from_frame_buffer(NULL);
}

TEST(MetadataTest, GetMetadataFromImage) {
  aom_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(aom_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AOM_MIF_ANY_FRAME),
            0);

  EXPECT_TRUE(aom_img_get_metadata(NULL, 0) == NULL);
  EXPECT_TRUE(aom_img_get_metadata(&image, 1u) == NULL);
  EXPECT_TRUE(aom_img_get_metadata(&image, 10u) == NULL);

  const aom_metadata_t *metadata = aom_img_get_metadata(&image, 0);
  ASSERT_TRUE(metadata != NULL);
  ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
  EXPECT_EQ(
      memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
      0);

  aom_img_metadata_array_free(image.metadata);
}

TEST(MetadataTest, ReadMetadatasFromImage) {
  aom_image_t image;
  image.metadata = NULL;

  uint32_t types[3];
  types[0] = OBU_METADATA_TYPE_ITUT_T35;
  types[1] = OBU_METADATA_TYPE_HDR_CLL;
  types[2] = OBU_METADATA_TYPE_HDR_MDCV;

  ASSERT_EQ(aom_img_add_metadata(&image, types[0], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AOM_MIF_ANY_FRAME),
            0);
  ASSERT_EQ(aom_img_add_metadata(&image, types[1], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AOM_MIF_KEY_FRAME),
            0);
  ASSERT_EQ(aom_img_add_metadata(&image, types[2], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AOM_MIF_KEY_FRAME),
            0);

  size_t number_metadata = aom_img_num_metadata(&image);
  ASSERT_EQ(number_metadata, 3u);
  for (size_t i = 0; i < number_metadata; ++i) {
    const aom_metadata_t *metadata = aom_img_get_metadata(&image, i);
    ASSERT_TRUE(metadata != NULL);
    ASSERT_EQ(metadata->type, types[i]);
    ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
    EXPECT_EQ(
        memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
        0);
  }
  aom_img_metadata_array_free(image.metadata);
}

#if CONFIG_BAND_METADATA
#include "av1/common/banding_metadata.h"

TEST(MetadataTest, BandingHintsMetadata) {
  // Create a test banding metadata structure for encoding
  aom_banding_hints_metadata_t encode_metadata;
  memset(&encode_metadata, 0, sizeof(encode_metadata));

  // Set up test values
  encode_metadata.coding_banding_present_flag = 1;
  encode_metadata.source_banding_present_flag = 0;
  encode_metadata.banding_hints_flag = 1;
  encode_metadata.three_color_components = 1;

  // Set per-component information
  for (int i = 0; i < 3; i++) {
    encode_metadata.banding_in_component_present_flag[i] = 1;
    encode_metadata.max_band_width_minus4[i] = 4 + i;  // 6-bit value
    encode_metadata.max_band_step_minus1[i] = 2 + i;   // 4-bit value
  }

  // Set band units information
  encode_metadata.band_units_information_present_flag = 1;
  encode_metadata.num_band_units_rows_minus_1 = 3;  // 4 rows
  encode_metadata.num_band_units_cols_minus_1 = 3;  // 4 cols
  encode_metadata.varying_size_band_units_flag = 1;
  encode_metadata.band_block_in_luma_samples = 2;  // 3-bit value

  // Set varying size information
  for (int r = 0; r <= encode_metadata.num_band_units_rows_minus_1; r++) {
    encode_metadata.vert_size_in_band_blocks_minus1[r] = r + 1;
  }
  for (int c = 0; c <= encode_metadata.num_band_units_cols_minus_1; c++) {
    encode_metadata.horz_size_in_band_blocks_minus1[c] = c + 2;
  }

  // Set per-tile banding flags
  for (int r = 0; r <= encode_metadata.num_band_units_rows_minus_1; r++) {
    for (int c = 0; c <= encode_metadata.num_band_units_cols_minus_1; c++) {
      encode_metadata.banding_in_band_unit_present_flag[r][c] = (r + c) % 2;
    }
  }

  // Test encoding to payload
  uint8_t payload[256];
  size_t payload_size = sizeof(payload);
  ASSERT_EQ(aom_encode_banding_hints_metadata(&encode_metadata, payload,
                                              &payload_size),
            0);
  ASSERT_GT(payload_size, 0u);

  // Test decoding from payload (using separate instance of same structure type)
  aom_banding_hints_metadata_t decode_metadata;
  ASSERT_EQ(aom_decode_banding_hints_metadata(payload, payload_size,
                                              &decode_metadata),
            0);

  // Verify that the decoded structure matches the encoded structure
  EXPECT_EQ(decode_metadata.coding_banding_present_flag,
            encode_metadata.coding_banding_present_flag);
  EXPECT_EQ(decode_metadata.source_banding_present_flag,
            encode_metadata.source_banding_present_flag);
  EXPECT_EQ(decode_metadata.banding_hints_flag,
            encode_metadata.banding_hints_flag);
  EXPECT_EQ(decode_metadata.three_color_components,
            encode_metadata.three_color_components);

  // Verify per-component information
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(decode_metadata.banding_in_component_present_flag[i],
              encode_metadata.banding_in_component_present_flag[i]);
    EXPECT_EQ(decode_metadata.max_band_width_minus4[i],
              encode_metadata.max_band_width_minus4[i]);
    EXPECT_EQ(decode_metadata.max_band_step_minus1[i],
              encode_metadata.max_band_step_minus1[i]);
  }

  // Verify band units information
  EXPECT_EQ(decode_metadata.band_units_information_present_flag,
            encode_metadata.band_units_information_present_flag);
  EXPECT_EQ(decode_metadata.num_band_units_rows_minus_1,
            encode_metadata.num_band_units_rows_minus_1);
  EXPECT_EQ(decode_metadata.num_band_units_cols_minus_1,
            encode_metadata.num_band_units_cols_minus_1);
  EXPECT_EQ(decode_metadata.varying_size_band_units_flag,
            encode_metadata.varying_size_band_units_flag);
  EXPECT_EQ(decode_metadata.band_block_in_luma_samples,
            encode_metadata.band_block_in_luma_samples);

  // Verify varying size information
  for (int r = 0; r <= encode_metadata.num_band_units_rows_minus_1; r++) {
    EXPECT_EQ(decode_metadata.vert_size_in_band_blocks_minus1[r],
              encode_metadata.vert_size_in_band_blocks_minus1[r]);
  }
  for (int c = 0; c <= encode_metadata.num_band_units_cols_minus_1; c++) {
    EXPECT_EQ(decode_metadata.horz_size_in_band_blocks_minus1[c],
              encode_metadata.horz_size_in_band_blocks_minus1[c]);
  }

  // Verify per-tile banding flags
  for (int r = 0; r <= encode_metadata.num_band_units_rows_minus_1; r++) {
    for (int c = 0; c <= encode_metadata.num_band_units_cols_minus_1; c++) {
      EXPECT_EQ(decode_metadata.banding_in_band_unit_present_flag[r][c],
                encode_metadata.banding_in_band_unit_present_flag[r][c]);
    }
  }
}

TEST(MetadataTest, BandingHintsImageMetadata) {
  aom_image_t image;
  image.metadata = NULL;

  // Create test banding metadata
  aom_banding_hints_metadata_t banding_metadata;
  memset(&banding_metadata, 0, sizeof(banding_metadata));
  banding_metadata.coding_banding_present_flag = 1;
  banding_metadata.source_banding_present_flag = 1;
  banding_metadata.banding_hints_flag = 1;
  banding_metadata.three_color_components = 0;  // Only component 0
  banding_metadata.banding_in_component_present_flag[0] = 1;
  banding_metadata.max_band_width_minus4[0] = 10;
  banding_metadata.max_band_step_minus1[0] = 5;
  banding_metadata.band_units_information_present_flag = 0;

  // Add banding metadata to image
  ASSERT_EQ(aom_img_add_banding_hints_metadata(&image, &banding_metadata,
                                               AOM_MIF_ANY_FRAME),
            0);

  // Verify metadata was added
  ASSERT_TRUE(image.metadata != nullptr);
  ASSERT_EQ(image.metadata->sz, 1u);
  ASSERT_EQ(image.metadata->metadata_array[0]->type,
            OBU_METADATA_TYPE_BANDING_HINTS);
  ASSERT_GT(image.metadata->metadata_array[0]->sz, 0u);

  // Test decoding the metadata from the image (using separate instance of same
  // structure type)
  aom_banding_hints_metadata_t decoded_metadata;
  ASSERT_EQ(aom_decode_banding_hints_metadata(
                image.metadata->metadata_array[0]->payload,
                image.metadata->metadata_array[0]->sz, &decoded_metadata),
            0);

  // Verify the decoded values match
  EXPECT_EQ(decoded_metadata.coding_banding_present_flag,
            banding_metadata.coding_banding_present_flag);
  EXPECT_EQ(decoded_metadata.source_banding_present_flag,
            banding_metadata.source_banding_present_flag);
  EXPECT_EQ(decoded_metadata.banding_hints_flag,
            banding_metadata.banding_hints_flag);
  EXPECT_EQ(decoded_metadata.three_color_components,
            banding_metadata.three_color_components);
  EXPECT_EQ(decoded_metadata.banding_in_component_present_flag[0],
            banding_metadata.banding_in_component_present_flag[0]);
  EXPECT_EQ(decoded_metadata.max_band_width_minus4[0],
            banding_metadata.max_band_width_minus4[0]);
  EXPECT_EQ(decoded_metadata.max_band_step_minus1[0],
            banding_metadata.max_band_step_minus1[0]);
  EXPECT_EQ(decoded_metadata.band_units_information_present_flag,
            banding_metadata.band_units_information_present_flag);

  aom_img_metadata_array_free(image.metadata);
}
#endif  // CONFIG_BAND_METADATA
