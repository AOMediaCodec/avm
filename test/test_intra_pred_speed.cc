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

//  Test and time AOM intra-predictor functions

#include <stdio.h>
#include <string>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/aom_dsp_rtcd.h"

#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/md5_helper.h"
#include "aom/aom_integer.h"
#include "aom_ports/mem.h"
#include "aom_ports/aom_timer.h"
#include "av1/common/common_data.h"

// -----------------------------------------------------------------------------

namespace {

// Note:
// APPLY_UNIT_TESTS
// 1: Do unit tests
// 0: Generate MD5 array as required
#define APPLY_UNIT_TESTS 1

const int kBPS = 64;
const int kTotalPixels = kBPS * kBPS;
// 4 DC variants, V, H, PAETH, SMOOTH, SMOOTH_V, SMOOTH_H
const int kNumAv1IntraFuncs = 10;

#if APPLY_UNIT_TESTS
const char *kAv1IntraPredNames[kNumAv1IntraFuncs] = {
  "DC_PRED", "DC_LEFT_PRED", "DC_TOP_PRED", "DC_128_PRED",   "V_PRED",
  "H_PRED",  "PAETH_PRED",   "SMOOTH_PRED", "SMOOTH_V_PRED", "SMOOTH_H_PRED",
};
#endif  // APPLY_UNIT_TESTS

template <typename Pixel>
struct IntraPredTestMem {
  void Init(int block_width, int block_height, int bd) {
    ASSERT_LE(block_width, kBPS);
    ASSERT_LE(block_height, kBPS);
    // Note: for blocks having width <= 32 and height <= 32, we generate 32x32
    // random pixels as before to avoid having to recalculate all hashes again.
    const int block_size_upto_32 = (block_width <= 32) && (block_height <= 32);
    stride = block_size_upto_32 ? 32 : kBPS;
    num_pixels = stride * stride;
    libaom_test::ACMRandom rnd(libaom_test::ACMRandom::DeterministicSeed());
    above = above_mem + 16;
    const int mask = (1 << bd) - 1;
    for (int i = 0; i < num_pixels; ++i) ref_src[i] = rnd.Rand16() & mask;
    for (int i = 0; i < stride; ++i) left[i] = rnd.Rand16() & mask;
    for (int i = -1; i < stride; ++i) above[i] = rnd.Rand16() & mask;

    for (int i = stride; i < 2 * stride; ++i) {
      left[i] = rnd.Rand16() & mask;
      above[i] = rnd.Rand16() & mask;
    }
  }

  DECLARE_ALIGNED(16, Pixel, src[kTotalPixels]);
  DECLARE_ALIGNED(16, Pixel, ref_src[kTotalPixels]);
  DECLARE_ALIGNED(16, Pixel, left[2 * kBPS]);
  Pixel *above;
  int stride;
  int num_pixels;

 private:
  DECLARE_ALIGNED(16, Pixel, above_mem[2 * kBPS + 16]);
};

static const char *const kTxSizeStrings[TX_SIZES_ALL] = {
  "4X4",  "8X8",  "16X16", "32X32", "64X64", "4X8",   "8X4",
  "8X16", "16X8", "16X32", "32X16", "32X64", "64X32", "4X16",
  "16X4", "8X32", "32X8",  "16X64", "64X16",
#if CONFIG_EXT_RECUR_PARTITIONS
  "4X32", "32X4", "8X64",  "64X8",  "4X64",  "64X4",
#endif  // CONFIG_EXT_RECUR_PARTITIONS
};

void CheckMd5Signature(TX_SIZE tx_size, const char *const signatures[],
                       const void *data, size_t data_size, int elapsed_time,
                       int idx) {
  const std::string hbd_str = "Hbd ";
  const std::string name_str = hbd_str + "Intra" + kTxSizeStrings[tx_size];
  libaom_test::MD5 md5;
  md5.Add(reinterpret_cast<const uint8_t *>(data), data_size);
#if APPLY_UNIT_TESTS
  printf("Mode %s[%13s]: %5d ms     MD5: %s\n", name_str.c_str(),
         kAv1IntraPredNames[idx], elapsed_time, md5.Get());
  EXPECT_STREQ(signatures[idx], md5.Get());
#else
  (void)signatures;
  (void)elapsed_time;
  (void)idx;
  printf("\"%s\",\n", md5.Get());
#endif
}

}  // namespace

