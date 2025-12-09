/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
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

#include "aom_dsp/bitwriter_buffer.h"

namespace {

// Test the examples in Table 25 in ITU-T H.274 (V3) (09/2023) and a few more.
//
// Bit string    codeNum
//     1            0
//    010           1
//    011           2
//   00100          3
//   00101          4
//   00110          5
//   00111          6
//  0001000         7
//  0001001         8
//  0001010         9
//  0001011        10
//  0001100        11
//  0001101        12
//  0001110        13
//  0001111        14
TEST(BitwriterBufferTest, UvlcOneByte) {
  static constexpr struct {
    uint32_t bit_offset;
    uint8_t byte;
  } kExpected[] = {
    { 1, 0x80 },  // 0
    { 3, 0x40 },  // 1
    { 3, 0x60 },  // 2
    { 5, 0x20 },  // 3
    { 5, 0x28 },  // 4
    { 5, 0x30 },  // 5
    { 5, 0x38 },  // 6
    { 7, 0x10 },  // 7
    { 7, 0x12 },  // 8
    { 7, 0x14 },  // 9
    { 7, 0x16 },  // 10
    { 7, 0x18 },  // 11
    { 7, 0x1a },  // 12
    { 7, 0x1c },  // 13
    { 7, 0x1e },  // 14
  };
  uint8_t dst[1];

  for (int i = 0; i < 15; i++) {
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_uvlc(&wb, i);
    ASSERT_EQ(wb.bit_offset, kExpected[i].bit_offset);
    EXPECT_EQ(wb.bit_buffer[0], kExpected[i].byte);
  }
}

// Tests two values with the maximum number (31) of leading zero bits.
TEST(BitwriterBufferTest, Uvlc31LeadingZeros) {
  uint8_t dst[8];

  // 2^31 - 1
  {
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_uvlc(&wb, 0x7fffffff);
    ASSERT_EQ(wb.bit_offset, 63u);
    EXPECT_EQ(wb.bit_buffer[0], 0x00);
    EXPECT_EQ(wb.bit_buffer[1], 0x00);
    EXPECT_EQ(wb.bit_buffer[2], 0x00);
    EXPECT_EQ(wb.bit_buffer[3], 0x01);
    EXPECT_EQ(wb.bit_buffer[4], 0x00);
    EXPECT_EQ(wb.bit_buffer[5], 0x00);
    EXPECT_EQ(wb.bit_buffer[6], 0x00);
    EXPECT_EQ(wb.bit_buffer[7], 0x00);
  }

  // 2^32 - 2
  {
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_uvlc(&wb, 0xfffffffe);
    ASSERT_EQ(wb.bit_offset, 63u);
    EXPECT_EQ(wb.bit_buffer[0], 0x00);
    EXPECT_EQ(wb.bit_buffer[1], 0x00);
    EXPECT_EQ(wb.bit_buffer[2], 0x00);
    EXPECT_EQ(wb.bit_buffer[3], 0x01);
    EXPECT_EQ(wb.bit_buffer[4], 0xff);
    EXPECT_EQ(wb.bit_buffer[5], 0xff);
    EXPECT_EQ(wb.bit_buffer[6], 0xff);
    EXPECT_EQ(wb.bit_buffer[7], 0xfe);
  }
}

// Test the examples in Table 25 and Table 26 in ITU-T H.274 (V3) (09/2023) and
// a few more.
//
// Bit string    codeNum    syntax element value
//     1            0                 0
//    010           1                 1
//    011           2                -1
//   00100          3                 2
//   00101          4                -2
//   00110          5                 3
//   00111          6                -3
//  0001000         7                 4
//  0001001         8                -4
//  0001010         9                 5
//  0001011        10                -5
//  0001100        11                 6
//  0001101        12                -6
//  0001110        13                 7
//  0001111        14                -7
TEST(BitwriterBufferTest, SvlcOneByte) {
  static constexpr struct {
    uint32_t bit_offset;
    uint8_t byte;
  } kExpected[] = {
    { 1, 0x80 },  // 0
    { 3, 0x40 },  // 1
    { 3, 0x60 },  // -1
    { 5, 0x20 },  // 2
    { 5, 0x28 },  // -2
    { 5, 0x30 },  // 3
    { 5, 0x38 },  // -3
    { 7, 0x10 },  // 4
    { 7, 0x12 },  // -4
    { 7, 0x14 },  // 5
    { 7, 0x16 },  // -5
    { 7, 0x18 },  // 6
    { 7, 0x1a },  // -6
    { 7, 0x1c },  // 7
    { 7, 0x1e },  // -7
  };
  uint8_t dst[1];

  int32_t sign = 1;
  for (int i = 0; i < 15; i++) {
    // input = (-1)^(i + 1) * Ceil( i / 2.0 )
    sign *= -1;
    const int32_t input = sign * ((i + 1) / 2);
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_svlc(&wb, input);
    ASSERT_EQ(wb.bit_offset, kExpected[i].bit_offset);
    EXPECT_EQ(wb.bit_buffer[0], kExpected[i].byte);
  }
}

// Tests four values with the maximum number (31) of leading zero bits.
TEST(BitwriterBufferTest, Svlc31LeadingZeros) {
  uint8_t dst[8];

  // 2^30
  {
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_svlc(&wb, 1 << 30);
    ASSERT_EQ(wb.bit_offset, 63u);
    EXPECT_EQ(wb.bit_buffer[0], 0x00);
    EXPECT_EQ(wb.bit_buffer[1], 0x00);
    EXPECT_EQ(wb.bit_buffer[2], 0x00);
    EXPECT_EQ(wb.bit_buffer[3], 0x01);
    EXPECT_EQ(wb.bit_buffer[4], 0x00);
    EXPECT_EQ(wb.bit_buffer[5], 0x00);
    EXPECT_EQ(wb.bit_buffer[6], 0x00);
    EXPECT_EQ(wb.bit_buffer[7], 0x00);
  }

  // -2^30
  {
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_svlc(&wb, -(1 << 30));
    ASSERT_EQ(wb.bit_offset, 63u);
    EXPECT_EQ(wb.bit_buffer[0], 0x00);
    EXPECT_EQ(wb.bit_buffer[1], 0x00);
    EXPECT_EQ(wb.bit_buffer[2], 0x00);
    EXPECT_EQ(wb.bit_buffer[3], 0x01);
    EXPECT_EQ(wb.bit_buffer[4], 0x00);
    EXPECT_EQ(wb.bit_buffer[5], 0x00);
    EXPECT_EQ(wb.bit_buffer[6], 0x00);
    EXPECT_EQ(wb.bit_buffer[7], 0x02);
  }

  // 2^31 - 1
  {
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_svlc(&wb, INT32_MAX);
    ASSERT_EQ(wb.bit_offset, 63u);
    EXPECT_EQ(wb.bit_buffer[0], 0x00);
    EXPECT_EQ(wb.bit_buffer[1], 0x00);
    EXPECT_EQ(wb.bit_buffer[2], 0x00);
    EXPECT_EQ(wb.bit_buffer[3], 0x01);
    EXPECT_EQ(wb.bit_buffer[4], 0xff);
    EXPECT_EQ(wb.bit_buffer[5], 0xff);
    EXPECT_EQ(wb.bit_buffer[6], 0xff);
    EXPECT_EQ(wb.bit_buffer[7], 0xfc);
  }

  // -2^31 + 1
  {
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_svlc(&wb, INT32_MIN + 1);
    ASSERT_EQ(wb.bit_offset, 63u);
    EXPECT_EQ(wb.bit_buffer[0], 0x00);
    EXPECT_EQ(wb.bit_buffer[1], 0x00);
    EXPECT_EQ(wb.bit_buffer[2], 0x00);
    EXPECT_EQ(wb.bit_buffer[3], 0x01);
    EXPECT_EQ(wb.bit_buffer[4], 0xff);
    EXPECT_EQ(wb.bit_buffer[5], 0xff);
    EXPECT_EQ(wb.bit_buffer[6], 0xff);
    EXPECT_EQ(wb.bit_buffer[7], 0xfe);
  }
}

// Test rg(2).
// Test examples.
//
// Value Quotient(q) Prefix Remainder(2 bits) Total bits Byte
// Value 0: q=0, r=0, -> "0" + "00" = "000" -> 0000 0000 == 0x00
// Value 1: q=0, r=1, -> "0" + "01" = "001" -> 0010 0000 == 0x20
// Value 2: q=0, r=2, -> "0" + "10" = "010" -> 0100 0000 == 0x40
// Value 3: q=0, r=3, -> "0" + "11" = "011" -> 0110 0000 == 0x60

// Value 4: q=1, r=0, -> "10" + "00" = "1000" -> 1000 0000 == 0x80
// Value 5: q=1, r=1, -> "10" + "01" = "1001" -> 1001 0000 == 0x90
// Value 6: q=1, r=2, -> "10" + "10" = "1010" -> 1010 0000 == 0xa0
// Value 7: q=1, r=3, -> "10" + "11" = "1011" -> 1011 0000 == 0xb0

// Value 8: q=2, r=0, -> "110" + "00" = "11000" -> 1100 0000 == 0xc0
// Value 9: q=2, r=1, -> "110" + "01" = "11001" -> 1100 1000 == 0xc8
// Value 10: q=2, r=2, -> "110" + "10" = "11010" -> 1101 0000 == 0xd0
// Value 11: q=2, r=3, -> "110" + "11" = "11011" -> 1101 1000 == 0xd8

// Value 12: q=3, r=0, -> "1110" + "00" = "111000" -> 1110 0000 == 0xe0
// Value 13: q=3, r=1, -> "1110" + "01" = "111001" -> 1110 0100 == 0xe4
// Value 14: q=3, r=2, -> "1110" + "10" = "111010" -> 1110 1000 == 0xe8
// Value 15: q=3, r=3, -> "1110" + "11" = "111011" -> 1110 1100 == 0xec

// Value 16: q=4, r=0, -> "11110" + "00" = "1111000" -> 1111 0000 == 0xf0
// Value 17: q=4, r=1, -> "11110" + "01" = "1111001" -> 1111 0010 == 0xf2
// Value 18: q=4, r=2, -> "11110" + "10" = "1111010" -> 1111 0100 == 0xf4
// Value 19: q=4, r=3, -> "11110" + "11" = "1111011" -> 1111 0110 == 0xf6

// Value 20: q=5, r=0, -> "111110" + "00" = "11111000" -> 1111 1000 == 0xf8
// Value 21: q=5, r=1, -> "111110" + "01" = "11111001" -> 1111 1001 == 0xf9
// Value 22: q=5, r=2, -> "111110" + "10" = "11111010" -> 1111 1010 == 0xfa
// Value 23: q=5, r=3, -> "111110" + "11" = "11111011" -> 1111 1011 == 0xfb
TEST(BitwriterBufferTest, Rg2OneByte) {
  static constexpr struct {
    uint32_t bit_offset;
    uint8_t byte;
  } kExpected[] = {
    { 3, 0x00 },  // 0
    { 3, 0x20 },  // 1
    { 3, 0x40 },  // 2
    { 3, 0x60 },  // 3
    { 4, 0x80 },  // 4
    { 4, 0x90 },  // 5
    { 4, 0xa0 },  // 6
    { 4, 0xb0 },  // 7
    { 5, 0xc0 },  // 8
    { 5, 0xc8 },  // 9
    { 5, 0xd0 },  // 10
    { 5, 0xd8 },  // 11
    { 6, 0xe0 },  // 12
    { 6, 0xe4 },  // 13
    { 6, 0xe8 },  // 14
    { 6, 0xec },  // 15
    { 7, 0xf0 },  // 16
    { 7, 0xf2 },  // 17
    { 7, 0xf4 },  // 18
    { 7, 0xf6 },  // 19
    { 8, 0xf8 },  // 20
    { 8, 0xf9 },  // 21
    { 8, 0xfa },  // 22
    { 8, 0xfb },  // 23
  };
  uint8_t dst[1];

  for (uint32_t i = 0; i < 24; i++) {
    struct aom_write_bit_buffer wb = { dst, 0 };
    aom_wb_write_rice_golomb(&wb, i, 2);
    ASSERT_EQ(wb.bit_offset, kExpected[i].bit_offset);
    EXPECT_EQ(wb.bit_buffer[0], kExpected[i].byte);
  }
}

}  // namespace
