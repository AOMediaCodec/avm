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

#include "avm/avm_codec.h"
#include "avm/avm_image.h"
#include "avm/internal/avm_image_internal.h"
#include "avm_scale/yv12config.h"
#include "av2/encoder/bitstream.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/video_source.h"

namespace {
const size_t kMetadataPayloadSizeT35 = 24;
// itu_t_t35_country_code = 0xB5 (USA)
// itu_t_t35_terminal_provider_code = 0x5890 (AVM)
// itu_t_t35_terminal_provider_oriented_code = 0xFE (not specified, for testing
// purposes only)
const uint8_t kMetadataPayloadT35[kMetadataPayloadSizeT35] = {
  0xB5, 0x58, 0x90, 0xFE, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
  0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

const size_t kMetadataPayloadSizeUserDataUnregistered = 32;
// uuid_iso_iec_11578 (16 bytes) + user data (16 bytes)
// UUID: 550e8400-e29b-41d4-a716-446655440000 (example UUID)
const uint8_t kMetadataPayloadUserDataUnregistered
    [kMetadataPayloadSizeUserDataUnregistered] = {
      0x55, 0x0e, 0x84, 0x00, 0xe2, 0x9b, 0x41, 0xd4, 0xa7, 0x16, 0x44,
      0x66, 0x55, 0x44, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
      0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
    };

const size_t kMetadataPayloadSizeCll = 4;
const uint8_t kMetadataPayloadCll[kMetadataPayloadSizeCll] = { 0xDE, 0xAD, 0xBE,
                                                               0xEF };
const size_t kMetadataPayloadSizeMdcv = 24;
const uint8_t kMetadataPayloadMdcv[kMetadataPayloadSizeMdcv] = {
  0x01, 0x00,              // primary_chromaticity_x[0]
  0x02, 0x00,              // primary_chromaticity_y[0]
  0x03, 0x00,              // primary_chromaticity_x[1]
  0x04, 0x00,              // primary_chromaticity_y[1]
  0x05, 0x00,              // primary_chromaticity_x[2]
  0x06, 0x00,              // primary_chromaticity_y[2]
  0x07, 0x00,              // white_point_chromaticity_x
  0x08, 0x00,              // white_point_chromaticity_y
  0x00, 0x00, 0x00, 0x09,  // luminance_max
  0x00, 0x00, 0x00, 0x0A   // luminance_min
};

#if CONFIG_AV2_ENCODER
const size_t kMetadataObuSizeT35 = 29;
const uint8_t kMetadataObuT35[kMetadataObuSizeT35] = {
  0x04,        // muh_metadata_type(leb128) = 4 (ITU-T T.35 metadata unit)
  0x06,        // muh_header_size(7) = 3 + muh_cancel_flag(1) = 0
  0x18,        // muh_payload_size(leb128) = 24
  0x00, 0x00,  // layer_idc(3) persistence_idc(3) priority(8) reserved(2)
  0xB5, 0x58, 0x90, 0xFE, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
  0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

const size_t kMetadataObuSizeMdcv = 29;
const uint8_t kMetadataObuMdcv[kMetadataObuSizeMdcv] = {
  0x02,        // muh_metadata_type(leb128) = 2 (Mastering display color volume)
  0x06,        // muh_header_size(7) = 3 + muh_cancel_flag(1) = 0
  0x18,        // muh_payload_size(leb128) = 24
  0x00, 0x00,  // layer_idc(3) persistence_idc(3) priority(8) reserved(2)
  0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00,
  0x07, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x0A
};

const size_t kMetadataObuSizeCll = 9;
const uint8_t kMetadataObuCll[kMetadataObuSizeCll] = {
  0x01,        // muh_metadata_type(leb128) = 1 (Content light level)
  0x06,        // muh_header_size(7) = 3 + muh_cancel_flag(1) = 0
  0x04,        // muh_payload_size(leb128) = 4
  0x00, 0x00,  // layer_idc(3) persistence_idc(3) priority(8) reserved(2)
  0xDE, 0xAD, 0xBE, 0xEF
};

class MetadataEncodeTest
    : public ::libavm_test::CodecTestWithParam<libavm_test::TestMode>,
      public ::libavm_test::EncoderTest {
 protected:
  MetadataEncodeTest() : EncoderTest(GET_PARAM(0)) {}

  virtual ~MetadataEncodeTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
    set_cpu_used_ = 5;
    max_partition_size_ = 16;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, set_cpu_used_);
      encoder->Control(AV2E_SET_MAX_PARTITION_SIZE, max_partition_size_);
      encoder->Control(AV2E_SET_ENABLE_KEYFRAME_FILTERING, 0);
    }

    avm_image_t *current_frame = video->img();
    if (current_frame) {
      if (current_frame->metadata) avm_img_remove_metadata(current_frame);
      ASSERT_EQ(avm_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                                     kMetadataPayloadT35, 0, AVM_MIF_ANY_FRAME),
                -1);
      ASSERT_EQ(
          avm_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35, NULL,
                               kMetadataPayloadSizeT35, AVM_MIF_ANY_FRAME),
          -1);
      ASSERT_EQ(avm_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                                     NULL, 0, AVM_MIF_ANY_FRAME),
                -1);
      ASSERT_EQ(
          avm_img_add_metadata(current_frame, OBU_METADATA_TYPE_ITUT_T35,
                               kMetadataPayloadT35, kMetadataPayloadSizeT35,
                               AVM_MIF_ANY_FRAME),
          0);

      ASSERT_EQ(
          avm_img_add_metadata(current_frame, OBU_METADATA_TYPE_HDR_MDCV,
                               kMetadataPayloadMdcv, kMetadataPayloadSizeMdcv,
                               AVM_MIF_KEY_FRAME),
          0);

      ASSERT_EQ(
          avm_img_add_metadata(current_frame, OBU_METADATA_TYPE_HDR_CLL,
                               kMetadataPayloadCll, kMetadataPayloadSizeCll,
                               AVM_MIF_KEY_FRAME),
          0);
    }
  }

  virtual void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                            ::libavm_test::DxDataIterator *dec_iter) {
    (void)dec_iter;
    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT ||
        pkt->kind == AVM_CODEC_CX_FRAME_NULL_PKT) {
      const size_t bitstream_size = pkt->data.frame.sz;
      const uint8_t *bitstream =
          static_cast<const uint8_t *>(pkt->data.frame.buf);
      bool is_keyframe =
          (pkt->data.frame.flags & AVM_FRAME_IS_KEY) ? true : false;

      // look for valid metadatas in bitstream
      bool itut_t35_metadata_found = false;
      // skip first 5 bytes (muh_metadata_type, muh_header_size,
      // muh_payload_size, layer_idc+persistence_idc+priority+reserved
      const size_t kMetadataObuSizeT35_payload_only = kMetadataObuSizeT35 - 5;
      const uint8_t *kMetadataObuT35_payload_pointer = &kMetadataObuT35[5];
      if (bitstream_size >= kMetadataObuSizeT35) {
        for (size_t i = 0;
             i <= bitstream_size - kMetadataObuSizeT35_payload_only; ++i) {
          if (memcmp(bitstream + i, kMetadataObuT35_payload_pointer,
                     kMetadataObuSizeT35_payload_only) == 0) {
            itut_t35_metadata_found = true;
          }
        }
      }
      ASSERT_EQ(itut_t35_metadata_found, 1u);

      if (is_keyframe) {
        // Testing for HDR MDCV metadata
        bool hdr_mdcv_metadata_found = false;
        // skip first 5 bytes (muh_metadata_type, muh_header_size,
        // muh_payload_size, layer_idc+persistence_idc+priority+reserved
        const size_t kMetadataObuSizeMdcv_payload_only =
            kMetadataObuSizeMdcv - 5;
        const uint8_t *kMetadataObuMdcv_payload_pointer = &kMetadataObuMdcv[5];
        if (bitstream_size >= kMetadataObuSizeMdcv) {
          for (size_t i = 0;
               i <= bitstream_size - kMetadataObuSizeMdcv_payload_only; ++i) {
            if (memcmp(bitstream + i, kMetadataObuMdcv_payload_pointer,
                       kMetadataObuSizeMdcv_payload_only) == 0) {
              hdr_mdcv_metadata_found = true;
            }
          }
        }
        ASSERT_TRUE(hdr_mdcv_metadata_found);

        // Testing for HDR CLL metadata
        bool hdr_cll_metadata_found = false;
        // skip first 5 bytes (muh_metadata_type, muh_header_size,
        // muh_payload_size, layer_idc+persistence_idc+priority+reserved
        const size_t kMetadataObuSizeCll_payload_only = kMetadataObuSizeCll - 5;
        const uint8_t *kMetadataObuCll_payload_pointer = &kMetadataObuCll[5];
        if (bitstream_size >= kMetadataObuSizeCll) {
          for (size_t i = 0;
               i <= bitstream_size - kMetadataObuSizeCll_payload_only; ++i) {
            if (memcmp(bitstream + i, kMetadataObuCll_payload_pointer,
                       kMetadataObuSizeCll_payload_only) == 0) {
              hdr_cll_metadata_found = true;
            }
          }
        }
        ASSERT_TRUE(hdr_cll_metadata_found);
      }
    }
  }

  virtual void DecompressedFrameHook(const avm_image_t &img,
                                     avm_codec_pts_t pts) {
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
          memcmp(kMetadataPayloadMdcv, img.metadata->metadata_array[1]->payload,
                 kMetadataPayloadSizeMdcv),
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
  ::libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 5);
  init_flags_ = AVM_CODEC_USE_PSNR;

  cfg_.g_w = 352;
  cfg_.g_h = 288;

  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 600;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_min_quantizer = 8;
  cfg_.rc_max_quantizer = 164;
  cfg_.rc_undershoot_pct = 50;
  cfg_.rc_overshoot_pct = 50;
  cfg_.rc_end_usage = AVM_CBR;
  cfg_.kf_mode = AVM_KF_AUTO;
  cfg_.g_lag_in_frames = 1;
  cfg_.kf_min_dist = cfg_.kf_max_dist = 3000;
  // Disable error_resilience mode.
  cfg_.g_error_resilient = 0;
  // Run at low bitrate.
  cfg_.rc_target_bitrate = 40;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV2_INSTANTIATE_TEST_SUITE(MetadataEncodeTest,
                           ::testing::Values(::libavm_test::kOnePassGood));