// -----------------------------------------------------------------------------
// High Bitdepth
namespace {

typedef void (*AvxHighbdPredFunc)(uint16_t *dst, ptrdiff_t y_stride,
                                  const uint16_t *above, const uint16_t *left,
                                  int bd);

typedef IntraPredTestMem<uint16_t> Av1HighbdIntraPredTestMem;

void TestHighbdIntraPred(TX_SIZE tx_size, AvxHighbdPredFunc const *pred_funcs,
                         const char *const signatures[]) {
  const int block_width = tx_size_wide[tx_size];
  const int block_height = tx_size_high[tx_size];
  const int num_pixels_per_test =
      block_width * block_height * kNumAv1IntraFuncs;
  const int kNumTests = static_cast<int>(2.e10 / num_pixels_per_test);
  Av1HighbdIntraPredTestMem intra_pred_test_mem;
  const int bd = 12;
  intra_pred_test_mem.Init(block_width, block_height, bd);

  for (int k = 0; k < kNumAv1IntraFuncs; ++k) {
    if (pred_funcs[k] == NULL) continue;
    memcpy(intra_pred_test_mem.src, intra_pred_test_mem.ref_src,
           sizeof(intra_pred_test_mem.src));
    aom_usec_timer timer;
    aom_usec_timer_start(&timer);
    for (int num_tests = 0; num_tests < kNumTests; ++num_tests) {
      pred_funcs[k](intra_pred_test_mem.src, intra_pred_test_mem.stride,
                    intra_pred_test_mem.above, intra_pred_test_mem.left, bd);
    }
    libaom_test::ClearSystemState();
    aom_usec_timer_mark(&timer);
    const int elapsed_time =
        static_cast<int>(aom_usec_timer_elapsed(&timer) / 1000);
    CheckMd5Signature(
        tx_size, signatures, intra_pred_test_mem.src,
        intra_pred_test_mem.num_pixels * sizeof(*intra_pred_test_mem.src),
        elapsed_time, k);
  }
}

static const char *const kHighbdSignatures[TX_SIZES_ALL][kNumAv1IntraFuncs] = {
  {
      // 4X4
      "11f74af6c5737df472f3275cbde062fa",
      "51bea056b6447c93f6eb8f6b7e8f6f71",
      "27e97f946766331795886f4de04c5594",
      "53ab15974b049111fb596c5168ec7e3f",
      "f0b640bb176fbe4584cf3d32a9b0320a",
      "729783ca909e03afd4b47111c80d967b",
      "6e30009c45474a22032678b1bd579c8f",
      "e57cba016d808aa8a35619df2a65f049",
      "55a6c37f39afcbbf5abca4a985b96459",
      "a623d45b37dafec1f8a75c4c5218913d",
  },
  {
      // 8X8
      "03da8829fe94663047fd108c5fcaa71d",
      "ecdb37b8120a2d3a4c706b016bd1bfd7",
      "1d4543ed8d2b9368cb96898095fe8a75",
      "f791c9a67b913cbd82d9da8ecede30e2",
      "065c70646f4dbaff913282f55a45a441",
      "51f87123616662ef7c35691497dfd0ba",
      "85c01ba03df68f9ece7bd3fa0f8980e6",
      "ad19b7dac092f56df6d054e1f67f21e7",
      "0edc415b5dd7299f7a34fb9f71d31d78",
      "2bc8ec19e9f4b77a64b8a0a1f6aec7e7",
  },
  {
      // 16X16
      "e33cb3f56a878e2fddb1b2fc51cdd275",
      "c7bff6f04b6052c8ab335d726dbbd52d",
      "d0b0b47b654a9bcc5c6008110a44589b",
      "78f5da7b10b2b9ab39f114a33b6254e9",
      "c78e31d23831abb40d6271a318fdd6f3",
      "90d1347f4ec9198a0320daecb6ff90b8",
      "e63ded54ab3d0e8728b6f24d4f01e53f",
      "35ce21fbe0ea114c089fc3489a78155d",
      "f277f6ef8e4d717f1f0dfe2706ac197d",
      "e8014d3f41256976c02e0f1e622ba2b9",
  },
  {
      // 32X32
      "a3e8056ba7e36628cce4917cd956fedd",
      "cc7d3024fe8748b512407edee045377e",
      "2aab0a0f330a1d3e19b8ecb8f06387a3",
      "a547bc3fb7b06910bf3973122a426661",
      "26f712514da95042f93d6e8dc8e431dc",
      "bb08c6e16177081daa3d936538dbc2e3",
      "84bf83f94a51b33654ca940c6f8bc057",
      "7168b03fc31bf29596a344d6a35d007c",
      "b073a70d3672f1282236994f5d12e94b",
      "c51607aebad5dcb3c1e3b58ef9e5b84e",
  },
  {
      // 64X64
      "a6baa0d4bfb2269a94c7a38f86a4bccf",
      "3f1ef5f473a49eba743f17a3324adf9d",
      "12ac11889ae5f55b7781454efd706a6a",
      "d9a906c0e692b22e1b4414e71a704b7e",
      "47d4cadd56f70c11ff8f3e5d8df81161",
      "de997744cf24c16c5ac2a36b02b351cc",
      "23781211ae178ddeb6c4bb97a6bd7d83",
      "a79d2e28340ca34b9e37daabbf030f63",
      "0372bd3ddfc258750a6ac106b70587f4",
      "228ef625d9460cbf6fa253a16a730976",
  },
  {
      // 4X8
      "22d519b796d59644043466320e4ccd14",
      "09513a738c49b3f9542d27f34abbe1d5",
      "807ae5e8813443ff01e71be6efacfb69",
      "cbfa18d0293430b6e9708b0be1fd2394",
      "346c354c34ec7fa780b576db355dab88",
      "f97dae85c35359632380b09ca98d611e",
      "698ae351d8896d89ed9e4e67b6e53eda",
      "dcc197034a9c45a3d8238bf085835f4e",
      "7a35e2c42ffdc2efc2d6d1d75a100fc7",
      "41ab6cebd4516c87a91b2a593e2c2506",
  },
  {
      // 8X4
      "d58cd4c4bf3b7bbaa5db5e1a5622ec78",
      "6e572c35aa782d00cafcb99e9ea047ea",
      "e8c22a3702b416dc9ab974505afbed09",
      "aaa4e4762a795aad7ad74de0c662c4e4",
      "a19f9101967383c3dcbd516dc317a291",
      "9ab8cb91f1a595b9ebe3fe8de58031aa",
      "2cf9021d5f1169268699807ee118b65f",
      "ee9605fcbd6fb871f1c5cd81a6989327",
      "b4871af8316089e3e23522175df7e93f",
      "d33301e1c2cb173be46792a22d19881a",
  },
  {
      // 8X16
      "4562de1d0336610880fdd5685498a9ec",
      "16310fa7076394f16fc85c4b149d89c9",
      "0e94af88e1dc573b6f0f499cddd1f530",
      "dfd245ee20d091c67809160340365aa9",
      "d3562504327f70c096c5be23fd8a3747",
      "601b853558502acbb5135eadd2da117a",
      "3c624345a723a1b2b1bea05a6a08bc99",
      "2a9c781de609e0184cc7ab442050f4e5",
      "0ddc5035c22252747126b61fc238c74d",
      "e43f5d83bab759af69c7b6773fc8f9b2",
  },
  {
      // 16X8
      "a57d6b5a9bfd30c29591d8717ace9c51",
      "f5907ba97ee6c53e339e953fc8d845ee",
      "ea3aa727913ce45af06f89dd1808db5f",
      "408af4f23e48d14b48ee35ae094fcd18",
      "85c41cbcb5d744f7961e8950026fbffe",
      "8a4e588a837638887ba671f8d4910485",
      "b792d8826b67a21757ea7097cff9e05b",
      "f94ce7101bb87fd3bb9312112527dbf4",
      "688c6660a6dc6fa61fa1aa38e708c209",
      "0cdf641b4f81d69509c92ae0b93ef5ff",
  },
  {
      // 16X32
      "aee4b3b0e3cc02d48e2c40d77f807927",
      "8baef2b2e789f79c8df9d90ad10f34a4",
      "038c38ee3c4f090bb8d736eab136aafc",
      "1a3de2aaeaffd68a9fd6c7f6557b83f3",
      "385c6e0ea29421dd81011a2934641e26",
      "6cf96c285d1a2d4787f955dad715b08c",
      "2d7f75dcd73b9528c8396279ff09ff3a",
      "5a63cd1841e4ed470e4ca5ef845f2281",
      "610d899ca945fbead33287d4335a8b32",
      "6bafaad81fce37be46730187e78d8b11",
  },
  {
      // 32X16
      "290b23c9f5a1de7905bfa71a942da29b",
      "701e7b82593c66da5052fc4b6afd79ce",
      "4da828c5455cd246735a663fbb204989",
      "e3fbeaf234efece8dbd752b77226200c",
      "4d1d8c969f05155a7e7e84cf7aad021b",
      "c22e4877c2c946d5bdc0d542e29e70cf",
      "8ac1ce815e7780500f842b0beb0bb980",
      "9fee2e2502b507f25bfad30a55b0b610",
      "4ced9c212ec6f9956e27f68a91b59fef",
      "4a7a0b93f138bb0863e4e465b01ec0b1",
  },
  {
      // 32X64
      "ad9cfc395a5c5644a21d958c7274ac14",
      "f29d6d03c143ddf96fef04c19f2c8333",
      "a8bdc852ef704dd4975c61893e8fbc3f",
      "7d0bd7dea26226741dbca9a97f27fa74",
      "45c27c5cca9a91b6ae8379feb0881c9f",
      "8a0b78df1e001b85c874d686eac4aa1b",
      "ce9fa75fac54a3f6c0cc3f2083b938f1",
      "c0dca10d88762c954af18dc9e3791a39",
      "61df229eddfccab913b8fda4bb02f9ac",
      "4f4df6bc8d50a5600b573f0e44d70e66",
  },
  {
      // 64X32
      "db9d82921fd88b24fdff6f849f2f9c87",
      "5ecc7fdc52d2f575ad4f2d0e9e6b1e11",
      "b4581311a0a73d95dfac7f8f44591032",
      "68bd283cfd1a125f6b2ee47cee874d36",
      "804179f05c032908a5e36077bb87c994",
      "fc5fd041a8ee779015394d0c066ee43c",
      "68f5579ccadfe9a1baafb158334a3db2",
      "fe237e45e215ab06d79046da9ad71e84",
      "9a8a938a6824551bf7d21b8fd1d70ea1",
      "eb7332f2017cd96882c76e7136aeaf53",
  },
  {
      // 4X16
      "7bafa307d507747b8132e7735b7f1c73",
      "e58bc2d8213a97d1fea9cfb73d7a9633",
      "435f8a8e8bbf14dbf2fe16b2be9e97aa",
      "1d0e767b68d84acbfb50b7a04e633836",
      "5f713bd7b324fe73bb7063e35ee14e5e",
      "0dac4e1fa3d59814202715468c01ed56",
      "47709d1db4a330c7a8900f450e6fddd1",
      "258e0b930bb27db28f05da9cf7d1ee7c",
      "36cf030fbae767912593efea045bfff5",
      "248d7aceabb7499febae663fae41a920",
  },
  {
      // 16X4
      "04dde98e632670e393704742c89f9067",
      "8c72543f1664651ae1fa08e2ac0adb9b",
      "2354a2cdc2773aa2df8ab4010db1be39",
      "6300ad3221c26da39b10e0e6d87ee3be",
      "8ea30b661c6ba60b28d3167f19e449b8",
      "fb6c1e4ff101a371cede63c2955cdb7e",
      "a517c06433d6d7927b16a72184a23e92",
      "393828be5d62ab6c48668bea5e2f801a",
      "b1e510c542013eb9d6fb188dea2ce90a",
      "569a8f2fe01679ca216535ecbcdccb62",
  },
  {
      // 8X32
      "9d541865c185ca7607852852613ac1fc",
      "b96be67f08c6b5fa5ebd3411299c2f7c",
      "75a2dcf50004b9d188849b048239767e",
      "429492ff415c9fd9b050d73b2ad500f8",
      "64b3606c1ccd036bd766bd5711392cf4",
      "cb59844a0f01660ac955bae3511f1100",
      "3e076155b7a70e8828618e3f33b51e3d",
      "ed2d1f597ab7c50beff690f737cf9726",
      "7909c6a26aaf20c59d996d3e5b5f9c29",
      "965798807240c98c6f7cc9b457ed0773",
  },
  {
      // 32X8
      "36f391aa31619eec1f4d9ee95ea454cc",
      "b82648f14eeba2527357cb50bc3223cb",
      "7a7b2adf429125e8bee9d1d00a66e13f",
      "4198e4d6ba503b7cc2d7e96bb845f661",
      "96c160d2ec1be9fe0cdea9682f14d257",
      "19a450bcebaa75afb4fc6bd1fd6434af",
      "2bd2e35967d43d0ec1c6587a36f204d5",
      "49799a99aa4ccfbd989bee92a99422f1",
      "955530e99813812a74659edeac3f5475",
      "f0316b84e378a19cd11b19a6e40b2914",
  },
  {
      // 16X64
      "8cba1b70a0bde29e8ef235cedc5faa7d",
      "96d00ddc7537bf7f196006591b733b4e",
      "cbf69d5d157c9f3355a4757b1d6e3414",
      "3ac1f642019493dec1b737d7a3a1b4e5",
      "35f9ee300d7fa3c97338e81a6f21dcd4",
      "aae335442e77c8ebc280f16ea50ba9c7",
      "a6140fdac2278644328be094d88731db",
      "2df93621b6ff100f7008432d509f4161",
      "c77bf5aee39e7ed4a3dd715f816f452a",
      "02109bd63557d90225c32a8f1338258e",
  },
  {
      // 64X16
      "a5e2f9fb685d5f4a048e9a96affd25a4",
      "1348f249690d9eefe09d9ad7ead2c801",
      "525da4b187acd81b1ff1116b60461141",
      "e99d072de858094c98b01bd4a6772634",
      "873bfa9dc24693f19721f7c8d527f7d3",
      "0acfc6507bd3468e9679efc127d6e4b9",
      "57d03f8d079c7264854e22ac1157cfae",
      "6c2c4036f70c7d957a9399b5436c0774",
      "42b8e4a97b7f8416c72a5148c031c0b1",
      "a38a2c5f79993dfae8530e9e25800893",
  },
#if CONFIG_EXT_RECUR_PARTITIONS
  {
      // 4X32
      "c1f205381dd47ee115861c8df74a8d2a",
      "d308c629b42a56caa55e93bc2ff08edf",
      "0484c9ed3e6fe75114af0223ca60b0b5",
      "fea0399bb03d7cd70120bd560cd25f7e",
      "1a90e01df720689d44f0e02363e29a6a",
      "c57921532a454508b8396e982d67645c",
      "f3efa10c9c8f6f5c613dd548a21eba92",
      "2d8350d3ff9eb151d08c2f7ef5b77a42",
      "c1b1559ce8a742a134ade2485495681b",
      "253807b389f030adb0db0a58dc3e2219",
  },
  {
      // 32X4
      "542034f74460992a3832a085a5fa7d25",
      "bcc87d9c4c8b3735d191b80f594301e1",
      "78af1ea02b695466de6ced787674d866",
      "b650485dac9bc7fa950c23a0c4d911ee",
      "4b8932f334cd82f80cd1817d9bf77e0b",
      "4c2d5d30d82966d7e4021743d482e75f",
      "9a504184040639a8deb436050b787858",
      "f7e001f9de5e21290740c47129b3a3e3",
      "f06e26a10b01d5d055a5f7b2dd96cd6a",
      "be5d8ae690f5e12bf0cdd0121dbc6c3c",
  },
  {
      // 8X64
      "80bf84825acf743d2652616c30b24d32",
      "22ee32f73153ff733e171d1a452a59d3",
      "55d6b331e94c22554ff8029d80296a17",
      "9245c7da44f7dacfa174cb76eba91c38",
      "9c021b9d2090c9ff6bace10358d7614f",
      "ff553672ed45246008710fe207664c8a",
      "8245747e70c63c642975c2d9863d3149",
      "e61fa1dcca5746a733e64028f649a426",
      "fb689879afd1b31bc17a3a6c8be6a564",
      "adbb9800477e45a70b98d55181b1784f",
  },
  {
      // 64X8
      "10e88eb167e37a48eb35fd7da7b49d01",
      "f4650467c1bf1b4ba2f2e40f43750b0c",
      "94ea269ebc197c9ac2067b6ef155bf23",
      "c1f64e228f33fcc03add69ac76ab71c7",
      "a26b47387bee6b736c4c793c32f36707",
      "b1e25d53395dd2865245baad8e09d080",
      "a050068aa252d8f6403626dbbc563967",
      "fe984530a619fe7a03006019e7ab9c3d",
      "667153043b1bd30a55752e05a8954895",
      "fdf532602fb459e6e8b203ff8f3c8937",
  },
  {
      // 4X64
      "eccb8c3bb21347c15476e5c95df1dca5",
      "db5b2789a9721bf272315e61d05a5fa6",
      "4f6862b525ebd927d313aa227f1e94a1",
      "8a16ff84d8fcab14462fd27a40c9373a",
      "759f6ca39945c4e0f3c77c985ab407dc",
      "44c0d6683d17fd4ae8f877f0164715da",
      "78caa4994daa63ee6bdd92737b2b3f52",
      "62b1bd244e363a528af5455fe0893c53",
      "6ecdf103ef4946a3ea4b2ed4eed34601",
      "49e8603f6a245411ae5664088431e3be",
  },
  {
      // 64X4
      "94c22d8118c0271bc3b00d3f3dcd4300",
      "9449b10f30f1e001ed86ce3fb6e90f0f",
      "cbe689a5948a18890b773f60e6767f19",
      "d9ea12ab2299ef331e3285bcac880617",
      "79d110d646e65a15d08e385598043e64",
      "c72945ce7b99d36883183f30de0601e5",
      "d7c7a7d49f418a7db3e5ae44aac03385",
      "eafe16d5686f2e050c9aa98291b5d3ea",
      "50d2243d7094a8a8091f7bb0e45a6dd3",
      "a25621808c86377cdddb630381f04f3d",
  },
#endif  // CONFIG_EXT_RECUR_PARTITIONS
};

}  // namespace

