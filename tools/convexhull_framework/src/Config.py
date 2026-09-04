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

import yaml


######################################
# Load configuration from YAML file
######################################
def load_config(config_file=None):
    """
    Load configuration from YAML file.

    Args:
        config_file: Path to the YAML config file. If None, uses default config.yaml
                    in the same directory as this script.

    Returns:
        Dictionary containing configuration settings.
    """
    if config_file is None:
        # Default to config.yaml in the same directory as this script
        script_dir = os.path.dirname(os.path.abspath(__file__))
        config_file = os.path.join(script_dir, "config.yaml")

    if not os.path.exists(config_file):
        raise FileNotFoundError(f"Configuration file not found: {config_file}")

    with open(config_file, "r") as f:
        config = yaml.safe_load(f)

    return config


# Load configuration
_config = load_config()

######################################
# CTC Version (loaded from config.yaml)
######################################
CTC_VERSION = _config.get("ctc_version", "9.0")

######################################
# Path configuration
# BinPath and WorkPath can be specified in config.yaml,
# or they default to {RootPath}/bin and {RootPath}/test
######################################
RootPath = _config["paths"]["root"]
BinPath = _config["paths"].get("bin", os.path.join(RootPath, "bin"))
WorkPath = _config["paths"].get("work", os.path.join(RootPath, "test"))

######################################
# Feature flags
######################################
EnableOpenGOP = _config["features"]["enable_open_gop"]
EnableParallelGopEncoding = _config["features"]["enable_parallel_gop_encoding"]
EnableSubjectiveTest = _config["features"]["enable_subjective_test"]
EnablePreInterpolation = _config["features"]["enable_pre_interpolation"]
UsePCHIPInterpolation = _config["features"]["use_pchip_interpolation"]
AS_DOWNSCALE_ON_THE_FLY = _config["features"]["as_downscale_on_the_fly"]

######################################
# ECF (Extended Chroma Format) configuration
######################################
_ecf_config = _config.get("ecf", {})
EnableECF = _ecf_config.get("enabled", False)
ECF_ContentPath = _ecf_config.get("content_path", "")
ECF_TEST_CONFIGURATIONS = _ecf_config.get("configurations", ["AI", "RA", "LD"])
ECF_DATASET = _ecf_config.get("dataset", "ECF_TEST_SET")
ECF_GOP_SIZE = _ecf_config.get("gop_size", 33)
ECF_FrameNum = _ecf_config.get("frame_counts", {"AI": 5, "RA": 66, "LD": 33})
ECF_PSNR_YUV_Weights = _ecf_config.get(
    "psnr_yuv_weights",
    {
        "444": {"psnr_y_weight": 4.0, "psnr_u_weight": 1.0, "psnr_v_weight": 1.0},
        "422": {"psnr_y_weight": 8.0, "psnr_u_weight": 1.0, "psnr_v_weight": 1.0},
    },
)
ECF_Template = _ecf_config.get("template", "")

######################################
# Perceptual metrics (LPIPS, DISTS, ColorVideoVDP) configuration
# Optional and off by default; see requirements-perceptual.txt
######################################
_perceptual_config = _config.get("perceptual_metrics", {})
EnablePerceptualMetrics = _perceptual_config.get("enabled", False)
PerceptualMetricsList = _perceptual_config.get("metrics", ["LPIPS", "DISTS", "CVVDP"])
PerceptualFrameStep = _perceptual_config.get("frame_step", 1)
PerceptualDevice = _perceptual_config.get("device", "auto")
PerceptualLpipsNet = _perceptual_config.get("lpips_net", "alex")
PerceptualHDRClasses = _perceptual_config.get(
    "hdr_classes", ["G1", "G2", "ECF-3", "ECF-4"]
)
CVVDPDisplayMap = _perceptual_config.get(
    "cvvdp_display",
    {
        "2160p": "standard_4k",
        "1080p": "standard_fhd",
        "default": "standard_fhd",
        "hdr": "standard_hdr_pq",
    },
)
_color_conv = _perceptual_config.get("color_conversion", {})
PerceptualSDRMatrix = _color_conv.get("sdr_matrix", "bt709")
PerceptualHDRMatrix = _color_conv.get("hdr_matrix", "bt2020nc")
PerceptualDefaultRange = _color_conv.get("default_range", "tv")

