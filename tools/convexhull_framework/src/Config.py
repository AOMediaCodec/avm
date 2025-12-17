#!/usr/bin/env python
## Copyright (c) 2021, Alliance for Open Media. All rights reserved
##
## This source code is subject to the terms of the BSD 3-Clause Clear License and the
## Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License was
## not distributed with this source code in the LICENSE file, you can obtain it
## at aomedia.org/license/software-license/bsd-3-c-c/.  If the Alliance for Open Media Patent
## License 1.0 was not distributed with this source code in the PATENTS file, you
## can obtain it at aomedia.org/license/patent-license/.
##
__author__ = "maggie.sun@intel.com, ryanlei@meta.com"

import os
import platform

import AV2CTCVideo
from AV2CTCVideo import CTC_VERSION


######################################
# configuration settings
######################################
RootPath = ".."
BinPath = os.path.join(RootPath, "bin")
WorkPath = os.path.join(RootPath, "test")


EnableOpenGOP = False
EnableParallelGopEncoding = True
EnableSubjectiveTest = False

if EnableSubjectiveTest:
    TEST_CONFIGURATIONS = ["RA"]
    DATASET = "AV2_SUBJECTIVE_TEST"
    ContentPath = "/refdata/all_users/enc_eval_videos/av2_subjective/"
    EnableTemporalFilter = True
    EnableVerificationTestConfig = True
    EnableTimingInfo = False
    UsePerfUtil = False
    EnableMD5 = False
else:
    TEST_CONFIGURATIONS = ["LD", "RA", "AI", "STILL"]
    DATASET = "CTC_TEST_SET"
    ContentPath = "/refdata/all_users/enc_eval_videos/av2_ctc/"
    EnableTemporalFilter = False
    EnableVerificationTestConfig = True
    EnableTimingInfo = True
    UsePerfUtil = True
    EnableMD5 = True


Platform = platform.system()
PSNR_Y_WEIGHT = 14.0
PSNR_U_WEIGHT = 1.0
PSNR_V_WEIGHT = 1.0
APSNR_Y_WEIGHT = 4.0
APSNR_U_WEIGHT = 1.0
APSNR_V_WEIGHT = 1.0

if CTC_VERSION in ["5.0", "6.0", "7.0", "8.0"]:
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AOM_CWG_Regular_CTCv5_v7.4.5.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AOM_CWG_AS_CTC_v10.0.xlsm")
elif CTC_VERSION == "4.0":
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AOM_CWG_Regular_CTCv4_v7.3.2.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AOM_CWG_AS_CTC_v9.7.1.xlsm")
elif CTC_VERSION == "3.0":
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AOM_CWG_Regular_CTC_v7.2.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AOM_CWG_AS_CTC_v9.7.xlsm")
elif CTC_VERSION == "2.0":
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AOM_CWG_Regular_CTC_v7.1.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AOM_CWG_AS_CTC_v9.7.xlsm")
else:
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AOM_CWG_Regular_CTC_v6.1.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AOM_CWG_AS_CTC_v9.6.xlsm")

############## Scaling settings ############################################
# down scaling ratio
DnScaleRatio = [1.0, 1.5, 2.0, 3.0, 4.0, 6.0]  # downscale ratio
# down and up scaling algorithm, the 2 lists should be of same size
DnScalingAlgos = ["lanczos"]  # ['bicubic', 'bilinear', 'gauss', 'lanczos', 'sinc']
UpScalingAlgos = ["lanczos"]  # ['bicubic', 'bilinear', 'gauss', 'lanczos', 'sinc']

ScaleMethods = ["hdrtool", "ffmpeg", "aom"]

HDRToolsConfigFileTemplate = os.path.join(BinPath, "HDRConvScalerY4MFile.cfg")
HDRConvert = os.path.join(BinPath, "HDRConvert")
AOMScaler = os.path.join(BinPath, "lanczos_resample_y4m")