#define HIGHBD_INTRA_PRED_TEST(arch, tx_size, dc, dc_left, dc_top, dc_128, v, \
                               h, paeth, smooth, smooth_v, smooth_h)          \
  TEST(arch, DISABLED_##TestHighbdIntraPred_##tx_size) {                      \
    static const AvxHighbdPredFunc aom_intra_pred[] = {                       \
      dc, dc_left, dc_top, dc_128, v, h, paeth, smooth, smooth_v, smooth_h    \
    };                                                                        \
    TestHighbdIntraPred(tx_size, aom_intra_pred, kHighbdSignatures[tx_size]); \
  }

// -----------------------------------------------------------------------------
// 4x4, 4x8, 4x16

HIGHBD_INTRA_PRED_TEST(
    C_1, TX_4X4, aom_highbd_dc_predictor_4x4_c,
    aom_highbd_dc_left_predictor_4x4_c, aom_highbd_dc_top_predictor_4x4_c,
    aom_highbd_dc_128_predictor_4x4_c, aom_highbd_v_predictor_4x4_c,
    aom_highbd_h_predictor_4x4_c, aom_highbd_paeth_predictor_4x4_c,
    aom_highbd_smooth_predictor_4x4_c, aom_highbd_smooth_v_predictor_4x4_c,
    aom_highbd_smooth_h_predictor_4x4_c)