#endif  // CONFIG_AV2_ENCODER
}  // namespace

TEST(MetadataTest, MetadataAllocation) {
  avm_metadata_t *metadata =
      avm_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AVM_MIF_ANY_FRAME);
  ASSERT_NE(metadata, nullptr);
  avm_img_metadata_free(metadata);
}

TEST(MetadataTest, MetadataAllocationUserDataUnregistered) {
  avm_metadata_t *metadata = avm_img_metadata_alloc(
      OBU_METADATA_TYPE_USER_DATA_UNREGISTERED,
      kMetadataPayloadUserDataUnregistered,
      kMetadataPayloadSizeUserDataUnregistered, AVM_MIF_ANY_FRAME);
  ASSERT_NE(metadata, nullptr);
  avm_img_metadata_free(metadata);
}

TEST(MetadataTest, MetadataArrayAllocation) {
  avm_metadata_array_t *metadata_array = avm_img_metadata_array_alloc(2);
  ASSERT_NE(metadata_array, nullptr);

  metadata_array->metadata_array[0] =
      avm_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AVM_MIF_ANY_FRAME);
  metadata_array->metadata_array[1] =
      avm_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AVM_MIF_ANY_FRAME);

  avm_img_metadata_array_free(metadata_array);
}

TEST(MetadataTest, MetadataArrayAllocationUserDataUnregistered) {
  avm_metadata_array_t *metadata_array = avm_img_metadata_array_alloc(2);
  ASSERT_NE(metadata_array, nullptr);

  metadata_array->metadata_array[0] = avm_img_metadata_alloc(
      OBU_METADATA_TYPE_USER_DATA_UNREGISTERED,
      kMetadataPayloadUserDataUnregistered,
      kMetadataPayloadSizeUserDataUnregistered, AVM_MIF_ANY_FRAME);
  metadata_array->metadata_array[1] = avm_img_metadata_alloc(
      OBU_METADATA_TYPE_USER_DATA_UNREGISTERED,
      kMetadataPayloadUserDataUnregistered,
      kMetadataPayloadSizeUserDataUnregistered, AVM_MIF_ANY_FRAME);

  avm_img_metadata_array_free(metadata_array);
}