######################################
# Test configuration (depends on EnableSubjectiveTest and EnableECF)
######################################
if EnableSubjectiveTest:
    TEST_CONFIGURATIONS = ["RA"]
    DATASET = "AV2_SUBJECTIVE_TEST"
    ContentPath = _config["paths"]["subjective_content"]
    EnableTemporalFilter = True
    EnableVerificationTestConfig = True
    EnableTimingInfo = False
    UsePerfUtil = False
    EnableMD5 = False
elif EnableECF:
    TEST_CONFIGURATIONS = ECF_TEST_CONFIGURATIONS
    DATASET = ECF_DATASET
    ContentPath = ECF_ContentPath
    EnableTemporalFilter = _config["features"].get("enable_temporal_filter", False)
    EnableVerificationTestConfig = _config["features"][
        "enable_verification_test_config"
    ]
    EnableTimingInfo = _config["features"]["enable_timing_info"]
    UsePerfUtil = _config["features"]["use_perf_util"]
    EnableMD5 = _config["features"]["enable_md5"]
else:
    TEST_CONFIGURATIONS = _config["test"]["configurations"]
    DATASET = _config["test"]["dataset"]
    ContentPath = _config["paths"]["content"]
    EnableTemporalFilter = _config["features"].get("enable_temporal_filter", False)
    EnableVerificationTestConfig = _config["features"][
        "enable_verification_test_config"
    ]
    EnableTimingInfo = _config["features"]["enable_timing_info"]
    UsePerfUtil = _config["features"]["use_perf_util"]
    EnableMD5 = _config["features"]["enable_md5"]

######################################
# Platform and quality weights
######################################
Platform = platform.system()
PSNR_Y_WEIGHT = _config["quality"]["psnr_y_weight"]
PSNR_U_WEIGHT = _config["quality"]["psnr_u_weight"]
PSNR_V_WEIGHT = _config["quality"]["psnr_v_weight"]
APSNR_Y_WEIGHT = _config["quality"]["apsnr_y_weight"]
APSNR_U_WEIGHT = _config["quality"]["apsnr_u_weight"]
APSNR_V_WEIGHT = _config["quality"]["apsnr_v_weight"]

######################################
# CTC template files (derived from CTC_VERSION)
######################################
if CTC_VERSION in ["5.0", "6.0", "7.0", "8.0", "9.0"]:
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AVM_CWG_Regular_CTCv5_v7.4.5.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AVM_CWG_AS_CTC_v10.0.xlsm")
elif CTC_VERSION == "4.0":
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AVM_CWG_Regular_CTCv4_v7.3.2.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AVM_CWG_AS_CTC_v9.7.1.xlsm")
elif CTC_VERSION == "3.0":
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AVM_CWG_Regular_CTC_v7.2.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AVM_CWG_AS_CTC_v9.7.xlsm")
elif CTC_VERSION == "2.0":
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AVM_CWG_Regular_CTC_v7.1.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AVM_CWG_AS_CTC_v9.7.xlsm")
else:
    CTC_RegularXLSTemplate = os.path.join(BinPath, "AVM_CWG_Regular_CTC_v6.1.xlsm")
    CTC_ASXLSTemplate = os.path.join(BinPath, "AVM_CWG_AS_CTC_v9.6.xlsm")

CTC_ECFXLSTemplate = os.path.join(BinPath, ECF_Template) if ECF_Template else ""

######################################
# Scaling settings
######################################
DnScaleRatio = _config["scaling"]["downscale_ratios"]
DnScalingAlgos = _config["scaling"]["downscale_algos"]
UpScalingAlgos = _config["scaling"]["upscale_algos"]

ScaleMethods = ["hdrtool", "ffmpeg", "aom"]