HIGHBD_INTRA_PRED_TEST(
    C_2, TX_4X8, aom_highbd_dc_predictor_4x8_c,
    aom_highbd_dc_left_predictor_4x8_c, aom_highbd_dc_top_predictor_4x8_c,
    aom_highbd_dc_128_predictor_4x8_c, aom_highbd_v_predictor_4x8_c,
    aom_highbd_h_predictor_4x8_c, aom_highbd_paeth_predictor_4x8_c,
    aom_highbd_smooth_predictor_4x8_c, aom_highbd_smooth_v_predictor_4x8_c,
    aom_highbd_smooth_h_predictor_4x8_c)

HIGHBD_INTRA_PRED_TEST(
    C_3, TX_4X16, aom_highbd_dc_predictor_4x16_c,
    aom_highbd_dc_left_predictor_4x16_c, aom_highbd_dc_top_predictor_4x16_c,
    aom_highbd_dc_128_predictor_4x16_c, aom_highbd_v_predictor_4x16_c,
    aom_highbd_h_predictor_4x16_c, aom_highbd_paeth_predictor_4x16_c,
    aom_highbd_smooth_predictor_4x16_c, aom_highbd_smooth_v_predictor_4x16_c,
    aom_highbd_smooth_h_predictor_4x16_c)