TEST(MetadataTest, AddMetadataToImage) {
  avm_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(avm_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AVM_MIF_ANY_FRAME),
            0);
  avm_img_metadata_array_free(image.metadata);
  EXPECT_EQ(avm_img_add_metadata(NULL, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AVM_MIF_ANY_FRAME),
            -1);
}

TEST(MetadataTest, AddMetadataToImageUserDataUnregistered) {
  avm_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(avm_img_add_metadata(
                &image, OBU_METADATA_TYPE_USER_DATA_UNREGISTERED,
                kMetadataPayloadUserDataUnregistered,
                kMetadataPayloadSizeUserDataUnregistered, AVM_MIF_ANY_FRAME),
            0);
  avm_img_metadata_array_free(image.metadata);
  EXPECT_EQ(avm_img_add_metadata(NULL, OBU_METADATA_TYPE_USER_DATA_UNREGISTERED,
                                 kMetadataPayloadUserDataUnregistered,
                                 kMetadataPayloadSizeUserDataUnregistered,
                                 AVM_MIF_ANY_FRAME),
            -1);
}

TEST(MetadataTest, RemoveMetadataFromImage) {
  avm_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(avm_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AVM_MIF_ANY_FRAME),
            0);
  avm_img_remove_metadata(&image);
  avm_img_remove_metadata(NULL);
}