##################### Encode Config ########################################
EncodeMethods = ["aom", "svt", "hm"]
CodecNames = ["av1", "av2", "hevc"]
SUFFIX = {"av1": ".obu", "av2": ".obu", "hevc": ".265"}
FFMPEG = os.path.join(BinPath, "ffmpeg")
AOMENC = os.path.join(BinPath, "avmenc-v8.0.0")
SVTAV1 = os.path.join(BinPath, "SvtAv1EncApp")
AOMDEC = os.path.join(BinPath, "avmdec-v8.0.0")
AV1ENC = os.path.join(BinPath, "av1enc")
AV1DEC = os.path.join(BinPath, "av1dec")
HMENC = os.path.join(BinPath, "TAppEncoderStatic")
VMAF = os.path.join(BinPath, "vmaf")
HEVCCfgFile = os.path.join(BinPath, "s2-hm-01.cfg")


if EnableSubjectiveTest:
    QPs = {
        "RA": [110, 122, 135, 147, 160, 172, 185, 197, 210, 222, 235],
    }
elif CTC_VERSION in ["2.0", "3.0", "4.0", "5.0", "6.0", "7.0", "8.0"]:
    QPs = {
        "LD": [110, 135, 160, 185, 210, 235],
        "RA": [110, 135, 160, 185, 210, 235],
        "AI": [85, 110, 135, 160, 185, 210],
        "AS": [110, 135, 160, 185, 210, 235],
        "STILL": [60, 85, 110, 135, 160, 185],
    }
else:
    QPs = {
        "LD": [23, 31, 39, 47, 55, 63],
        "RA": [23, 31, 39, 47, 55, 63],
        "AI": [15, 23, 31, 39, 47, 55],
        "AS": [23, 31, 39, 47, 55, 63],
        "STILL": [15, 23, 31, 39, 47, 55],
    }

HEVC_QPs = {
    "LD": [22, 27, 32, 37, 42, 47],
    "RA": [22, 27, 32, 37, 42, 47],
    "AI": [22, 27, 32, 37, 42, 47],
    "AS": [22, 27, 32, 37, 42, 47],
    "STILL": [22, 27, 32, 37, 42, 47],
}
MIN_GOP_LENGTH = 16
SUB_GOP_SIZE = 16
GOP_SIZE = 65
AS_DOWNSCALE_ON_THE_FLY = False

######################## quality evalution config #############################
QualityList = [
    "PSNR_Y",
    "PSNR_U",
    "PSNR_V",
    "SSIM_Y(dB)",
    "MS-SSIM_Y(dB)",
    "VMAF_Y",
    "VMAF_Y-NEG",
    "PSNR-HVS",
    "CIEDE2000",
    "APSNR_Y",
    "APSNR_U",
    "APSNR_V",
    "CAMBI",
]

EnablePreInterpolation = True
UsePCHIPInterpolation = False
# InterpolatePieces - 1 is the number of interpolated points generated between two qp points.
InterpolatePieces = 8

if (CTC_VERSION in ["7.0", "8.0"]) and (EnableVerificationTestConfig == False):
    FrameNum = {
        "LD": 130,
        "RA": 130,
        "AI": 15,
        "AS": 130,
        "STILL": 1,
    }
else:
    FrameNum = {
        "LD": 130,
        "RA": 130,
        "AI": 30,
        "AS": 130,
        "STILL": 1,
    }

if EnableSubjectiveTest:
    FrameNum = {
        "RA": {
            "BarScene_1920x1080_60fps_10bit_420.y4m": 360,
            "GregoryCactus_fr216_515_1080x1920_30p_420_10b_SDR.y4m": 300,
            "GregoryFence_fr0_299_1080x1920_30p_420_10b_SDR.y4m": 300,
            "Marathon2_3840x2160_30fps_10bit_420pf.y4m": 300,
            "meridian_aom_sdr_11872-12263.y4m": 392,
            "Metro_1920x1080_60fps_10bit_420.y4m": 600,
            "MountainBay2_3840x2160_30fps_420_10bit.y4m": 300,
            "TallBuildings2_3840x2160_30fps_10bit_420pf.y4m": 300,
            "YonseiS01_R_00_00.y4m": 300,
        }
    }

######################## post analysis #########################################
PostAnalysis_Path = os.path.join(RootPath, "analysis")
Path_RDResults = os.path.join(PostAnalysis_Path, "rdresult")
SummaryOutPath = os.path.join(PostAnalysis_Path, "summary")
Path_ScalingResults = os.path.join(PostAnalysis_Path, "scalingresult")

######################## logging #########################################
LoggerName = "AV2CTC"
LogLevels = ["NONE", "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG"]