HDRToolsConfigFileTemplate = os.path.join(BinPath, "HDRConvScalerY4MFile.cfg")
HDRConvert = os.path.join(BinPath, _config["executables"]["hdr_convert"])
AOMScaler = os.path.join(BinPath, _config["executables"]["aom_scaler"])

######################################
# Encode Config
######################################
EncodeMethods = ["aom", "svt", "hm"]
CodecNames = ["av1", "av2", "hevc"]
SUFFIX = {"av1": ".obu", "av2": ".obu", "hevc": ".265"}

FFMPEG = os.path.join(BinPath, _config["executables"]["ffmpeg"])
AOMENC = os.path.join(BinPath, _config["executables"]["aomenc"])
SVTAV1 = os.path.join(BinPath, _config["executables"]["svtav1"])
AOMDEC = os.path.join(BinPath, _config["executables"]["aomdec"])
AV1ENC = os.path.join(BinPath, _config["executables"]["av1enc"])
AV1DEC = os.path.join(BinPath, _config["executables"]["av1dec"])
HMENC = os.path.join(BinPath, _config["executables"]["hmenc"])
VMAF = os.path.join(BinPath, _config["executables"]["vmaf"])
HEVCCfgFile = os.path.join(BinPath, "s2-hm-01.cfg")

######################################
# QP configuration
######################################
if EnableSubjectiveTest:
    QPs = {
        "RA": _config["subjective_qps"]["RA"],
    }
elif CTC_VERSION in ["2.0", "3.0", "4.0", "5.0", "6.0", "7.0", "8.0", "9.0"]:
    QPs = _config["encoding"]["qps"]
else:
    # Legacy QP values for older CTC versions
    QPs = {
        "LD": [23, 31, 39, 47, 55, 63],
        "RA": [23, 31, 39, 47, 55, 63],
        "AI": [15, 23, 31, 39, 47, 55],
        "AS": [23, 31, 39, 47, 55, 63],
        "STILL": [15, 23, 31, 39, 47, 55],
    }

HEVC_QPs = _config["encoding"]["hevc_qps"]

######################################
# GOP settings
######################################
MIN_GOP_LENGTH = _config["encoding"]["min_gop_length"]
SUB_GOP_SIZE = _config["encoding"]["sub_gop_size"]
if EnableECF:
    GOP_SIZE = ECF_GOP_SIZE
else:
    GOP_SIZE = _config["encoding"]["gop_size"]

######################################
# Quality evaluation config
######################################
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

# Perceptual metric columns, appended AFTER the MD5 columns in the RD CSV.
# Deliberately not part of QualityList: AV2CTCProgress.WriteSheet copies the
# CSV into the CTC .xlsm templates by absolute column index, so inserting
# these into QualityList would shift EncT/DecT/Instr/Cycles/MD5 rightward and
# corrupt every generated workbook.
if EnablePerceptualMetrics:
    PerceptualQualityList = [
        m for m in ["LPIPS", "DISTS", "CVVDP"] if m in PerceptualMetricsList
    ]
else:
    PerceptualQualityList = []

InterpolatePieces = _config["quality"]["interpolate_pieces"]

######################################
# Frame count configuration
######################################
if EnableSubjectiveTest:
    FrameNum = {"RA": _config["subjective_frame_counts"]}
elif EnableECF:
    FrameNum = ECF_FrameNum
elif (CTC_VERSION in ["7.0", "8.0", "9.0"]) and (EnableVerificationTestConfig == False):
    FrameNum = _config["frame_counts_non_verification"]
else:
    FrameNum = _config["frame_counts"]

######################################
# Post analysis paths (derived from RootPath)
######################################
PostAnalysis_Path = os.path.join(RootPath, "analysis")
Path_RDResults = os.path.join(PostAnalysis_Path, "rdresult")
SummaryOutPath = os.path.join(PostAnalysis_Path, "summary")
Path_ScalingResults = os.path.join(PostAnalysis_Path, "scalingresult")

######################################
# Logging
######################################
LoggerName = _config["logging"]["name"]
LogLevels = ["NONE", "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG"]
