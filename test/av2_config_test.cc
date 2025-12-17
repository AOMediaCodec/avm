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
#include <string.h>

#include "common/av2_config.h"
#include "test/util.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av2/common/restoration.h"

namespace {

//
// Input buffers containing exactly one Sequence Header OBU.
//
// Each buffer is named according to the type of Sequence Header OBU ("Full"
// Sequence Header OBUs vs Sequence Header OBUs with the
// single_picture_header_flag set).
//
const uint8_t kFullSequenceHeaderObu[] = { 0x0c, 0x08, 0x00, 0x00, 0x00,
                                           0x04, 0x45, 0x7e, 0x3e, 0xff,
                                           0xfc, 0xc0, 0x20 };
const uint8_t kReducedStillImageSequenceHeaderObu[] = { 0x08, 0x08, 0x18,
                                                        0x22, 0x2b, 0xf1,
                                                        0xfe, 0xc0, 0x20 };

const uint8_t kAv2cAllZero[] = { 0, 0, 0, 0 };

// The size of AV2 config when no configOBUs are present at the end of the
// configuration structure.
const size_t kAv2cNoConfigObusSize = 4;

bool VerifyAv2c(const uint8_t *const obu_buffer, size_t obu_buffer_length) {
  Av2Config av2_config;
  memset(&av2_config, 0, sizeof(av2_config));
  bool parse_ok =
      get_av2config_from_obu(obu_buffer, obu_buffer_length, &av2_config) == 0;
  if (parse_ok) {
    EXPECT_EQ(1, av2_config.marker);
    EXPECT_EQ(1, av2_config.version);
    EXPECT_EQ(0, av2_config.seq_profile);
    EXPECT_EQ(0, av2_config.seq_level_idx_0);
    EXPECT_EQ(0, av2_config.seq_tier_0);
    EXPECT_EQ(0, av2_config.bitdepth_idx);
    EXPECT_EQ(0, av2_config.monochrome);
    EXPECT_EQ(1, av2_config.chroma_subsampling_x);
    EXPECT_EQ(1, av2_config.chroma_subsampling_y);
    EXPECT_EQ(0, av2_config.chroma_sample_position);
    EXPECT_EQ(0, av2_config.initial_presentation_delay_present);
    EXPECT_EQ(0, av2_config.initial_presentation_delay_minus_one);
  }
  return parse_ok && ::testing::Test::HasFailure() == false;
}

TEST(Av2Config, ObuInvalidInputs) {
  Av2Config av2_config;
  memset(&av2_config, 0, sizeof(av2_config));
  ASSERT_EQ(-1, get_av2config_from_obu(NULL, 0, NULL));
  ASSERT_EQ(-1, get_av2config_from_obu(&kFullSequenceHeaderObu[0], 0, NULL));
  ASSERT_EQ(-1, get_av2config_from_obu(&kFullSequenceHeaderObu[0],
                                       sizeof(kFullSequenceHeaderObu), NULL));
  ASSERT_EQ(-1,
            get_av2config_from_obu(NULL, sizeof(kFullSequenceHeaderObu), NULL));
  ASSERT_EQ(-1,
            get_av2config_from_obu(&kFullSequenceHeaderObu[0], 0, &av2_config));
}

TEST(Av2Config, ReadInvalidInputs) {
  Av2Config av2_config;
  memset(&av2_config, 0, sizeof(av2_config));
  size_t bytes_read = 0;
  ASSERT_EQ(-1, read_av2config(NULL, 0, NULL, NULL));
  ASSERT_EQ(-1, read_av2config(NULL, 4, NULL, NULL));
  ASSERT_EQ(-1, read_av2config(&kAv2cAllZero[0], 0, NULL, NULL));
  ASSERT_EQ(-1, read_av2config(&kAv2cAllZero[0], 4, &bytes_read, NULL));
  ASSERT_EQ(-1, read_av2config(NULL, 4, &bytes_read, &av2_config));
}

TEST(Av2Config, WriteInvalidInputs) {
  Av2Config av2_config;
  memset(&av2_config, 0, sizeof(av2_config));
  size_t bytes_written = 0;
  uint8_t av2c_buffer[4] = { 0 };
  ASSERT_EQ(-1, write_av2config(NULL, 0, NULL, NULL));
  ASSERT_EQ(-1, write_av2config(&av2_config, 0, NULL, NULL));
  ASSERT_EQ(-1, write_av2config(&av2_config, 0, &bytes_written, NULL));

  ASSERT_EQ(-1,
            write_av2config(&av2_config, 0, &bytes_written, &av2c_buffer[0]));
  ASSERT_EQ(-1, write_av2config(&av2_config, 4, &bytes_written, NULL));
}

TEST(Av2Config, DISABLED_GetAv2ConfigFromObu) {
  // Test parsing of a Sequence Header OBU with the single_picture_header_flag
  // unset-- aka a full Sequence Header OBU.
  ASSERT_TRUE(
      VerifyAv2c(kFullSequenceHeaderObu, sizeof(kFullSequenceHeaderObu)));

  // Test parsing of a reduced still image Sequence Header OBU.
  ASSERT_TRUE(VerifyAv2c(kReducedStillImageSequenceHeaderObu,
                         sizeof(kReducedStillImageSequenceHeaderObu)));
}

TEST(Av2Config, DISABLED_ReadWriteConfig) {
  Av2Config av2_config;
  memset(&av2_config, 0, sizeof(av2_config));

  // Test writing out the AV2 config.
  size_t bytes_written = 0;
  uint8_t av2c_buffer[4] = { 0 };
  ASSERT_EQ(0, write_av2config(&av2_config, sizeof(av2c_buffer), &bytes_written,
                               &av2c_buffer[0]));
  ASSERT_EQ(kAv2cNoConfigObusSize, bytes_written);
  for (size_t i = 0; i < kAv2cNoConfigObusSize; ++i) {
    ASSERT_EQ(kAv2cAllZero[i], av2c_buffer[i])
        << "Mismatch in output Av2Config at offset=" << i;
  }

  // Test reading the AV2 config.
  size_t bytes_read = 0;
  ASSERT_EQ(0, read_av2config(&kAv2cAllZero[0], sizeof(kAv2cAllZero),
                              &bytes_read, &av2_config));
  ASSERT_EQ(kAv2cNoConfigObusSize, bytes_read);
  ASSERT_EQ(0, write_av2config(&av2_config, sizeof(av2c_buffer), &bytes_written,
                               &av2c_buffer[0]));
  for (size_t i = 0; i < kAv2cNoConfigObusSize; ++i) {
    ASSERT_EQ(kAv2cAllZero[i], av2c_buffer[i])
        << "Mismatch in output Av2Config at offset=" << i;
  }
}

}  // namespace