TEST(MetadataTest, RemoveMetadataFromImageUserDataUnregistered) {
  avm_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(avm_img_add_metadata(
                &image, OBU_METADATA_TYPE_USER_DATA_UNREGISTERED,
                kMetadataPayloadUserDataUnregistered,
                kMetadataPayloadSizeUserDataUnregistered, AVM_MIF_ANY_FRAME),
            0);
  avm_img_remove_metadata(&image);
  avm_img_remove_metadata(NULL);
}

TEST(MetadataTest, CopyMetadataToFrameBuffer) {
  YV12_BUFFER_CONFIG yvBuf;
  yvBuf.metadata = NULL;

  avm_metadata_array_t *metadata_array = avm_img_metadata_array_alloc(1);
  ASSERT_NE(metadata_array, nullptr);

  metadata_array->metadata_array[0] =
      avm_img_metadata_alloc(OBU_METADATA_TYPE_ITUT_T35, kMetadataPayloadT35,
                             kMetadataPayloadSizeT35, AVM_MIF_ANY_FRAME);

  // Metadata_array
  int status = avm_copy_metadata_to_frame_buffer(&yvBuf, metadata_array);
  EXPECT_EQ(status, 0);
  status = avm_copy_metadata_to_frame_buffer(NULL, metadata_array);
  EXPECT_EQ(status, -1);
  avm_img_metadata_array_free(metadata_array);

  // Metadata_array_2
  avm_metadata_array_t *metadata_array_2 = avm_img_metadata_array_alloc(0);
  ASSERT_NE(metadata_array_2, nullptr);
  status = avm_copy_metadata_to_frame_buffer(&yvBuf, metadata_array_2);
  EXPECT_EQ(status, -1);
  avm_img_metadata_array_free(metadata_array_2);

  // YV12_BUFFER_CONFIG
  status = avm_copy_metadata_to_frame_buffer(&yvBuf, NULL);
  EXPECT_EQ(status, -1);
  avm_remove_metadata_from_frame_buffer(&yvBuf);
  avm_remove_metadata_from_frame_buffer(NULL);
}

TEST(MetadataTest, CopyMetadataToFrameBufferUserDataUnregistered) {
  YV12_BUFFER_CONFIG yvBuf;
  yvBuf.metadata = NULL;

  avm_metadata_array_t *metadata_array = avm_img_metadata_array_alloc(1);
  ASSERT_NE(metadata_array, nullptr);

  metadata_array->metadata_array[0] = avm_img_metadata_alloc(
      OBU_METADATA_TYPE_USER_DATA_UNREGISTERED,
      kMetadataPayloadUserDataUnregistered,
      kMetadataPayloadSizeUserDataUnregistered, AVM_MIF_ANY_FRAME);

  // Metadata_array
  int status = avm_copy_metadata_to_frame_buffer(&yvBuf, metadata_array);
  EXPECT_EQ(status, 0);
  status = avm_copy_metadata_to_frame_buffer(NULL, metadata_array);
  EXPECT_EQ(status, -1);
  avm_img_metadata_array_free(metadata_array);

  // YV12_BUFFER_CONFIG
  status = avm_copy_metadata_to_frame_buffer(&yvBuf, NULL);
  EXPECT_EQ(status, -1);
  avm_remove_metadata_from_frame_buffer(&yvBuf);
  avm_remove_metadata_from_frame_buffer(NULL);
}