#if CONFIG_EXT_RECUR_PARTITIONS
HIGHBD_INTRA_PRED_TEST(
    C_4, TX_4X32, aom_highbd_dc_predictor_4x32_c,
    aom_highbd_dc_left_predictor_4x32_c, aom_highbd_dc_top_predictor_4x32_c,
    aom_highbd_dc_128_predictor_4x32_c, aom_highbd_v_predictor_4x32_c,
    aom_highbd_h_predictor_4x32_c, aom_highbd_paeth_predictor_4x32_c,
    aom_highbd_smooth_predictor_4x32_c, aom_highbd_smooth_v_predictor_4x32_c,
    aom_highbd_smooth_h_predictor_4x32_c)

HIGHBD_INTRA_PRED_TEST(
    C_5, TX_4X64, aom_highbd_dc_predictor_4x64_c,
    aom_highbd_dc_left_predictor_4x64_c, aom_highbd_dc_top_predictor_4x64_c,
    aom_highbd_dc_128_predictor_4x64_c, aom_highbd_v_predictor_4x64_c,
    aom_highbd_h_predictor_4x64_c, aom_highbd_paeth_predictor_4x64_c,
    aom_highbd_smooth_predictor_4x64_c, aom_highbd_smooth_v_predictor_4x64_c,
    aom_highbd_smooth_h_predictor_4x64_c)
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if HAVE_SSE2
HIGHBD_INTRA_PRED_TEST(SSE2_1, TX_4X4, aom_highbd_dc_predictor_4x4_sse2,
                       aom_highbd_dc_left_predictor_4x4_sse2,
                       aom_highbd_dc_top_predictor_4x4_sse2,
                       aom_highbd_dc_128_predictor_4x4_sse2,
                       aom_highbd_v_predictor_4x4_sse2,
                       aom_highbd_h_predictor_4x4_sse2, NULL, NULL, NULL, NULL)

HIGHBD_INTRA_PRED_TEST(SSE2_2, TX_4X8, aom_highbd_dc_predictor_4x8_sse2,
                       aom_highbd_dc_left_predictor_4x8_sse2,
                       aom_highbd_dc_top_predictor_4x8_sse2,
                       aom_highbd_dc_128_predictor_4x8_sse2,
                       aom_highbd_v_predictor_4x8_sse2,
                       aom_highbd_h_predictor_4x8_sse2, NULL, NULL, NULL, NULL)
#endif

// -----------------------------------------------------------------------------
// 8x8, 8x4, 8x16, 8x32

HIGHBD_INTRA_PRED_TEST(
    C_1, TX_8X8, aom_highbd_dc_predictor_8x8_c,
    aom_highbd_dc_left_predictor_8x8_c, aom_highbd_dc_top_predictor_8x8_c,
    aom_highbd_dc_128_predictor_8x8_c, aom_highbd_v_predictor_8x8_c,
    aom_highbd_h_predictor_8x8_c, aom_highbd_paeth_predictor_8x8_c,
    aom_highbd_smooth_predictor_8x8_c, aom_highbd_smooth_v_predictor_8x8_c,
    aom_highbd_smooth_h_predictor_8x8_c)

HIGHBD_INTRA_PRED_TEST(
    C_2, TX_8X4, aom_highbd_dc_predictor_8x4_c,
    aom_highbd_dc_left_predictor_8x4_c, aom_highbd_dc_top_predictor_8x4_c,
    aom_highbd_dc_128_predictor_8x4_c, aom_highbd_v_predictor_8x4_c,
    aom_highbd_h_predictor_8x4_c, aom_highbd_paeth_predictor_8x4_c,
    aom_highbd_smooth_predictor_8x4_c, aom_highbd_smooth_v_predictor_8x4_c,
    aom_highbd_smooth_h_predictor_8x4_c)

HIGHBD_INTRA_PRED_TEST(
    C_3, TX_8X16, aom_highbd_dc_predictor_8x16_c,
    aom_highbd_dc_left_predictor_8x16_c, aom_highbd_dc_top_predictor_8x16_c,
    aom_highbd_dc_128_predictor_8x16_c, aom_highbd_v_predictor_8x16_c,
    aom_highbd_h_predictor_8x16_c, aom_highbd_paeth_predictor_8x16_c,
    aom_highbd_smooth_predictor_8x16_c, aom_highbd_smooth_v_predictor_8x16_c,
    aom_highbd_smooth_h_predictor_8x16_c)

HIGHBD_INTRA_PRED_TEST(
    C_4, TX_8X32, aom_highbd_dc_predictor_8x32_c,
    aom_highbd_dc_left_predictor_8x32_c, aom_highbd_dc_top_predictor_8x32_c,
    aom_highbd_dc_128_predictor_8x32_c, aom_highbd_v_predictor_8x32_c,
    aom_highbd_h_predictor_8x32_c, aom_highbd_paeth_predictor_8x32_c,
    aom_highbd_smooth_predictor_8x32_c, aom_highbd_smooth_v_predictor_8x32_c,
    aom_highbd_smooth_h_predictor_8x32_c)

#if CONFIG_EXT_RECUR_PARTITIONS
HIGHBD_INTRA_PRED_TEST(
    C_5, TX_8X64, aom_highbd_dc_predictor_8x64_c,
    aom_highbd_dc_left_predictor_8x64_c, aom_highbd_dc_top_predictor_8x64_c,
    aom_highbd_dc_128_predictor_8x64_c, aom_highbd_v_predictor_8x64_c,
    aom_highbd_h_predictor_8x64_c, aom_highbd_paeth_predictor_8x64_c,
    aom_highbd_smooth_predictor_8x64_c, aom_highbd_smooth_v_predictor_8x64_c,
    aom_highbd_smooth_h_predictor_8x64_c)
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if HAVE_SSE2
HIGHBD_INTRA_PRED_TEST(SSE2_1, TX_8X8, aom_highbd_dc_predictor_8x8_sse2,
                       aom_highbd_dc_left_predictor_8x8_sse2,
                       aom_highbd_dc_top_predictor_8x8_sse2,
                       aom_highbd_dc_128_predictor_8x8_sse2,
                       aom_highbd_v_predictor_8x8_sse2,
                       aom_highbd_h_predictor_8x8_sse2, NULL, NULL, NULL, NULL)
HIGHBD_INTRA_PRED_TEST(SSE2_2, TX_8X4, aom_highbd_dc_predictor_8x4_sse2,
                       aom_highbd_dc_left_predictor_8x4_sse2,
                       aom_highbd_dc_top_predictor_8x4_sse2,
                       aom_highbd_dc_128_predictor_8x4_sse2,
                       aom_highbd_v_predictor_8x4_sse2,
                       aom_highbd_h_predictor_8x4_sse2, NULL, NULL, NULL, NULL)
HIGHBD_INTRA_PRED_TEST(SSE2_3, TX_8X16, aom_highbd_dc_predictor_8x16_sse2,
                       aom_highbd_dc_left_predictor_8x16_sse2,
                       aom_highbd_dc_top_predictor_8x16_sse2,
                       aom_highbd_dc_128_predictor_8x16_sse2,
                       aom_highbd_v_predictor_8x16_sse2,
                       aom_highbd_h_predictor_8x16_sse2, NULL, NULL, NULL, NULL)
#endif

#if HAVE_SSSE3
HIGHBD_INTRA_PRED_TEST(SSSE3, TX_8X8, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL)
#endif

// -----------------------------------------------------------------------------
// 16x16, 16x8, 16x32, 16x4, 16x64

HIGHBD_INTRA_PRED_TEST(
    C_1, TX_16X16, aom_highbd_dc_predictor_16x16_c,
    aom_highbd_dc_left_predictor_16x16_c, aom_highbd_dc_top_predictor_16x16_c,
    aom_highbd_dc_128_predictor_16x16_c, aom_highbd_v_predictor_16x16_c,
    aom_highbd_h_predictor_16x16_c, aom_highbd_paeth_predictor_16x16_c,
    aom_highbd_smooth_predictor_16x16_c, aom_highbd_smooth_v_predictor_16x16_c,
    aom_highbd_smooth_h_predictor_16x16_c)

HIGHBD_INTRA_PRED_TEST(
    C_2, TX_16X8, aom_highbd_dc_predictor_16x8_c,
    aom_highbd_dc_left_predictor_16x8_c, aom_highbd_dc_top_predictor_16x8_c,
    aom_highbd_dc_128_predictor_16x8_c, aom_highbd_v_predictor_16x8_c,
    aom_highbd_h_predictor_16x8_c, aom_highbd_paeth_predictor_16x8_c,
    aom_highbd_smooth_predictor_16x8_c, aom_highbd_smooth_v_predictor_16x8_c,
    aom_highbd_smooth_h_predictor_16x8_c)

HIGHBD_INTRA_PRED_TEST(
    C_3, TX_16X32, aom_highbd_dc_predictor_16x32_c,
    aom_highbd_dc_left_predictor_16x32_c, aom_highbd_dc_top_predictor_16x32_c,
    aom_highbd_dc_128_predictor_16x32_c, aom_highbd_v_predictor_16x32_c,
    aom_highbd_h_predictor_16x32_c, aom_highbd_paeth_predictor_16x32_c,
    aom_highbd_smooth_predictor_16x32_c, aom_highbd_smooth_v_predictor_16x32_c,
    aom_highbd_smooth_h_predictor_16x32_c)