TEST(MetadataTest, GetMetadataFromImage) {
  avm_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(avm_img_add_metadata(&image, OBU_METADATA_TYPE_ITUT_T35,
                                 kMetadataPayloadT35, kMetadataPayloadSizeT35,
                                 AVM_MIF_ANY_FRAME),
            0);

  EXPECT_TRUE(avm_img_get_metadata(NULL, 0) == NULL);
  EXPECT_TRUE(avm_img_get_metadata(&image, 1u) == NULL);
  EXPECT_TRUE(avm_img_get_metadata(&image, 10u) == NULL);

  const avm_metadata_t *metadata = avm_img_get_metadata(&image, 0);
  ASSERT_TRUE(metadata != NULL);
  ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
  EXPECT_EQ(
      memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
      0);

  avm_img_metadata_array_free(image.metadata);
}

TEST(MetadataTest, GetMetadataFromImageUserDataUnregistered) {
  avm_image_t image;
  image.metadata = NULL;

  ASSERT_EQ(avm_img_add_metadata(
                &image, OBU_METADATA_TYPE_USER_DATA_UNREGISTERED,
                kMetadataPayloadUserDataUnregistered,
                kMetadataPayloadSizeUserDataUnregistered, AVM_MIF_ANY_FRAME),
            0);

  EXPECT_TRUE(avm_img_get_metadata(NULL, 0) == NULL);
  EXPECT_TRUE(avm_img_get_metadata(&image, 1u) == NULL);
  EXPECT_TRUE(avm_img_get_metadata(&image, 10u) == NULL);

  const avm_metadata_t *metadata = avm_img_get_metadata(&image, 0);
  ASSERT_TRUE(metadata != NULL);
  ASSERT_EQ(metadata->sz, kMetadataPayloadSizeUserDataUnregistered);
  EXPECT_EQ(memcmp(kMetadataPayloadUserDataUnregistered, metadata->payload,
                   kMetadataPayloadSizeUserDataUnregistered),
            0);

  avm_img_metadata_array_free(image.metadata);
}

TEST(MetadataTest, ReadMetadatasFromImage) {
  avm_image_t image;
  image.metadata = NULL;

  uint32_t types[3];
  types[0] = OBU_METADATA_TYPE_ITUT_T35;
  types[1] = OBU_METADATA_TYPE_HDR_CLL;
  types[2] = OBU_METADATA_TYPE_HDR_MDCV;

  ASSERT_EQ(avm_img_add_metadata(&image, types[0], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AVM_MIF_ANY_FRAME),
            0);
  ASSERT_EQ(avm_img_add_metadata(&image, types[1], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AVM_MIF_KEY_FRAME),
            0);
  ASSERT_EQ(avm_img_add_metadata(&image, types[2], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AVM_MIF_KEY_FRAME),
            0);

  size_t number_metadata = avm_img_num_metadata(&image);
  ASSERT_EQ(number_metadata, 3u);
  for (size_t i = 0; i < number_metadata; ++i) {
    const avm_metadata_t *metadata = avm_img_get_metadata(&image, i);
    ASSERT_TRUE(metadata != NULL);
    ASSERT_EQ(metadata->type, types[i]);
    ASSERT_EQ(metadata->sz, kMetadataPayloadSizeT35);
    EXPECT_EQ(
        memcmp(kMetadataPayloadT35, metadata->payload, kMetadataPayloadSizeT35),
        0);
  }
  avm_img_metadata_array_free(image.metadata);
}