HIGHBD_INTRA_PRED_TEST(
    C_4, TX_16X4, aom_highbd_dc_predictor_16x4_c,
    aom_highbd_dc_left_predictor_16x4_c, aom_highbd_dc_top_predictor_16x4_c,
    aom_highbd_dc_128_predictor_16x4_c, aom_highbd_v_predictor_16x4_c,
    aom_highbd_h_predictor_16x4_c, aom_highbd_paeth_predictor_16x4_c,
    aom_highbd_smooth_predictor_16x4_c, aom_highbd_smooth_v_predictor_16x4_c,
    aom_highbd_smooth_h_predictor_16x4_c)

HIGHBD_INTRA_PRED_TEST(
    C_5, TX_16X64, aom_highbd_dc_predictor_16x64_c,
    aom_highbd_dc_left_predictor_16x64_c, aom_highbd_dc_top_predictor_16x64_c,
    aom_highbd_dc_128_predictor_16x64_c, aom_highbd_v_predictor_16x64_c,
    aom_highbd_h_predictor_16x64_c, aom_highbd_paeth_predictor_16x64_c,
    aom_highbd_smooth_predictor_16x64_c, aom_highbd_smooth_v_predictor_16x64_c,
    aom_highbd_smooth_h_predictor_16x64_c)

#if HAVE_SSE2
HIGHBD_INTRA_PRED_TEST(SSE2_1, TX_16X16, aom_highbd_dc_predictor_16x16_sse2,
                       aom_highbd_dc_left_predictor_16x16_sse2,
                       aom_highbd_dc_top_predictor_16x16_sse2,
                       aom_highbd_dc_128_predictor_16x16_sse2,
                       aom_highbd_v_predictor_16x16_sse2,
                       aom_highbd_h_predictor_16x16_sse2, NULL, NULL, NULL,
                       NULL)
HIGHBD_INTRA_PRED_TEST(SSE2_2, TX_16X8, aom_highbd_dc_predictor_16x8_sse2,
                       aom_highbd_dc_left_predictor_16x8_sse2,
                       aom_highbd_dc_top_predictor_16x8_sse2,
                       aom_highbd_dc_128_predictor_16x8_sse2,
                       aom_highbd_v_predictor_16x8_sse2,
                       aom_highbd_h_predictor_16x8_sse2, NULL, NULL, NULL, NULL)
HIGHBD_INTRA_PRED_TEST(SSE2_3, TX_16X32, aom_highbd_dc_predictor_16x32_sse2,
                       aom_highbd_dc_left_predictor_16x32_sse2,
                       aom_highbd_dc_top_predictor_16x32_sse2,
                       aom_highbd_dc_128_predictor_16x32_sse2,
                       aom_highbd_v_predictor_16x32_sse2,
                       aom_highbd_h_predictor_16x32_sse2, NULL, NULL, NULL,
                       NULL)
#endif

#if HAVE_SSSE3
HIGHBD_INTRA_PRED_TEST(SSSE3_1, TX_16X16, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL)
#endif

#if HAVE_AVX2
HIGHBD_INTRA_PRED_TEST(AVX2_1, TX_16X16, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL)

HIGHBD_INTRA_PRED_TEST(AVX2_2, TX_16X8, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL)

HIGHBD_INTRA_PRED_TEST(AVX2_3, TX_16X32, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL)
#endif

// -----------------------------------------------------------------------------
// 32x32, 32x16, 32x64, 32x8

HIGHBD_INTRA_PRED_TEST(
    C_1, TX_32X32, aom_highbd_dc_predictor_32x32_c,
    aom_highbd_dc_left_predictor_32x32_c, aom_highbd_dc_top_predictor_32x32_c,
    aom_highbd_dc_128_predictor_32x32_c, aom_highbd_v_predictor_32x32_c,
    aom_highbd_h_predictor_32x32_c, aom_highbd_paeth_predictor_32x32_c,
    aom_highbd_smooth_predictor_32x32_c, aom_highbd_smooth_v_predictor_32x32_c,
    aom_highbd_smooth_h_predictor_32x32_c)

HIGHBD_INTRA_PRED_TEST(
    C_2, TX_32X16, aom_highbd_dc_predictor_32x16_c,
    aom_highbd_dc_left_predictor_32x16_c, aom_highbd_dc_top_predictor_32x16_c,
    aom_highbd_dc_128_predictor_32x16_c, aom_highbd_v_predictor_32x16_c,
    aom_highbd_h_predictor_32x16_c, aom_highbd_paeth_predictor_32x16_c,
    aom_highbd_smooth_predictor_32x16_c, aom_highbd_smooth_v_predictor_32x16_c,
    aom_highbd_smooth_h_predictor_32x16_c)

HIGHBD_INTRA_PRED_TEST(
    C_3, TX_32X64, aom_highbd_dc_predictor_32x64_c,
    aom_highbd_dc_left_predictor_32x64_c, aom_highbd_dc_top_predictor_32x64_c,
    aom_highbd_dc_128_predictor_32x64_c, aom_highbd_v_predictor_32x64_c,
    aom_highbd_h_predictor_32x64_c, aom_highbd_paeth_predictor_32x64_c,
    aom_highbd_smooth_predictor_32x64_c, aom_highbd_smooth_v_predictor_32x64_c,
    aom_highbd_smooth_h_predictor_32x64_c)

HIGHBD_INTRA_PRED_TEST(
    C_4, TX_32X8, aom_highbd_dc_predictor_32x8_c,
    aom_highbd_dc_left_predictor_32x8_c, aom_highbd_dc_top_predictor_32x8_c,
    aom_highbd_dc_128_predictor_32x8_c, aom_highbd_v_predictor_32x8_c,
    aom_highbd_h_predictor_32x8_c, aom_highbd_paeth_predictor_32x8_c,
    aom_highbd_smooth_predictor_32x8_c, aom_highbd_smooth_v_predictor_32x8_c,
    aom_highbd_smooth_h_predictor_32x8_c)

#if CONFIG_EXT_RECUR_PARTITIONS
HIGHBD_INTRA_PRED_TEST(
    C_5, TX_32X4, aom_highbd_dc_predictor_32x4_c,
    aom_highbd_dc_left_predictor_32x4_c, aom_highbd_dc_top_predictor_32x4_c,
    aom_highbd_dc_128_predictor_32x4_c, aom_highbd_v_predictor_32x4_c,
    aom_highbd_h_predictor_32x4_c, aom_highbd_paeth_predictor_32x4_c,
    aom_highbd_smooth_predictor_32x4_c, aom_highbd_smooth_v_predictor_32x4_c,
    aom_highbd_smooth_h_predictor_32x4_c)
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#if HAVE_SSE2
HIGHBD_INTRA_PRED_TEST(SSE2_1, TX_32X32, aom_highbd_dc_predictor_32x32_sse2,
                       aom_highbd_dc_left_predictor_32x32_sse2,
                       aom_highbd_dc_top_predictor_32x32_sse2,
                       aom_highbd_dc_128_predictor_32x32_sse2,
                       aom_highbd_v_predictor_32x32_sse2,
                       aom_highbd_h_predictor_32x32_sse2, NULL, NULL, NULL,
                       NULL)
HIGHBD_INTRA_PRED_TEST(SSE2_2, TX_32X16, aom_highbd_dc_predictor_32x16_sse2,
                       aom_highbd_dc_left_predictor_32x16_sse2,
                       aom_highbd_dc_top_predictor_32x16_sse2,
                       aom_highbd_dc_128_predictor_32x16_sse2,
                       aom_highbd_v_predictor_32x16_sse2,
                       aom_highbd_h_predictor_32x16_sse2, NULL, NULL, NULL,
                       NULL)
#endif

#if HAVE_SSSE3
HIGHBD_INTRA_PRED_TEST(SSSE3_1, TX_32X32, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL)
#endif

#if HAVE_AVX2
HIGHBD_INTRA_PRED_TEST(AVX2_1, TX_32X32, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL)

HIGHBD_INTRA_PRED_TEST(AVX2_2, TX_32X16, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, NULL)
#endif

// -----------------------------------------------------------------------------
// 64x64, 64x32, 64x16

HIGHBD_INTRA_PRED_TEST(
    C_1, TX_64X64, aom_highbd_dc_predictor_64x64_c,
    aom_highbd_dc_left_predictor_64x64_c, aom_highbd_dc_top_predictor_64x64_c,
    aom_highbd_dc_128_predictor_64x64_c, aom_highbd_v_predictor_64x64_c,
    aom_highbd_h_predictor_64x64_c, aom_highbd_paeth_predictor_64x64_c,
    aom_highbd_smooth_predictor_64x64_c, aom_highbd_smooth_v_predictor_64x64_c,
    aom_highbd_smooth_h_predictor_64x64_c)

HIGHBD_INTRA_PRED_TEST(
    C_2, TX_64X32, aom_highbd_dc_predictor_64x32_c,
    aom_highbd_dc_left_predictor_64x32_c, aom_highbd_dc_top_predictor_64x32_c,
    aom_highbd_dc_128_predictor_64x32_c, aom_highbd_v_predictor_64x32_c,
    aom_highbd_h_predictor_64x32_c, aom_highbd_paeth_predictor_64x32_c,
    aom_highbd_smooth_predictor_64x32_c, aom_highbd_smooth_v_predictor_64x32_c,
    aom_highbd_smooth_h_predictor_64x32_c)

HIGHBD_INTRA_PRED_TEST(
    C_3, TX_64X16, aom_highbd_dc_predictor_64x16_c,
    aom_highbd_dc_left_predictor_64x16_c, aom_highbd_dc_top_predictor_64x16_c,
    aom_highbd_dc_128_predictor_64x16_c, aom_highbd_v_predictor_64x16_c,
    aom_highbd_h_predictor_64x16_c, aom_highbd_paeth_predictor_64x16_c,
    aom_highbd_smooth_predictor_64x16_c, aom_highbd_smooth_v_predictor_64x16_c,
    aom_highbd_smooth_h_predictor_64x16_c)

#if CONFIG_EXT_RECUR_PARTITIONS
HIGHBD_INTRA_PRED_TEST(
    C_4, TX_64X4, aom_highbd_dc_predictor_64x4_c,
    aom_highbd_dc_left_predictor_64x4_c, aom_highbd_dc_top_predictor_64x4_c,
    aom_highbd_dc_128_predictor_64x4_c, aom_highbd_v_predictor_64x4_c,
    aom_highbd_h_predictor_64x4_c, aom_highbd_paeth_predictor_64x4_c,
    aom_highbd_smooth_predictor_64x4_c, aom_highbd_smooth_v_predictor_64x4_c,
    aom_highbd_smooth_h_predictor_64x4_c)

HIGHBD_INTRA_PRED_TEST(
    C_5, TX_64X8, aom_highbd_dc_predictor_64x8_c,
    aom_highbd_dc_left_predictor_64x8_c, aom_highbd_dc_top_predictor_64x8_c,
    aom_highbd_dc_128_predictor_64x8_c, aom_highbd_v_predictor_64x8_c,
    aom_highbd_h_predictor_64x8_c, aom_highbd_paeth_predictor_64x8_c,
    aom_highbd_smooth_predictor_64x8_c, aom_highbd_smooth_v_predictor_64x8_c,
    aom_highbd_smooth_h_predictor_64x8_c)
#endif  // CONFIG_EXT_RECUR_PARTITIONS
// -----------------------------------------------------------------------------

#include "test/test_libaom.cc"