TEST(MetadataTest, ReadMetadatasFromImageIncludingUserDataUnregistered) {
  avm_image_t image;
  image.metadata = NULL;

  uint32_t types[4];
  types[0] = OBU_METADATA_TYPE_ITUT_T35;
  types[1] = OBU_METADATA_TYPE_USER_DATA_UNREGISTERED;
  types[2] = OBU_METADATA_TYPE_HDR_CLL;
  types[3] = OBU_METADATA_TYPE_HDR_MDCV;

  ASSERT_EQ(avm_img_add_metadata(&image, types[0], kMetadataPayloadT35,
                                 kMetadataPayloadSizeT35, AVM_MIF_ANY_FRAME),
            0);
  ASSERT_EQ(avm_img_add_metadata(
                &image, types[1], kMetadataPayloadUserDataUnregistered,
                kMetadataPayloadSizeUserDataUnregistered, AVM_MIF_ANY_FRAME),
            0);
  ASSERT_EQ(avm_img_add_metadata(&image, types[2], kMetadataPayloadCll,
                                 kMetadataPayloadSizeCll, AVM_MIF_KEY_FRAME),
            0);
  ASSERT_EQ(avm_img_add_metadata(&image, types[3], kMetadataPayloadMdcv,
                                 kMetadataPayloadSizeMdcv, AVM_MIF_KEY_FRAME),
            0);

  size_t number_metadata = avm_img_num_metadata(&image);
  ASSERT_EQ(number_metadata, 4u);

  // Check ITUT_T35
  const avm_metadata_t *metadata0 = avm_img_get_metadata(&image, 0);
  ASSERT_TRUE(metadata0 != NULL);
  ASSERT_EQ(metadata0->type, types[0]);
  ASSERT_EQ(metadata0->sz, kMetadataPayloadSizeT35);
  EXPECT_EQ(
      memcmp(kMetadataPayloadT35, metadata0->payload, kMetadataPayloadSizeT35),
      0);

  // Check USER_DATA_UNREGISTERED
  const avm_metadata_t *metadata1 = avm_img_get_metadata(&image, 1);
  ASSERT_TRUE(metadata1 != NULL);
  ASSERT_EQ(metadata1->type, types[1]);
  ASSERT_EQ(metadata1->sz, kMetadataPayloadSizeUserDataUnregistered);
  EXPECT_EQ(memcmp(kMetadataPayloadUserDataUnregistered, metadata1->payload,
                   kMetadataPayloadSizeUserDataUnregistered),
            0);

  // Check HDR_CLL
  const avm_metadata_t *metadata2 = avm_img_get_metadata(&image, 2);
  ASSERT_TRUE(metadata2 != NULL);
  ASSERT_EQ(metadata2->type, types[2]);
  ASSERT_EQ(metadata2->sz, kMetadataPayloadSizeCll);
  EXPECT_EQ(
      memcmp(kMetadataPayloadCll, metadata2->payload, kMetadataPayloadSizeCll),
      0);

  // Check HDR_MDCV
  const avm_metadata_t *metadata3 = avm_img_get_metadata(&image, 3);
  ASSERT_TRUE(metadata3 != NULL);
  ASSERT_EQ(metadata3->type, types[3]);
  ASSERT_EQ(metadata3->sz, kMetadataPayloadSizeMdcv);
  EXPECT_EQ(memcmp(kMetadataPayloadMdcv, metadata3->payload,
                   kMetadataPayloadSizeMdcv),
            0);

  avm_img_metadata_array_free(image.metadata);
}

#include "av2/common/banding_metadata.h"

TEST(MetadataTest, BandingHintsMetadata) {
  // Create a test banding metadata structure for encoding
  avm_banding_hints_metadata_t encode_metadata;
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
  ASSERT_EQ(avm_encode_banding_hints_metadata(&encode_metadata, payload,
                                              &payload_size),
            0);
  ASSERT_GT(payload_size, 0u);

  // Test decoding from payload (using separate instance of same structure type)
  avm_banding_hints_metadata_t decode_metadata;
  ASSERT_EQ(avm_decode_banding_hints_metadata(payload, payload_size,
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
  avm_image_t image;
  image.metadata = NULL;

  // Create test banding metadata
  avm_banding_hints_metadata_t banding_metadata;
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
  ASSERT_EQ(avm_img_add_banding_hints_metadata(&image, &banding_metadata,
                                               AVM_MIF_ANY_FRAME),
            0);

  // Verify metadata was added
  ASSERT_TRUE(image.metadata != nullptr);
  ASSERT_EQ(image.metadata->sz, 1u);
  ASSERT_EQ(image.metadata->metadata_array[0]->type,
            OBU_METADATA_TYPE_BANDING_HINTS);
  ASSERT_GT(image.metadata->metadata_array[0]->sz, 0u);

  // Test decoding the metadata from the image (using separate instance of same
  // structure type)
  avm_banding_hints_metadata_t decoded_metadata;
  ASSERT_EQ(avm_decode_banding_hints_metadata(
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

  avm_img_metadata_array_free(image.metadata);
}
